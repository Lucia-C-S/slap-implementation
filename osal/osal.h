// osal.h
#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>

/* ================================================================
 * OSAL — Operating System Abstraction Layer for SLAP
 *
 * Implement osal.c once per target:
 *   - osal_freertos.c   (for OBC / PAYLOAD running FreeRTOS)
 *   - osal_baremetal.c  (for testing without an RTOS)
 *   - osal_posix.c      (for ground-side simulation / unit tests on Linux)
 * ================================================================ */


/* ----------------------------------------------------------------
 * 1. RETURN TYPES
 * ---------------------------------------------------------------- */
#define OSAL_OK          0
#define OSAL_ERR        -1
#define OSAL_TIMEOUT    -2
#define OSAL_FULL       -3
#define OSAL_EMPTY      -4


/* ----------------------------------------------------------------
 * 2. TIME
 * The most critical piece for SLAP. All timestamps in the protocol
 * use CCSDS CUC 4.2: 4 bytes coarse (seconds since 1958-01-01 TAI)
 * + 3 bytes fine (sub-second). Total = 7 bytes.
 * ---------------------------------------------------------------- */

/**
 * Fill `out[7]` with the current on-board time in CUC 4.2 format.
 * Bytes [0..3] = coarse (big-endian seconds since TAI epoch 1958-01-01).
 * Bytes [4..6] = fine   (big-endian sub-second fraction).
 */
void osal_get_time_cuc(uint8_t out[7]);

/**
 * Get current time as a raw 64-bit value for scheduling comparisons.
 * Upper 32 bits = coarse seconds, lower 24 bits = fine sub-seconds.
 */
uint64_t osal_get_time_raw(void);

/**
 * Delay execution for `ms` milliseconds.
 * On FreeRTOS: vTaskDelay(pdMS_TO_TICKS(ms))
 * On bare-metal: busy-wait using a hardware timer
 */
void osal_delay_ms(uint32_t ms);


/* ----------------------------------------------------------------
 * 3. CRITICAL SECTIONS
 * Used to protect shared state (e.g. databank pool, schedule table)
 * from concurrent access between tasks or ISRs.
 * ---------------------------------------------------------------- */

/**
 * Disable interrupts / suspend scheduler.
 * On FreeRTOS:   taskENTER_CRITICAL()
 * On bare-metal: __disable_irq()  (ARM Cortex-M)
 */
void osal_enter_critical(void);

/**
 * Re-enable interrupts / resume scheduler.
 * On FreeRTOS:   taskEXIT_CRITICAL()
 * On bare-metal: __enable_irq()
 */
void osal_exit_critical(void);


/* ----------------------------------------------------------------
 * 4. MUTEXES
 * Used when two tasks might write to the same resource.
 * Example: housekeeping data buffer written by sensor task,
 *          read by SLAP housekeeping service.
 * ---------------------------------------------------------------- */

typedef void* osal_mutex_t;  /* opaque handle */

/**
 * Create a mutex. Returns OSAL_OK on success.
 * On FreeRTOS: xSemaphoreCreateMutex()
 */
int  osal_mutex_create(osal_mutex_t *mtx);

/**
 * Lock a mutex. Blocks until available or timeout_ms elapses.
 * Pass 0xFFFFFFFF for infinite wait.
 */
int  osal_mutex_lock(osal_mutex_t *mtx, uint32_t timeout_ms);

/** Unlock a mutex. */
int  osal_mutex_unlock(osal_mutex_t *mtx);

/** Destroy a mutex and free its resources. */
void osal_mutex_destroy(osal_mutex_t *mtx);


/* ----------------------------------------------------------------
 * 5. QUEUES (MESSAGE QUEUES)
 * Used to pass packets between tasks without shared memory races.
 * The SLAP main loop can post received packets to a queue;
 * service tasks drain the queue independently.
 *
 * For SLAP this is optional in early versions (single-task loop
 * is simpler), but required if you use FreeRTOS multi-task.
 * ---------------------------------------------------------------- */

typedef void* osal_queue_t;  /* opaque handle */

/**
 * Create a queue.
 * @param depth      max number of items in the queue
 * @param item_size  size of each item in bytes
 */
int  osal_queue_create(osal_queue_t *q, uint32_t depth, uint32_t item_size);

/**
 * Post an item to the queue. Non-blocking.
 * Returns OSAL_FULL if the queue is full.
 */
int  osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms);

/**
 * Receive an item from the queue.
 * Blocks for up to timeout_ms milliseconds.
 * Returns OSAL_EMPTY if nothing arrived in time.
 */
int  osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms);

void osal_queue_destroy(osal_queue_t *q);


/* ----------------------------------------------------------------
 * 6. TASKS (THREADS)
 * Optional for bare-metal (where you have one loop), required
 * for FreeRTOS where the scheduling service tick runs as its
 * own task, and the SLAP receive loop runs as another.
 * ---------------------------------------------------------------- */

typedef void (*osal_task_func_t)(void *arg);
typedef void* osal_task_t;

/**
 * Create and start a task.
 * @param task       output handle
 * @param name       human-readable name (for debugging)
 * @param func       task entry function
 * @param arg        argument passed to func
 * @param stack_size stack size in bytes
 * @param priority   higher = more urgent (FreeRTOS: 1–configMAX_PRIORITIES)
 */
int  osal_task_create(osal_task_t *task,
                      const char  *name,
                      osal_task_func_t func,
                      void        *arg,
                      uint32_t     stack_size,
                      uint32_t     priority);

void osal_task_delete(osal_task_t *task);

/** Yield the CPU to another task of equal or higher priority. */
void osal_task_yield(void);


/* ----------------------------------------------------------------
 * 7. MEMORY (optional, for pools beyond the databank)
 * In embedded systems: NEVER use malloc directly.
 * These wrap a static arena or RTOS heap if available.
 * For SLAP, slap_databank.c handles packet memory, so these
 * are only needed if you add dynamic-length log buffers, etc.
 * ---------------------------------------------------------------- */

void *osal_malloc(uint32_t size);
void  osal_free(void *ptr);


/* ----------------------------------------------------------------
 * 8. WATCHDOG (spacecraft-critical)
 * The OBC watchdog must be kicked regularly. If SLAP's receive
 * task hangs, the watchdog fires and resets the system.
 * ---------------------------------------------------------------- */

/** Initialize the hardware watchdog with a timeout in milliseconds. */
void osal_watchdog_init(uint32_t timeout_ms);

/** Kick (reset) the watchdog — call this in your main loop. */
void osal_watchdog_kick(void);


/* ----------------------------------------------------------------
 * 9. INIT
 * ---------------------------------------------------------------- */

/** Initialize the OSAL layer. Call before anything else. */
void osal_init(void);

#endif /* OSAL_H */