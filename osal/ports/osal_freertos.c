// osal_freertos.c
// Link this file instead of osal_baremetal.c when targeting FreeRTOS
// use if OBC runs FreeRTOS
/* osal/ports/osal_freertos.c
 *
 * OSAL port for FreeRTOS targets.
 *
 * This single file implements everything declared in:
 *   osal.h, osal_task.h, osal_queue.h,
 *   osal_semaphore.h, osal_mutex.h, osal_eventgroup.h
 *
 * It is the ONLY file in the osal/ports/ directory that is compiled
 * for FreeRTOS targets. osal_baremetal.c and osal_posix.c are NOT
 * compiled alongside it — the build system selects one port only.
 *
 * FreeRTOS dependencies (must exist in your project):
 *   FreeRTOS.h, task.h, semphr.h, queue.h, event_groups.h
 *   FreeRTOSConfig.h (your configuration — must define
 *     configTICK_RATE_HZ, configTOTAL_HEAP_SIZE, configMAX_PRIORITIES)
 *
 * Hardware dependencies (fill in once Item 0.1 confirmed):
 *   rtc_get_seconds() — your RTC driver, returns seconds since
 *                       1958-01-01 TAI (CUC epoch).
 *   hiwdg             — STM32 IWDG handle, defined in main.c by HAL.
 */

#include "osal/osal.h"

/* FreeRTOS headers — ONLY included in this file in the entire
 * SLAP stack. Everything else uses osal.h.                        */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"

/* STM32 HAL for watchdog — replace with your MCU vendor header     */
/* Replace placeholder: */
#include "stm32f4xx_hal.h"   /* or stm32h7xx_hal.h — confirm your series */
/* ================================================================
 * HARDWARE DEPENDENCIES
 * Implement these in your BSP (Board Support Package).
 * ================================================================ */

/**
 * Returns elapsed seconds since the CUC epoch (1958-01-01 TAI).
 * Implement in your RTC driver. Two options:
 *
 * Option A — Hardware RTC:
 *   Read your RTC peripheral and add 378,691,200 (the offset between
 *   1958-01-01 TAI and 1970-01-01 Unix epoch) to the Unix timestamp.
 *
 * Option B — Counter from boot:
 *   Maintain a uint32_t incremented by a 1-Hz timer ISR since boot.
 *   At mission start, initialise it to the known launch epoch.
 */
extern uint32_t rtc_get_seconds(void);

/* STM32 IWDG handle — declared in main.c by STM32 HAL / CubeMX   */
extern IWDG_HandleTypeDef hiwdg;

/* ================================================================
 * 1. INITIALISATION
 * ================================================================ */

void osal_init(void)
{
    /* FreeRTOS does not require explicit initialisation before
     * xTaskCreate() — the scheduler handles its own init when
     * vTaskStartScheduler() is called.
     * If your BSP needs any OSAL-level setup, add it here.         */
}

/* ================================================================
 * 2. TIME
 * ================================================================ */

void osal_get_time_cuc(uint8_t out[7])
{
    /* Coarse: seconds since 1958-01-01 TAI, big-endian uint32 */
    uint32_t coarse = rtc_get_seconds();

    /* Fine: sub-second resolution derived from the FreeRTOS tick.
     * Logic: take the tick count modulo ticks-per-second to get
     * the fractional second, then scale to 24-bit binary fraction.
     *
     * Example with configTICK_RATE_HZ = 1000 (1 ms resolution):
     *   tick=1500 → tick%1000 = 500 → 500/1000 × 2^24 ≈ 8388608
     * This gives ~60 ns resolution (1/2^24 seconds ≈ 59.6 ns).    */
    TickType_t tick          = xTaskGetTickCount();
    uint32_t   ticks_per_sec = (uint32_t)configTICK_RATE_HZ;
    uint32_t   fine = (uint32_t)(
        ((uint64_t)(tick % ticks_per_sec) * (1UL << 24))
        / ticks_per_sec
    );

    /* Pack into CUC 4.2 format (big-endian) */
    out[0] = (uint8_t)(coarse >> 24);
    out[1] = (uint8_t)(coarse >> 16);
    out[2] = (uint8_t)(coarse >>  8);
    out[3] = (uint8_t)(coarse);
    out[4] = (uint8_t)(fine >> 16);
    out[5] = (uint8_t)(fine >>  8);
    out[6] = (uint8_t)(fine);
}

uint64_t osal_get_time_raw(void)
{
    /* Returns a 64-bit value encoding both coarse and fine time.
     * Bits [63:24] = coarse seconds, bits [23:0] = fine fraction.
     * Used by the scheduling service to compare uint64_t timestamps
     * without re-parsing the 7-byte CUC array.                    */
    uint32_t coarse = rtc_get_seconds();
    TickType_t tick = xTaskGetTickCount();
    uint32_t fine = (uint32_t)(
        ((uint64_t)(tick % (uint32_t)configTICK_RATE_HZ) * (1UL << 24))
        / (uint32_t)configTICK_RATE_HZ
    );
    return ((uint64_t)coarse << 24) | (fine & 0x00FFFFFFU);
}

void osal_delay_ms(uint32_t ms)
{
    /* vTaskDelay() suspends the calling task for the specified
     * number of ticks. pdMS_TO_TICKS converts ms to ticks using
     * configTICK_RATE_HZ. The CPU is free to run other tasks
     * during the delay — this is NOT a busy-wait.                 */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ================================================================
 * 3. CRITICAL SECTIONS
 * ================================================================ */

void osal_enter_critical(void)
{
    /* taskENTER_CRITICAL() on Cortex-M:
     *   - Raises BASEPRI to mask all interrupts at or below
     *     configMAX_SYSCALL_INTERRUPT_PRIORITY.
     *   - Saves the previous BASEPRI value on the stack.
     *   - Re-entrant: each Enter increments a nesting counter.
     * This is FreeRTOS-managed — safer than raw CPSID/CPSIE
     * because it handles nesting correctly.                        */
    taskENTER_CRITICAL();
}

void osal_exit_critical(void)
{
    taskEXIT_CRITICAL();
}

/* ================================================================
 * 4. WATCHDOG
 * ================================================================ */

void osal_watchdog_init(uint32_t timeout_ms)
{
    /* The IWDG timeout is configured by CubeMX via the prescaler
     * and reload registers. This function starts the IWDG.
     * timeout_ms is informational — the actual timeout is set
     * by CubeMX configuration (hiwdg.Init.Prescaler/Reload).
     *
     * TODO: once Item 0.1 confirmed, verify CubeMX IWDG timeout
     * matches SLAP_CORE_WDT_TIMEOUT_MS.                           */
    (void)timeout_ms;
    HAL_IWDG_Init(&hiwdg);
}

void osal_watchdog_kick(void)
{
    /* Reset the IWDG downcounter. Must be called more frequently
     * than the configured timeout or the MCU resets.              */
    HAL_IWDG_Refresh(&hiwdg);
}

/* ================================================================
 * 5. MEMORY
 * ================================================================ */

void *osal_malloc(uint32_t size)
{
    /* pvPortMalloc is FreeRTOS's heap allocator. It is thread-safe
     * and uses the heap_4.c scheme (best-fit with coalescence).
     * Use ONLY for one-time init allocations (RTOS objects at boot).
     * For packet memory, use slap_databank_alloc() instead.       */
    return pvPortMalloc((size_t)size);
}

void osal_free(void *ptr)
{
    vPortFree(ptr);
}

/* ================================================================
 * 6. TASKS (osal_task.h)
 * ================================================================ */

int osal_task_create(osal_task_t *task, const char *name,
                     osal_task_func_t func, void *arg,
                     uint32_t stack_bytes, uint32_t priority)
{
    /* FreeRTOS stack depth is in WORDS (4 bytes each on Cortex-M).
     * We accept bytes in our API and convert here.                */
    uint32_t stack_words = stack_bytes / sizeof(StackType_t);

    BaseType_t result = xTaskCreate(
        (TaskFunction_t)func,     /* task function pointer         */
        name,                     /* human-readable name (debug)   */
        (uint16_t)stack_words,    /* stack depth in words          */
        arg,                      /* argument passed to func       */
        (UBaseType_t)priority,    /* FreeRTOS priority level       */
        (TaskHandle_t *)task      /* output handle                 */
    );

    return (result == pdPASS) ? OSAL_OK : OSAL_ERR;
}

void osal_task_delete(osal_task_t *task)
{
    vTaskDelete((TaskHandle_t)*task);
    *task = NULL;
}

void osal_task_yield(void)
{
    /* Yield the CPU to a task of equal or higher priority.
     * The calling task re-enters the ready queue immediately.     */
    taskYIELD();
}

void osal_task_suspend(osal_task_t *task)
{
    vTaskSuspend((TaskHandle_t)*task);
}

void osal_task_resume(osal_task_t *task)
{
    vTaskResume((TaskHandle_t)*task);
}

/* ================================================================
 * 7. QUEUES (osal_queue.h)
 * ================================================================ */

int osal_queue_create(osal_queue_t *q, uint32_t depth, uint32_t item_size)
{
    *q = (osal_queue_t)xQueueCreate((UBaseType_t)depth,
                                     (UBaseType_t)item_size);
    return (*q != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFFU)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    return (xQueueSend((QueueHandle_t)*q, item, ticks) == pdTRUE)
           ? OSAL_OK : OSAL_FULL;
}

int osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFFU)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    return (xQueueReceive((QueueHandle_t)*q, item, ticks) == pdTRUE)
           ? OSAL_OK : OSAL_EMPTY;
}

uint32_t osal_queue_count(osal_queue_t *q)
{
    return (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)*q);
}

void osal_queue_destroy(osal_queue_t *q)
{
    vQueueDelete((QueueHandle_t)*q);
    *q = NULL;
}

/* ================================================================
 * 8. SEMAPHORES (osal_semaphore.h)
 * ================================================================ */

int osal_sem_create_binary(osal_sem_t *sem, uint8_t initial_value)
{
    *sem = (osal_sem_t)xSemaphoreCreateBinary();
    if (*sem == NULL) return OSAL_ERR;

    /* xSemaphoreCreateBinary() always starts in the "taken" state.
     * If caller wants it available (initial_value=1), give it now. */
    if (initial_value)
        xSemaphoreGive((SemaphoreHandle_t)*sem);

    return OSAL_OK;
}

int osal_sem_create_counting(osal_sem_t *sem,
                              uint32_t max_count, uint32_t initial_count)
{
    *sem = (osal_sem_t)xSemaphoreCreateCounting(
               (UBaseType_t)max_count,
               (UBaseType_t)initial_count);
    return (*sem != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_sem_take(osal_sem_t *sem, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFFU)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    return (xSemaphoreTake((SemaphoreHandle_t)*sem, ticks) == pdTRUE)
           ? OSAL_OK : OSAL_TIMEOUT;
}

int osal_sem_give(osal_sem_t *sem)
{
    return (xSemaphoreGive((SemaphoreHandle_t)*sem) == pdTRUE)
           ? OSAL_OK : OSAL_ERR;
}

int osal_sem_give_from_isr(osal_sem_t *sem)
{
    BaseType_t higher_prio_woken = pdFALSE;

    BaseType_t r = xSemaphoreGiveFromISR((SemaphoreHandle_t)*sem,
                                          &higher_prio_woken);

    /* If a higher-priority task was waiting on this semaphore,
     * yield immediately so it runs before the ISR returns.
     * Without this yield, the high-priority task would only run
     * after the current task's next scheduling slot — adding
     * up to one full tick of latency (typically 1 ms).           */
    portYIELD_FROM_ISR(higher_prio_woken);

    return (r == pdTRUE) ? OSAL_OK : OSAL_ERR;
}

void osal_sem_destroy(osal_sem_t *sem)
{
    vSemaphoreDelete((SemaphoreHandle_t)*sem);
    *sem = NULL;
}

/* ================================================================
 * 9. MUTEXES (osal_mutex.h)
 * ================================================================ */

int osal_mutex_create(osal_mutex_t *mtx)
{
    /* xSemaphoreCreateMutex() creates a mutex WITH priority
     * inheritance. This means if a low-priority task holds the
     * mutex, and a high-priority task tries to acquire it, the
     * low-priority task temporarily inherits the high priority
     * so it finishes and releases the mutex quickly.
     * This prevents priority inversion — critical in RTOS systems. */
    *mtx = (osal_mutex_t)xSemaphoreCreateMutex();
    return (*mtx != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_mutex_lock(osal_mutex_t *mtx, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFFU)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    return (xSemaphoreTake((SemaphoreHandle_t)*mtx, ticks) == pdTRUE)
           ? OSAL_OK : OSAL_TIMEOUT;
}

int osal_mutex_unlock(osal_mutex_t *mtx)
{
    return (xSemaphoreGive((SemaphoreHandle_t)*mtx) == pdTRUE)
           ? OSAL_OK : OSAL_ERR;
}

void osal_mutex_destroy(osal_mutex_t *mtx)
{
    vSemaphoreDelete((SemaphoreHandle_t)*mtx);
    *mtx = NULL;
}

/* ================================================================
 * 10. EVENT GROUPS (osal_eventgroup.h)
 * ================================================================ */

int osal_eventgroup_create(osal_eventgroup_t *eg)
{
    *eg = (osal_eventgroup_t)xEventGroupCreate();
    return (*eg != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_eventgroup_set(osal_eventgroup_t *eg, osal_event_bits_t bits)
{
    /* xEventGroupSetBits() sets bits and unblocks any tasks that
     * were waiting for those bits. All bits remain set until
     * explicitly cleared or consumed with clear_on_exit=1.        */
    xEventGroupSetBits((EventGroupHandle_t)*eg, (EventBits_t)bits);
    return OSAL_OK;
}

int osal_eventgroup_set_from_isr(osal_eventgroup_t *eg,
                                  osal_event_bits_t bits)
{
    BaseType_t higher_prio_woken = pdFALSE;

    /* xEventGroupSetBitsFromISR() defers the actual bit-setting to
     * the FreeRTOS timer daemon task to avoid executing event group
     * logic inside the ISR. This requires configUSE_TIMERS=1 and
     * the timer task to have sufficient priority.                  */
    xEventGroupSetBitsFromISR((EventGroupHandle_t)*eg,
                               (EventBits_t)bits,
                               &higher_prio_woken);

    portYIELD_FROM_ISR(higher_prio_woken);
    return OSAL_OK;
}

int osal_eventgroup_clear(osal_eventgroup_t *eg, osal_event_bits_t bits)
{
    xEventGroupClearBits((EventGroupHandle_t)*eg, (EventBits_t)bits);
    return OSAL_OK;
}

osal_event_bits_t osal_eventgroup_wait(osal_eventgroup_t *eg,
                                        osal_event_bits_t bits,
                                        uint8_t wait_all,
                                        uint8_t clear_on_exit,
                                        uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFFU)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    /* xEventGroupWaitBits() blocks until:
     *   - wait_all=1: ALL specified bits are set simultaneously, OR
     *   - wait_all=0: ANY one of the specified bits is set.
     * If clear_on_exit=1, the matched bits are cleared atomically
     * when the function returns — the task consumes the event.    */
    return (osal_event_bits_t)xEventGroupWaitBits(
        (EventGroupHandle_t)*eg,
        (EventBits_t)bits,
        (BaseType_t)clear_on_exit,   /* clear on exit   */
        (BaseType_t)wait_all,        /* wait for all    */
        ticks
    );
}

void osal_eventgroup_destroy(osal_eventgroup_t *eg)
{
    vEventGroupDelete((EventGroupHandle_t)*eg);
    *eg = NULL;
}