// osal/osal_semaphore.h
#ifndef OSAL_SEMAPHORE_H
#define OSAL_SEMAPHORE_H

#include <stdint.h>

typedef void* osal_sem_t;

/* Binary semaphore: value is either 0 or 1.
 * initial_value: 0 = start locked, 1 = start available */
int  osal_sem_create_binary(osal_sem_t *sem, uint8_t initial_value);

/* Counting semaphore: can count up to max_count.
 * Used for resource pools (e.g. "N packet slots available") */
int  osal_sem_create_counting(osal_sem_t *sem,
                               uint32_t   max_count,
                               uint32_t   initial_count);

/* Take (decrement). Blocks up to timeout_ms. */
int  osal_sem_take(osal_sem_t *sem, uint32_t timeout_ms);

/* Give (increment). Can be called from an ISR. */
int  osal_sem_give(osal_sem_t *sem);

/* Give from an ISR (FreeRTOS requires a different call from ISR context) */
int  osal_sem_give_from_isr(osal_sem_t *sem);

void osal_sem_destroy(osal_sem_t *sem);

#endif