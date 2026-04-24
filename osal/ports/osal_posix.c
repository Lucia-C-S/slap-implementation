// osal_posix.c
// Use this on your development machine to test SLAP logic without hardware.
// use for PC unit testing
#include "osal.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* TAI offset: seconds from 1958-01-01 to 1970-01-01 = 378691200 */
#define TAI_EPOCH_OFFSET_SECS 378691200UL

void osal_get_time_cuc(uint8_t out[7])
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t coarse = (uint32_t)(ts.tv_sec + TAI_EPOCH_OFFSET_SECS);
    uint32_t fine   = (uint32_t)(((uint64_t)ts.tv_nsec * (1UL << 24)) / 1000000000UL);
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
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t c = (uint32_t)(ts.tv_sec + TAI_EPOCH_OFFSET_SECS);
    uint32_t f = (uint32_t)(((uint64_t)ts.tv_nsec * (1UL << 24)) / 1000000000UL);
    return ((uint64_t)c << 24) | (f & 0xFFFFFF);
}

void osal_delay_ms(uint32_t ms)    { usleep(ms * 1000); }

static pthread_mutex_t g_crit = PTHREAD_MUTEX_INITIALIZER;
void osal_enter_critical(void)     { pthread_mutex_lock(&g_crit); }
void osal_exit_critical(void)      { pthread_mutex_unlock(&g_crit); }

int  osal_mutex_create(osal_mutex_t *mtx)
{
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(m, NULL);
    *mtx = m;
    return OSAL_OK;
}
int  osal_mutex_lock(osal_mutex_t *mtx, uint32_t t)
{
    (void)t;
    return (pthread_mutex_lock((pthread_mutex_t *)*mtx) == 0) ? OSAL_OK : OSAL_ERR;
}
int  osal_mutex_unlock(osal_mutex_t *mtx)
{
    return (pthread_mutex_unlock((pthread_mutex_t *)*mtx) == 0) ? OSAL_OK : OSAL_ERR;
}
void osal_mutex_destroy(osal_mutex_t *mtx)
{
    pthread_mutex_destroy((pthread_mutex_t *)*mtx);
    free(*mtx);
}

/* Queues and tasks: stubs for POSIX testing (use pipes or full impl if needed) */
int  osal_queue_create(osal_queue_t *q, uint32_t d, uint32_t s) { (void)q;(void)d;(void)s; return OSAL_OK; }
int  osal_queue_send(osal_queue_t *q, const void *i, uint32_t t) { (void)q;(void)i;(void)t; return OSAL_OK; }
int  osal_queue_recv(osal_queue_t *q, void *i, uint32_t t)       { (void)q;(void)i;(void)t; return OSAL_EMPTY; }
void osal_queue_destroy(osal_queue_t *q)                          { (void)q; }
int  osal_task_create(osal_task_t *t, const char *n, osal_task_func_t f, void *a, uint32_t ss, uint32_t p)
                                                                   { (void)t;(void)n;(void)f;(void)a;(void)ss;(void)p; return OSAL_ERR; }
void osal_task_delete(osal_task_t *t)                              { (void)t; }
void osal_task_yield(void)                                         {}
void *osal_malloc(uint32_t size)  { return malloc(size); }
void  osal_free(void *ptr)        { free(ptr); }
void  osal_watchdog_init(uint32_t t) { (void)t; }
void  osal_watchdog_kick(void)       {}
void  osal_init(void)                {}