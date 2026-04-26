// osal_baremetal.c
// For targets without FreeRTOS: ARM Cortex-M bare-metal
// Replace HAL_xxx calls with your actual MCU HAL (STM32, etc.)
// use for the actual flight target
// Your build system (Makefile or CMake) then selects which osal_xxx.c to compile. 

/* osal/ports/osal_baremetal.c
 *
 * OSAL port for bare-metal targets (no RTOS).
 * Suitable for simple MCUs running a superloop (while(1) in main).
 *
 * GAPS REQUIRING YOUR HARDWARE-SPECIFIC INPUT (marked TODO):
 *   A. Timer ISR for g_rtc_coarse / g_rtc_fine (Item 2.7A)
 *   B. osal_delay_ms() — needs SysTick or hardware timer
 *   C. osal_enter_critical() — ARM Cortex-M assembly shown;
 *      replace for non-ARM targets
 *   D. osal_watchdog_init/kick — your MCU's IWDG peripheral
 *
 * All FreeRTOS primitives (tasks, queues, semaphores, mutexes,
 * event groups) are STUB IMPLEMENTATIONS:
 *   - Tasks: osal_task_create() is a no-op; use the slap_tick()
 *     superloop instead (see bare-metal main.c)
 *   - Queues: depth-1 single-item buffer; no blocking
 *   - Semaphores/Mutexes: critical-section based
 *   - Event groups: polling flag word, no blocking
 *
 * These stubs are intentionally minimal because bare-metal SLAP
 * runs in a single execution context. For concurrency, use FreeRTOS.
 */

#include "osal/osal.h"
#include <string.h>

/* ================================================================
 * HARDWARE TIME COUNTERS
 *
 * These two variables must be maintained by a hardware timer ISR.
 *
 * TODO (Item 2.7A) — IMPLEMENT THE ISR:
 *
 * The coarse counter increments every second (1 Hz).
 * The fine counter increments at a sub-second rate and rolls over
 * every second. Set OSAL_FINE_TICKS_PER_SEC to your timer frequency.
 *
 * Example for STM32 with TIM2 at 1 kHz (1 ms resolution):
 *
 *   #define OSAL_FINE_TICKS_PER_SEC  1000U
 *   volatile uint32_t g_rtc_coarse = 0;
 *   volatile uint32_t g_rtc_fine   = 0;
 *
 *   void TIM2_IRQHandler(void)
 *   {
 *       __HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE);
 *       g_rtc_fine++;
 *       if (g_rtc_fine >= OSAL_FINE_TICKS_PER_SEC) {
 *           g_rtc_fine = 0;
 *           g_rtc_coarse++;      // one second elapsed
 *       }
 *   }
 *
 * Initialise g_rtc_coarse at boot to your mission epoch offset if
 * you have an RTC: g_rtc_coarse = rtc_read() + TAI_EPOCH_OFFSET;
 * ================================================================ */

#define OSAL_FINE_TICKS_PER_SEC  1000U   /* TODO: set to your timer freq */
#define TAI_EPOCH_OFFSET_SECS    378691200UL

/* TODO: make these volatile and populate via ISR as described above */
volatile uint32_t g_rtc_coarse = 0;
volatile uint32_t g_rtc_fine   = 0;

/* ================================================================
 * 1. INITIALISATION
 * ================================================================ */

void osal_init(void)
{
    /* TODO: start your hardware timer ISR here if not done in board_init()
     * Example STM32: HAL_TIM_Base_Start_IT(&htim2);
     *
     * If you have a hardware RTC, read it here and initialise
     * g_rtc_coarse with seconds since 1958-01-01 TAI:
     *   g_rtc_coarse = HAL_RTC_GetUnixTime() + TAI_EPOCH_OFFSET_SECS;
     */
}

/* ================================================================
 * 2. TIME
 * ================================================================ */

void osal_get_time_cuc(uint8_t out[7])
{
    /* Atomic read: disable interrupts briefly to prevent the ISR
     * from modifying g_rtc_coarse between our two reads.         */
    osal_enter_critical();
    uint32_t coarse = g_rtc_coarse;
    uint32_t fine_ticks = g_rtc_fine;
    osal_exit_critical();

    /* Scale fine_ticks to 24-bit binary fraction of a second:
     * fine = (fine_ticks / FINE_TICKS_PER_SEC) × 2^24
     * Using integer arithmetic to avoid float on bare-metal:     */
    uint32_t fine = (uint32_t)(
        ((uint64_t)fine_ticks * (1UL << 24)) / OSAL_FINE_TICKS_PER_SEC
    );

    out[0] = (uint8_t)(coarse >> 24);
    out[1] = (uint8_t)(coarse >> 16);
    out[2] = (uint8_t)(coarse >>  8);
    out[3] = (uint8_t)(coarse);
    out[4] = (uint8_t)(fine   >> 16);
    out[5] = (uint8_t)(fine   >>  8);
    out[6] = (uint8_t)(fine);
}

uint64_t osal_get_time_raw(void)
{
    osal_enter_critical();
    uint32_t c = g_rtc_coarse;
    uint32_t f_ticks = g_rtc_fine;
    osal_exit_critical();
    uint32_t fine = (uint32_t)(
        ((uint64_t)f_ticks * (1UL << 24)) / OSAL_FINE_TICKS_PER_SEC
    );
    return ((uint64_t)c << 24) | (fine & 0x00FFFFFFU);
}

void osal_delay_ms(uint32_t ms)
{
    /* TODO: replace with a real SysTick-based delay once Item 0.1
     * confirms your MCU. This busy-wait is clock-speed dependent
     * and inaccurate.
     *
     * Preferred implementation for STM32 HAL:
     *   HAL_Delay(ms);
     *
     * For ARM Cortex-M without HAL, use DWT cycle counter:
     *   uint32_t start = DWT->CYCCNT;
     *   uint32_t ticks = ms * (SystemCoreClock / 1000U);
     *   while ((DWT->CYCCNT - start) < ticks) {}
     */
    volatile uint32_t count = ms * 8000U; /* rough for ~8 MHz — REPLACE */
    while (count--) { __asm volatile ("nop"); }
}

/* ================================================================
 * 3. CRITICAL SECTIONS — ARM Cortex-M
 *
 * Uses PRIMASK register to save/restore interrupt enable state.
 * This correctly handles nested critical sections: each enter
 * saves the current state and disables; each exit restores it.
 *
 * TODO: for non-ARM targets, replace the inline assembly.
 * AVR:   cli() / sei()
 * RISC-V: csrci mstatus, 0x8 / csrsi mstatus, 0x8
 * ================================================================ */

void osal_enter_critical(void)
{
#if defined(__ARM_ARCH)
    __asm volatile (
        "MRS  R0, PRIMASK  \n" /* save current interrupt state to R0 */
        "CPSID I           \n" /* disable all interrupts (set PRIMASK) */
        "PUSH {R0}         \n" /* push saved state onto the stack     */
        ::: "r0", "memory"
    );
#else
    /* TODO: add your architecture's interrupt disable here */
#endif
}

void osal_exit_critical(void)
{
#if defined(__ARM_ARCH)
    __asm volatile (
        "POP  {R0}         \n" /* restore saved state from stack */
        "MSR  PRIMASK, R0  \n" /* restore previous interrupt state */
        ::: "r0", "memory"
    );
#else
    /* TODO: add your architecture's interrupt enable here */
#endif
}

/* ================================================================
 * 4. WATCHDOG
 *
 * TODO (Item 2.7D): fill in your MCU's IWDG peripheral calls.
 *
 * STM32 HAL example:
 *   extern IWDG_HandleTypeDef hiwdg;
 *   void osal_watchdog_init(uint32_t t) { (void)t; HAL_IWDG_Init(&hiwdg); }
 *   void osal_watchdog_kick(void)       { HAL_IWDG_Refresh(&hiwdg); }
 *
 * Direct IWDG register access (no HAL):
 *   void osal_watchdog_init(uint32_t t) {
 *       (void)t;
 *       IWDG->KR  = 0x5555;   // enable register write access
 *       IWDG->PR  = 0x04;     // prescaler /64 → 500 Hz from LSI 32kHz
 *       IWDG->RLR = 0x3E7;    // reload = 999 → ~2.0 s timeout
 *       IWDG->KR  = 0xCCCC;   // start the IWDG
 *   }
 *   void osal_watchdog_kick(void) { IWDG->KR = 0xAAAA; }
 * ================================================================ */

void osal_watchdog_init(uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* TODO: initialise hardware watchdog */
}

void osal_watchdog_kick(void)
{
    /* TODO: reload hardware watchdog counter */
}

/* ================================================================
 * 5. MEMORY
 * Bump allocator: monotonically advances a pointer through a
 * static arena. Supports alloc but not free — intentional.
 * Use ONLY for one-time init (creating semaphore/mutex objects
 * at boot). Packet memory uses slap_databank.c.
 * ================================================================ */

#define OSAL_HEAP_SIZE  2048U
static uint8_t  g_heap[OSAL_HEAP_SIZE];
static uint32_t g_heap_top = 0;

void *osal_malloc(uint32_t size)
{
    /* Align to 4-byte boundary */
    uint32_t aligned = (size + 3U) & ~3U;
    if (g_heap_top + aligned > OSAL_HEAP_SIZE)
        return NULL; /* out of static arena */
    void *p = &g_heap[g_heap_top];
    g_heap_top += aligned;
    return p;
}

void osal_free(void *ptr)
{
    /* Intentional no-op: bump allocator has no free.
     * Memory is only ever allocated at boot-time init and
     * lives for the entire program lifetime.               */
    (void)ptr;
}

/* ================================================================
 * 6. TASKS — stubs (bare-metal uses superloop, not tasks)
 * ================================================================ */

int osal_task_create(osal_task_t *task, const char *name,
                     osal_task_func_t func, void *arg,
                     uint32_t stack_bytes, uint32_t priority)
{
    /* On bare-metal there is no scheduler.
     * The caller (slap_core.c) checks the return code and falls
     * back to the slap_tick() superloop when OSAL_ERR is returned.*/
    (void)task; (void)name; (void)func;
    (void)arg; (void)stack_bytes; (void)priority;
    return OSAL_ERR;
}

void osal_task_delete(osal_task_t *t)  { (void)t; }
void osal_task_yield(void)             {}
void osal_task_suspend(osal_task_t *t) { (void)t; }
void osal_task_resume(osal_task_t *t)  { (void)t; }

/* ================================================================
 * 7. QUEUES — single-item non-blocking buffer
 * Sufficient for bare-metal superloop where send and receive
 * happen in the same execution context.
 * ================================================================ */

typedef struct {
    uint8_t  buf[2050]; /* max item size: MTU + 2 bytes length */
    uint32_t item_size;
    uint8_t  full;
} baremetal_queue_t;

static baremetal_queue_t g_queue_pool[4]; /* max 4 queues in SLAP */
static uint8_t g_queue_used[4] = {0};

int osal_queue_create(osal_queue_t *q, uint32_t depth, uint32_t item_size)
{
    (void)depth; /* single-item: depth is always 1 on bare-metal */
    for (int i = 0; i < 4; i++) {
        if (!g_queue_used[i]) {
            g_queue_used[i] = 1;
            g_queue_pool[i].item_size = item_size;
            g_queue_pool[i].full = 0;
            *q = (osal_queue_t)&g_queue_pool[i];
            return OSAL_OK;
        }
    }
    return OSAL_ERR;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms)
{
    baremetal_queue_t *bq = (baremetal_queue_t *)*q;
    (void)timeout_ms;
    if (bq->full) return OSAL_FULL;
    memcpy(bq->buf, item, bq->item_size);
    bq->full = 1;
    return OSAL_OK;
}

int osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms)
{
    baremetal_queue_t *bq = (baremetal_queue_t *)*q;
    (void)timeout_ms;
    if (!bq->full) return OSAL_EMPTY;
    memcpy(item, bq->buf, bq->item_size);
    bq->full = 0;
    return OSAL_OK;
}

uint32_t osal_queue_count(osal_queue_t *q)
{
    return ((baremetal_queue_t *)*q)->full ? 1U : 0U;
}

void osal_queue_destroy(osal_queue_t *q)
{
    for (int i = 0; i < 4; i++) {
        if ((osal_queue_t)&g_queue_pool[i] == *q) {
            g_queue_used[i] = 0;
            break;
        }
    }
    *q = NULL;
}

/* ================================================================
 * 8. SEMAPHORES — critical-section backed flag
 * ================================================================ */

typedef struct { uint32_t count; uint32_t max; } baremetal_sem_t;
static baremetal_sem_t g_sem_pool[8];
static uint8_t         g_sem_used[8] = {0};

static int sem_alloc(osal_sem_t *s, uint32_t max, uint32_t init)
{
    for (int i = 0; i < 8; i++) {
        if (!g_sem_used[i]) {
            g_sem_used[i]   = 1;
            g_sem_pool[i].count = init;
            g_sem_pool[i].max   = max;
            *s = (osal_sem_t)&g_sem_pool[i];
            return OSAL_OK;
        }
    }
    return OSAL_ERR;
}

int osal_sem_create_binary(osal_sem_t *s, uint8_t init)
{
    return sem_alloc(s, 1, (uint32_t)init);
}

int osal_sem_create_counting(osal_sem_t *s, uint32_t max, uint32_t init)
{
    return sem_alloc(s, max, init);
}

int osal_sem_take(osal_sem_t *s, uint32_t timeout_ms)
{
    (void)timeout_ms;
    baremetal_sem_t *bs = (baremetal_sem_t *)*s;
    osal_enter_critical();
    int r = OSAL_TIMEOUT;
    if (bs->count > 0) { bs->count--; r = OSAL_OK; }
    osal_exit_critical();
    return r;
}

int osal_sem_give(osal_sem_t *s)
{
    baremetal_sem_t *bs = (baremetal_sem_t *)*s;
    osal_enter_critical();
    if (bs->count < bs->max) bs->count++;
    osal_exit_critical();
    return OSAL_OK;
}

int osal_sem_give_from_isr(osal_sem_t *s) { return osal_sem_give(s); }

void osal_sem_destroy(osal_sem_t *s)
{
    for (int i = 0; i < 8; i++) {
        if ((osal_sem_t)&g_sem_pool[i] == *s)
            { g_sem_used[i] = 0; break; }
    }
    *s = NULL;
}

/* ================================================================
 * 9. MUTEXES — critical-section backed on bare-metal
 * ================================================================ */

typedef struct { uint8_t locked; } baremetal_mutex_t;
static baremetal_mutex_t g_mtx_pool[8];
static uint8_t           g_mtx_used[8] = {0};

int osal_mutex_create(osal_mutex_t *m)
{
    for (int i = 0; i < 8; i++) {
        if (!g_mtx_used[i]) {
            g_mtx_used[i]      = 1;
            g_mtx_pool[i].locked = 0;
            *m = (osal_mutex_t)&g_mtx_pool[i];
            return OSAL_OK;
        }
    }
    return OSAL_ERR;
}

int osal_mutex_lock(osal_mutex_t *m, uint32_t timeout_ms)
{
    (void)timeout_ms;
    osal_enter_critical();
    ((baremetal_mutex_t *)*m)->locked = 1;
    /* Critical section remains active — exit is in unlock.
     * This means the entire locked region is interrupt-free. */
    return OSAL_OK;
}

int osal_mutex_unlock(osal_mutex_t *m)
{
    ((baremetal_mutex_t *)*m)->locked = 0;
    osal_exit_critical();
    return OSAL_OK;
}

void osal_mutex_destroy(osal_mutex_t *m)
{
    for (int i = 0; i < 8; i++) {
        if ((osal_mutex_t)&g_mtx_pool[i] == *m)
            { g_mtx_used[i] = 0; break; }
    }
    *m = NULL;
}

/* ================================================================
 * 10. EVENT GROUPS — polling flag word (no blocking)
 * ================================================================ */

typedef struct { volatile uint32_t bits; } baremetal_eg_t;
static baremetal_eg_t g_eg_pool[4];
static uint8_t        g_eg_used[4] = {0};

int osal_eventgroup_create(osal_eventgroup_t *eg)
{
    for (int i = 0; i < 4; i++) {
        if (!g_eg_used[i]) {
            g_eg_used[i]      = 1;
            g_eg_pool[i].bits = 0;
            *eg = (osal_eventgroup_t)&g_eg_pool[i];
            return OSAL_OK;
        }
    }
    return OSAL_ERR;
}

int osal_eventgroup_set(osal_eventgroup_t *eg, osal_event_bits_t b)
{
    osal_enter_critical();
    ((baremetal_eg_t *)*eg)->bits |= b;
    osal_exit_critical();
    return OSAL_OK;
}

int osal_eventgroup_set_from_isr(osal_eventgroup_t *eg,
                                  osal_event_bits_t b)
{
    ((baremetal_eg_t *)*eg)->bits |= b; /* already in ISR — no lock */
    return OSAL_OK;
}

int osal_eventgroup_clear(osal_eventgroup_t *eg, osal_event_bits_t b)
{
    osal_enter_critical();
    ((baremetal_eg_t *)*eg)->bits &= ~b;
    osal_exit_critical();
    return OSAL_OK;
}

osal_event_bits_t osal_eventgroup_wait(osal_eventgroup_t *eg,
                                        osal_event_bits_t bits,
                                        uint8_t wait_all,
                                        uint8_t clear_on_exit,
                                        uint32_t timeout_ms)
{
    /* Bare-metal: non-blocking poll only (timeout_ms ignored).
     * The superloop calls this; if the bits are not set the loop
     * will call it again on the next iteration.                  */
    (void)timeout_ms;
    baremetal_eg_t *e = (baremetal_eg_t *)*eg;
    osal_event_bits_t current = e->bits;
    int satisfied = wait_all
                  ? ((current & bits) == bits)
                  : ((current & bits) != 0);
    if (satisfied && clear_on_exit) {
        osal_enter_critical();
        e->bits &= ~bits;
        osal_exit_critical();
    }
    return satisfied ? current : 0;
}

void osal_eventgroup_destroy(osal_eventgroup_t *eg)
{
    for (int i = 0; i < 4; i++) {
        if ((osal_eventgroup_t)&g_eg_pool[i] == *eg)
            { g_eg_used[i] = 0; break; }
    }
    *eg = NULL;
}