// osal_freertos.c
// Link this file instead of osal_baremetal.c when targeting FreeRTOS
// use if OBC runs FreeRTOS
#include "osal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/* ---- TIME ---- */
/* FreeRTOS has no RTC built-in. You still need a hardware RTC.
 * xTaskGetTickCount() gives you RTOS ticks, not wall-clock time.
 * Use it only for the fine part; coarse must come from hardware RTC. */

extern uint32_t rtc_get_seconds_since_1958(void); /* your RTC driver */

void osal_get_time_cuc(uint8_t out[7])
{
    uint32_t coarse = rtc_get_seconds_since_1958();
    /* Fine: convert RTOS ticks within current second to 24-bit fraction */
    uint32_t ticks_per_sec = configTICK_RATE_HZ;
    uint32_t tick_now      = (uint32_t)xTaskGetTickCount();
    uint32_t fine = ((tick_now % ticks_per_sec) * (1UL << 24)) / ticks_per_sec;

    out[0] = (uint8_t)(coarse >> 24);
    out[1] = (uint8_t)(coarse >> 16);
    out[2] = (uint8_t)(coarse >> 8);
    out[3] = (uint8_t)(coarse);
    out[4] = (uint8_t)(fine >> 16);
    out[5] = (uint8_t)(fine >> 8);
    out[6] = (uint8_t)(fine);
}

uint64_t osal_get_time_raw(void)
{
    uint32_t c = rtc_get_seconds_since_1958();
    uint32_t tps = configTICK_RATE_HZ;
    uint32_t tick = (uint32_t)xTaskGetTickCount();
    uint32_t f = ((tick % tps) * (1UL << 24)) / tps;
    return ((uint64_t)c << 24) | (f & 0xFFFFFF);
}

void osal_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ---- CRITICAL SECTIONS ---- */
void osal_enter_critical(void) { taskENTER_CRITICAL(); }
void osal_exit_critical(void)  { taskEXIT_CRITICAL(); }

/* ---- MUTEXES ---- */
int osal_mutex_create(osal_mutex_t *mtx)
{
    *mtx = (osal_mutex_t)xSemaphoreCreateMutex();
    return (*mtx != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_mutex_lock(osal_mutex_t *mtx, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF)
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
}

/* ---- QUEUES ---- */
int osal_queue_create(osal_queue_t *q, uint32_t depth, uint32_t item_size)
{
    *q = (osal_queue_t)xQueueCreate(depth, item_size);
    return (*q != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return (xQueueSend((QueueHandle_t)*q, item, ticks) == pdTRUE)
           ? OSAL_OK : OSAL_FULL;
}

int osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);
    return (xQueueReceive((QueueHandle_t)*q, item, ticks) == pdTRUE)
           ? OSAL_OK : OSAL_EMPTY;
}

void osal_queue_destroy(osal_queue_t *q)
{
    vQueueDelete((QueueHandle_t)*q);
}

/* ---- TASKS ---- */
int osal_task_create(osal_task_t *task, const char *name,
                     osal_task_func_t func, void *arg,
                     uint32_t stack_size, uint32_t priority)
{
    /* FreeRTOS stack size is in words, not bytes */
    BaseType_t r = xTaskCreate((TaskFunction_t)func, name,
                               stack_size / sizeof(StackType_t),
                               arg, priority,
                               (TaskHandle_t *)task);
    return (r == pdPASS) ? OSAL_OK : OSAL_ERR;
}

void osal_task_delete(osal_task_t *task)
{
    vTaskDelete((TaskHandle_t)*task);
}

void osal_task_yield(void) { taskYIELD(); }

/* ---- MEMORY ---- */
void *osal_malloc(uint32_t size) { return pvPortMalloc(size); }
void  osal_free(void *ptr)       { vPortFree(ptr); }

/* ---- WATCHDOG ---- */
void osal_watchdog_init(uint32_t timeout_ms) { (void)timeout_ms; /* your HW watchdog */ }
void osal_watchdog_kick(void)                { /* IWDG_Reload(); */ }

/* ---- INIT ---- */
void osal_init(void) { /* FreeRTOS is already running when osal_init() is called */ }