// osal/osal_mutex.h
#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include <stdint.h>

typedef void* osal_mutex_t;

int  osal_mutex_create(osal_mutex_t *mtx);
int  osal_mutex_lock(osal_mutex_t *mtx, uint32_t timeout_ms);
int  osal_mutex_unlock(osal_mutex_t *mtx);
void osal_mutex_destroy(osal_mutex_t *mtx);

#endif