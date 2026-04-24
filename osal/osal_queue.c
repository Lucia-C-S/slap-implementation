// osal/osal_queue.c
#include "osal_queue.h"
#include "osal.h"
#include "FreeRTOS.h"
#include "queue.h"

int osal_queue_create(osal_queue_t *q, uint32_t depth, uint32_t item_size)
{
    *q = (osal_queue_t)xQueueCreate((UBaseType_t)depth,
                                    (UBaseType_t)item_size);
    return (*q != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

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

uint32_t osal_queue_count(osal_queue_t *q)
{
    return (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)*q);
}

void osal_queue_destroy(osal_queue_t *q)
{
    vQueueDelete((QueueHandle_t)*q);
    *q = NULL;
}