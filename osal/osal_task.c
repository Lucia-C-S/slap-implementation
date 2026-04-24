// osal/osal_task.c  — FreeRTOS implementation
#include "osal_task.h"
#include "ports\osal_freertos.h"
#include "task.h"

int osal_task_create(osal_task_t *task, const char *name,
                     osal_task_func_t func, void *arg,
                     uint32_t stack_bytes, uint32_t priority)
{
    // FreeRTOS takes stack depth in WORDS not bytes
    uint32_t stack_words = stack_bytes / sizeof(StackType_t);

    BaseType_t r = xTaskCreate(
        (TaskFunction_t)func,
        name,
        (uint16_t)stack_words,
        arg,
        (UBaseType_t)priority,
        (TaskHandle_t *)task
    );
    return (r == pdPASS) ? OSAL_OK : OSAL_ERR;
}

void osal_task_delete(osal_task_t *task)
{
    vTaskDelete((TaskHandle_t)*task);
    *task = NULL;
}

void osal_task_yield(void)              { taskYIELD(); }

void osal_task_suspend(osal_task_t *task)
{
    vTaskSuspend((TaskHandle_t)*task);
}

void osal_task_resume(osal_task_t *task)
{
    vTaskResume((TaskHandle_t)*task);
}