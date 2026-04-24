// osal/osal_semaphore.c
#include "osal_semaphore.h"
#include "osal.h"
#include "FreeRTOS.h"
#include "semphr.h"

int osal_sem_create_binary(osal_sem_t *sem, uint8_t initial_value)
{
    *sem = (osal_sem_t)xSemaphoreCreateBinary();
    if (*sem == NULL) return OSAL_ERR;
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
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF)
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
    portYIELD_FROM_ISR(higher_prio_woken);
    return (r == pdTRUE) ? OSAL_OK : OSAL_ERR;
}

void osal_sem_destroy(osal_sem_t *sem)
{
    vSemaphoreDelete((SemaphoreHandle_t)*sem);
    *sem = NULL;
}