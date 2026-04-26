/* slap_core.c
 *
 * SLAP protocol core — initialisation and task management.
 *
 * Architecture:
 *
 *   HAL ISR  ──(osal_sem_give_from_isr)──► g_rx_sem
 *                                              │
 *                                    slap_rx_task_fn (high prio)
 *                                         ├── hal_receive()
 *                                         ├── slap_decode_packet()
 *                                         ├── slap_dispatch_packet()
 *                                         ├── slap_encode_packet()
 *                                         ├── hal_send()
 *                                         └── osal_watchdog_kick()
 *
 *   1-Hz timer ISR ──(eventgroup_set)──► SLAP_EVENT_SCHED_TICK
 *                                              │
 *                                    slap_sched_task_fn (low prio)
 *                                         ├── slap_scheduling_tick()
 *                                         └── osal_watchdog_kick()
 */

#include "slap_core.h"
#include "slap_packet.h"
#include "slap_databank.h"
#include "slap_dispatcher.h"
#include "hal/hal.h"
#include "osal/osal.h"

#include <string.h>

/* ----------------------------------------------------------------
 * MODULE-LEVEL OSAL OBJECTS
 * These are owned by slap_core and used by the two tasks.
 * Declared static — nothing outside this file accesses them directly.
 * ---------------------------------------------------------------- */

/* Binary semaphore: ISR gives it when bytes arrive; rx task takes it.
 * This replaces polling — the rx task sleeps with zero CPU usage
 * until real data arrives.                                         */
static osal_sem_t g_rx_sem;

/* Event group: coordinates the scheduling tick and system state.   */
static osal_eventgroup_t g_slap_events;

/* Task handles: stored for potential suspend/resume from application */
static osal_task_t g_rx_task;
static osal_task_t g_sched_task;

/* file scope: */
static osal_queue_t g_rx_queue;


/* Wire buffers: static (never on the stack).
 * Two separate buffers so TX can be built while RX is still valid. */
static uint8_t g_rx_wire_buf[SLAP_MTU];
static uint8_t g_tx_wire_buf[SLAP_MTU];

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/**
 * Execute one full receive–decode–dispatch–encode–send cycle.
 * Called from slap_rx_task_fn() after the semaphore is taken.
 */
static void run_one_cycle(void)
{
    typedef struct { uint8_t data[SLAP_MTU]; uint16_t len; } rx_envelope_t;
    rx_envelope_t env;

    if (osal_queue_recv(&g_rx_queue, &env, 0) != OSAL_OK)
        return; /* nothing in queue */

    /* Use env.data and env.len instead of calling hal_receive() */
    
    /* Step 2: allocate packet structs from the static pool.
     * NEVER declare slap_packet_t as a local variable — it is ~2 KB. */
    slap_packet_t *req  = slap_databank_alloc();
    slap_packet_t *resp = slap_databank_alloc();

    if (req == NULL || resp == NULL) {
        /* DataBank pool exhausted — drop this packet.
         * This indicates SLAP_POOL_SIZE is too small for the
         * current packet burst rate. Increase it in slap_databank.h. */
        slap_databank_free(req);   /* free(NULL) is safe in our impl */
        slap_databank_free(resp);
        return;
    }

    /* Step 3: deserialise wire bytes into the request struct.
     * Validates the CRC if ECF_FLAG is set.                        */
    int decode_result = slap_decode_packet(env.data, env.len, req);
    if (decode_result != SLAP_OK) {
        /* Packet is malformed or CRC failed — discard silently.
         * In a more mature implementation you would increment a
         * diagnostic counter here for ground monitoring.            */
        slap_databank_free(req);
        slap_databank_free(resp);
        return;
    }

    /* Step 4: route to the appropriate service handler.
     * The handler populates resp in place.                          */
    int dispatch_result = slap_dispatch_packet(req, resp);

    if (dispatch_result == SLAP_OK) {
        /* Step 5: serialise the response struct into wire bytes.    */
        int tx_len = slap_encode_packet(resp,
                                        g_tx_wire_buf,
                                        (uint16_t)sizeof(g_tx_wire_buf));
        if (tx_len > 0) {
            /* Step 6: hand the encoded bytes to the HAL for
             * transmission over the physical transport.             */
            hal_send(g_tx_wire_buf, (uint16_t)tx_len);
        }
    }
    /* Service returned an error — no response is sent.
     * The ground station will detect the missing ACK by timeout.   */

    /* Step 7: return both structs to the pool.
     * This MUST happen regardless of the dispatch outcome.         */
    slap_databank_free(req);
    slap_databank_free(resp);
}

/* ----------------------------------------------------------------
 * PUBLIC API
 * ---------------------------------------------------------------- */

int slap_init(void)
{
    /* 1. Initialise the OS abstraction layer first — all subsequent
     *    OSAL calls depend on this.                                 */
    osal_init();

    /* 2. Initialise the hardware transport layer.                   */
    hal_init();

    /* 3. Initialise the static packet memory pool.                  */
    slap_databank_init();

    /* 4. Create synchronisation objects.                            */
    if (osal_sem_create_binary(&g_rx_sem, 0) != OSAL_OK)
        return SLAP_ERR_INVALID; /* starts locked — ISR will give it */
    
    osal_queue_create(&g_rx_queue, SLAP_CORE_QUEUE_DEPTH, SLAP_MTU);

    if (osal_eventgroup_create(&g_slap_events) != OSAL_OK)
        return SLAP_ERR_INVALID;

    /* 5. Create the two SLAP tasks.
     *    FreeRTOS: these begin executing after vTaskStartScheduler().
     *    Bare-metal: osal_task_create() is a stub that returns OSAL_ERR;
     *    in that case slap_tick() is called manually from main().    */
    osal_task_create(&g_rx_task,
                     "slap_rx",
                     slap_rx_task_fn,
                     NULL,
                     OSAL_STACK_LARGE,    /* ~2 KB — must fit g_rx_wire_buf */
                     OSAL_PRIO_HIGH);

    osal_task_create(&g_sched_task,
                     "slap_sched",
                     slap_sched_task_fn,
                     NULL,
                     OSAL_STACK_SMALL,
                     OSAL_PRIO_LOW);

    /* 6. Initialise the hardware watchdog.
     *    Timeout = 2000 ms. Must be kicked every cycle.
     *    Adjust based on your worst-case loop time after profiling. */
    osal_watchdog_init(2000);

    return SLAP_OK;
}

void slap_tick(void)
{
    /* For bare-metal single-task operation: call this from while(1).
     * It blocks on the semaphore (or polls, depending on osal_baremetal)
     * then runs one complete receive–dispatch–respond cycle.        */
    osal_sem_take(&g_rx_sem, 10); /* 10 ms timeout — then kick WDT  */
    run_one_cycle();
    osal_watchdog_kick();
}

void slap_core_notify_rx(const uint8_t *buf, uint16_t len)
{
    /* Copy the received bytes into the queue.
     * The queue holds up to SLAP_CORE_QUEUE_DEPTH complete packets.
     * If full, the packet is dropped (ground station retries).     */

    /* Pack buf + len into a small envelope */
    typedef struct { uint8_t data[SLAP_MTU]; uint16_t len; } rx_envelope_t;

    rx_envelope_t env;
    if (len > SLAP_MTU) len = SLAP_MTU;
    memcpy(env.data, buf, len);
    env.len = len;

    /* Non-blocking send — called from ISR context */
    osal_queue_send(&g_rx_queue, &env, 0);

    /* Wake the rx task */
    osal_sem_give_from_isr(&g_rx_sem);
}

/* ----------------------------------------------------------------
 * TASK ENTRY POINTS (FreeRTOS)
 * ---------------------------------------------------------------- */

void slap_rx_task_fn(void *arg)
{
    (void)arg;

    /* FreeRTOS task functions must never return.
     * Loop forever: wait for the rx semaphore, then process.       */
    while (1) {
        /* Block here with zero CPU usage until the ISR signals us.
         * 0xFFFFFFFF = wait forever (portMAX_DELAY equivalent).    */
        if (osal_sem_take(&g_rx_sem, 0xFFFFFFFF) == OSAL_OK) {
            run_one_cycle();
        }
        /* Kick the watchdog at the bottom of every cycle.
         * If run_one_cycle() hangs, the watchdog fires and resets. */
        osal_watchdog_kick();
    }
}

void slap_sched_task_fn(void *arg)
{
    (void)arg;

    /* Import the scheduling tick function from service 4.
     * This function checks all scheduled telecommands against the
     * current time and executes any that are due.                  */
    extern void slap_scheduling_tick(uint64_t current_time);

    while (1) {
        /* Block until a 1-Hz timer ISR sets SLAP_EVENT_SCHED_TICK.
         * wait_all=1, clear_on_exit=1 — the bit is cleared after
         * we wake so each tick fires exactly once.                 */
        osal_eventgroup_wait(&g_slap_events,
                             SLAP_EVENT_SCHED_TICK,
                             1,           /* wait_all  */
                             1,           /* clear_on_exit */
                             0xFFFFFFFF); /* wait forever  */

        slap_scheduling_tick(osal_get_time_raw());
        osal_watchdog_kick();
    }
}