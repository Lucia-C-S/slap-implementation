/* slap_core.h
 *
 * Public API for the SLAP protocol core loop.
 * This is the only header that your application's main.c needs
 * to include. Everything else is an internal implementation detail.
 */

#ifndef SLAP_CORE_H
#define SLAP_CORE_H

#include "slap_types.h"
#include "osal/osal.h"

/* ----------------------------------------------------------------
 * CONFIGURATION — tune these to your mission's requirements
 * ---------------------------------------------------------------- */

/* Size of the raw-bytes receive queue (number of MTU-sized slots).
 * Must be ≥ SLAP_POOL_SIZE to avoid the queue filling before the
 * DataBank pool runs out.                                          */
#define SLAP_CORE_QUEUE_DEPTH   4

/* ----------------------------------------------------------------
 * PUBLIC API
 * ---------------------------------------------------------------- */

/**
 * Initialise the entire SLAP stack.
 *
 * Call sequence internally:
 *   1. osal_init()
 *   2. hal_init()
 *   3. slap_databank_init()
 *   4. Create OSAL objects (queue, semaphore, event group)
 *   5. Create the two SLAP tasks (FreeRTOS) or return (bare-metal)
 *   6. osal_watchdog_init()
 *
 * Must be called once before vTaskStartScheduler() (FreeRTOS) or
 * before the bare-metal superloop begins.
 *
 * @return SLAP_OK on success, SLAP_ERR_INVALID if any sub-system fails.
 */
int slap_init(void);

/**
 * Process one receive–dispatch–respond cycle.
 *
 * Bare-metal: call from while(1) in main().
 * FreeRTOS:   called internally by slap_rx_task_fn() — do not call
 *             this directly from application code.
 */
void slap_tick(void);

/**
 * Signal to slap_core that raw bytes have been received by the HAL.
 *
 * Call this from your HAL receive-complete ISR or DMA callback.
 * It sets SLAP_EVENT_RX_READY in the event group so the receive
 * task wakes without polling.
 *
 * This function is ISR-safe (uses osal_sem_give_from_isr internally).
 *
 * @param buf pointer to the received byte buffer
 * @param len number of bytes received
 */
void slap_core_notify_rx(const uint8_t *buf, uint16_t len);

/* ----------------------------------------------------------------
 * TASK ENTRY POINTS (FreeRTOS only)
 * Pass these to osal_task_create() — do not call directly.
 * ---------------------------------------------------------------- */

/** Receive and dispatch loop. High priority. */
void slap_rx_task_fn(void *arg);

/** Scheduling service 1-Hz tick. Low priority. */
void slap_sched_task_fn(void *arg);

#endif /* SLAP_CORE_H */