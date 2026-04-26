// osal/osal_mutex.c
#include "osal_mutex.h"
#include "osal.h"
#include "FreeRTOS.h"
#include "semphr.h"

int osal_mutex_create(osal_mutex_t *mtx)
{
    // xSemaphoreCreateMutex includes priority inheritance
    *mtx = (osal_mutex_t)xSemaphoreCreateMutex(); // use xSemaphoreCreateMutex(), not xSemaphoreCreateBinary(). The difference is priority inheritance — a mutex automatically elevates the priority of the task holding it when a higher-priority task is waiting. This prevents priority inversion, a class of real-time scheduling failure that has caused spacecraft anomalies
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
    *mtx = NULL;
}