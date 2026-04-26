/* slap_core.h
 *
 * Public API for the SLAP protocol core.
 *
 * This is the only SLAP header your application code (main.c, BSP)
 * needs to include. Internal modules (#include their own headers).
 *
 * Initialisation call sequence (performed inside slap_init()):
 *
 *   1. osal_init()
 *        └─ Initialises OSAL primitives and the port layer.
 *           On FreeRTOS: no-op (scheduler not started yet).
 *           On bare-metal: initialises hardware timer for time.
 *
 *   2. hal_init()
 *        └─ Initialises the physical transport (UART DMA, CSP, SPI).
 *           Arms the first DMA reception so the ISR fires when
 *           bytes arrive.
 *
 *   3. slap_databank_init()
 *        └─ Initialises the static packet pool and its mutex.
 *           After this call, slap_databank_alloc() is safe to use.
 *
 *   4. OSAL object creation
 *        └─ Creates g_rx_sem (binary semaphore, starts locked),
 *           g_rx_queue (holds up to SLAP_CORE_QUEUE_DEPTH packets),
 *           g_slap_events (event group, all bits clear).
 *
 *   5. Task creation (FreeRTOS only)
 *        └─ Creates slap_rx_task (high priority, OSAL_STACK_LARGE).
 *           Creates slap_sched_task (low priority, OSAL_STACK_SMALL).
 *           Tasks are created but blocked — they run after
 *           vTaskStartScheduler() is called in main().
 *
 *   6. osal_watchdog_init(2000)
 *        └─ Arms the hardware IWDG with a 2-second timeout.
 *           After this point, osal_watchdog_kick() must be called
 *           at least once every 2 seconds or the MCU resets.
 */

#ifndef SLAP_CORE_H
#define SLAP_CORE_H

#include <stdint.h>
#include "slap_types.h"

/* ----------------------------------------------------------------
 * CONFIGURATION
 * ---------------------------------------------------------------- */

/**
 * Maximum number of received packets that can be queued between
 * the HAL ISR and the dispatch task.
 * Must be ≥ SLAP_POOL_SIZE (defined in slap_databank.h).
 * Increasing this costs (SLAP_CORE_QUEUE_DEPTH × SLAP_MTU) bytes
 * of static RAM.
 */
#define SLAP_CORE_QUEUE_DEPTH  4U

/**
 * Watchdog timeout in milliseconds.
 * Must be > worst-case slap_tick() execution time × 2.
 * Measure your worst-case time during testing and set this
 * accordingly. 2000 ms is a conservative starting value.
 */
#define SLAP_CORE_WDT_TIMEOUT_MS  2000U

/* ----------------------------------------------------------------
 * INITIALISATION
 * ---------------------------------------------------------------- */

/**
 * Initialise the entire SLAP stack.
 *
 * Call this ONCE from main(), before vTaskStartScheduler() (FreeRTOS)
 * or before the bare-metal superloop begins.
 *
 * All hardware peripherals (clocks, UART, RTC) must be initialised
 * by board_init() BEFORE calling slap_init().
 *
 * @return SLAP_OK on success.
 *         SLAP_ERR_INVALID if any sub-system (OSAL, HAL, DataBank,
 *         or RTOS object creation) fails.
 */
int slap_init(void);

/* ----------------------------------------------------------------
 * BARE-METAL OPERATION
 * Call slap_tick() from your while(1) superloop.
 * Not needed when using FreeRTOS — the tasks handle this internally.
 * ---------------------------------------------------------------- */

/**
 * Execute one receive–decode–dispatch–encode–send cycle.
 *
 * Non-blocking: if no packet is available, returns immediately.
 * Kicks the hardware watchdog before returning.
 *
 * Bare-metal: call this from while(1) in main().
 * FreeRTOS:   do NOT call this — the rx task calls it internally.
 */
void slap_tick(void);

/* ----------------------------------------------------------------
 * HAL INTEGRATION
 * Call slap_core_notify_rx() from your HAL receive-complete ISR.
 * ---------------------------------------------------------------- */

/**
 * Signal that raw bytes have arrived on the physical transport.
 *
 * This function is ISR-safe. It copies the received bytes into the
 * internal queue and wakes the receive task (FreeRTOS) or sets a
 * flag (bare-metal).
 *
 * @param buf  pointer to the buffer containing the received bytes
 *             (the HAL's DMA buffer — must remain valid until this
 *              function returns, then the HAL may re-arm the DMA)
 * @param len  number of bytes in buf (must be ≤ SLAP_MTU)
 */
void slap_core_notify_rx(const uint8_t *buf, uint16_t len);

/* ----------------------------------------------------------------
 * TASK ENTRY POINTS (FreeRTOS only)
 * These are passed to osal_task_create() inside slap_init().
 * Do NOT call them directly from application code.
 * ---------------------------------------------------------------- */

/**
 * Receive and dispatch loop task.
 * Blocks on g_rx_sem until slap_core_notify_rx() wakes it.
 * Runs at OSAL_PRIO_HIGH.
 * @param arg unused — always NULL
 */
void slap_rx_task_fn(void *arg);

/**
 * Scheduling service 1-Hz tick task.
 * Blocks on SLAP_EVENT_SCHED_TICK in the event group.
 * Runs at OSAL_PRIO_LOW.
 * @param arg unused — always NULL
 */
void slap_sched_task_fn(void *arg);

#endif /* SLAP_CORE_H */