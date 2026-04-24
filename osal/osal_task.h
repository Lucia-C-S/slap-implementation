/* osal/osal_task.h */
#ifndef OSAL_TASK_H
#define OSAL_TASK_H
#include <stdint.h>

typedef void *osal_task_t;
typedef void (*osal_task_func_t)(void *arg);

/* Stack size constants — tune to your MCU RAM once Item 0.1 confirmed */
#define OSAL_STACK_SMALL   512U   /* bytes */
#define OSAL_STACK_MEDIUM 1024U
#define OSAL_STACK_LARGE  2048U

/* Priority levels — map to FreeRTOS 1..configMAX_PRIORITIES-1 */
#define OSAL_PRIO_LOW    1U
#define OSAL_PRIO_MEDIUM 3U
#define OSAL_PRIO_HIGH   5U

int  osal_task_create(osal_task_t *task, const char *name,
                      osal_task_func_t func, void *arg,
                      uint32_t stack_bytes, uint32_t priority);
void osal_task_delete(osal_task_t *task);
void osal_task_yield(void);
void osal_task_suspend(osal_task_t *task);
void osal_task_resume(osal_task_t *task);

#endif /* OSAL_TASK_H */