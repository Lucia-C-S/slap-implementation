// osal_posix.c
// Use this on your development machine to test SLAP logic without hardware.
// use for PC unit testing
/* osal/ports/osal_posix.c
 *
 * OSAL port for POSIX targets (Linux, macOS).
 * Used EXCLUSIVELY for PC-side unit testing.
 * Never compiled for flight hardware.
 *
 * Dependencies: pthread, POSIX clock (link with -pthread)
 *
 * Build test binaries:
 *   gcc -Wall -I. test/test_databank.c slap_databank.c \
 *       osal/ports/osal_posix.c -pthread -o test_databank
 */

#include "../osal.h"

#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ================================================================
 * TAI EPOCH OFFSET
 * Seconds between 1958-01-01 (CUC/TAI epoch) and
 * 1970-01-01 (Unix epoch).
 * Derivation: 12 years × 365.25 days × 86400 s = 378,691,200 s
 * ================================================================ */
#define TAI_EPOCH_OFFSET_SECS  378691200UL

/* ================================================================
 * 1. INITIALISATION
 * ================================================================ */

void osal_init(void)
{
    /* POSIX needs no special initialisation for our use case. */
}

/* ================================================================
 * 2. TIME
 * ================================================================ */

void osal_get_time_cuc(uint8_t out[7])
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    /* Coarse: Unix seconds + TAI offset → seconds since 1958-01-01 */
    uint32_t coarse = (uint32_t)((uint64_t)ts.tv_sec
                                + TAI_EPOCH_OFFSET_SECS);

    /* Fine: nanoseconds scaled to 24-bit binary fraction of a second.
     * fine = (ns / 10^9) × 2^24  →  fine = (ns × 2^24) / 10^9
     * Uses 64-bit intermediate to avoid overflow.                   */
    uint32_t fine = (uint32_t)(
        ((uint64_t)ts.tv_nsec * (1UL << 24)) / 1000000000UL
    );

    out[0] = (uint8_t)(coarse >> 24);
    out[1] = (uint8_t)(coarse >> 16);
    out[2] = (uint8_t)(coarse >>  8);
    out[3] = (uint8_t)(coarse);
    out[4] = (uint8_t)(fine   >> 16);
    out[5] = (uint8_t)(fine   >>  8);
    out[6] = (uint8_t)(fine);
}

uint64_t osal_get_time_raw(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t coarse = (uint32_t)((uint64_t)ts.tv_sec
                                + TAI_EPOCH_OFFSET_SECS);
    uint32_t fine = (uint32_t)(
        ((uint64_t)ts.tv_nsec * (1UL << 24)) / 1000000000UL
    );
    return ((uint64_t)coarse << 24) | (fine & 0x00FFFFFFU);
}

void osal_delay_ms(uint32_t ms)
{
    usleep((useconds_t)(ms * 1000U));
}

/* ================================================================
 * 3. CRITICAL SECTIONS
 * On POSIX a global mutex simulates the critical section.
 * This is safe for single-threaded tests; for multi-threaded
 * POSIX tests use the real per-resource mutexes instead.
 * ================================================================ */

static pthread_mutex_t g_crit_mutex = PTHREAD_MUTEX_INITIALIZER;

void osal_enter_critical(void)
{
    pthread_mutex_lock(&g_crit_mutex);
}

void osal_exit_critical(void)
{
    pthread_mutex_unlock(&g_crit_mutex);
}

/* ================================================================
 * 4. WATCHDOG — no hardware, stubs are safe on PC
 * ================================================================ */

void osal_watchdog_init(uint32_t timeout_ms) { (void)timeout_ms; }
void osal_watchdog_kick(void)                {}

/* ================================================================
 * 5. MEMORY
 * ================================================================ */

void *osal_malloc(uint32_t size) { return malloc((size_t)size); }
void  osal_free(void *ptr)       { free(ptr); }

/* ================================================================
 * 6. TASKS
 * Full pthread implementation — usable if you want true multi-
 * threaded POSIX tests. For simple unit tests, the stubs suffice.
 * ================================================================ */

int osal_task_create(osal_task_t *task, const char *name,
                     osal_task_func_t func, void *arg,
                     uint32_t stack_bytes, uint32_t priority)
{
    (void)name; (void)stack_bytes; (void)priority;
    pthread_t *t = (pthread_t *)malloc(sizeof(pthread_t));
    if (!t) return OSAL_ERR;
    int r = pthread_create(t, NULL, (void *(*)(void *))func, arg);
    *task = (osal_task_t)t;
    return (r == 0) ? OSAL_OK : OSAL_ERR;
}

void osal_task_delete(osal_task_t *task)
{
    if (task && *task) {
        pthread_cancel(*(pthread_t *)*task);
        free(*task);
        *task = NULL;
    }
}

void osal_task_yield(void)    { sched_yield(); }
void osal_task_suspend(osal_task_t *t) { (void)t; }
void osal_task_resume(osal_task_t *t)  { (void)t; }

/* ================================================================
 * 7. QUEUES
 * Simple thread-safe circular buffer using a mutex + condvar.
 * ================================================================ */

/* Internal queue state — allocated on heap by osal_queue_create() */
typedef struct {
    uint8_t         *buf;       /* circular buffer storage            */
    uint32_t         depth;     /* max items                          */
    uint32_t         item_size; /* bytes per item                     */
    uint32_t         head;      /* next write position                */
    uint32_t         tail;      /* next read position                 */
    uint32_t         count;     /* current item count                 */
    pthread_mutex_t  mtx;
    pthread_cond_t   cond_not_empty;
    pthread_cond_t   cond_not_full;
} posix_queue_t;

int osal_queue_create(osal_queue_t *q, uint32_t depth, uint32_t item_size)
{
    posix_queue_t *pq = (posix_queue_t *)malloc(sizeof(posix_queue_t));
    if (!pq) return OSAL_ERR;
    pq->buf = (uint8_t *)malloc(depth * item_size);
    if (!pq->buf) { free(pq); return OSAL_ERR; }
    pq->depth     = depth;
    pq->item_size = item_size;
    pq->head = pq->tail = pq->count = 0;
    pthread_mutex_init(&pq->mtx, NULL);
    pthread_cond_init(&pq->cond_not_empty, NULL);
    pthread_cond_init(&pq->cond_not_full,  NULL);
    *q = (osal_queue_t)pq;
    return OSAL_OK;
}

int osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms)
{
    posix_queue_t *pq = (posix_queue_t *)*q;
    pthread_mutex_lock(&pq->mtx);

    if (pq->count == pq->depth) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&pq->mtx);
            return OSAL_FULL;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += (time_t)(timeout_ms / 1000U);
        ts.tv_nsec += (long)((timeout_ms % 1000U) * 1000000L);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (pq->count == pq->depth) {
            int r = pthread_cond_timedwait(&pq->cond_not_full, &pq->mtx, &ts);
            if (r != 0) {
                pthread_mutex_unlock(&pq->mtx);
                return OSAL_TIMEOUT;
            }
        }
    }

    memcpy(pq->buf + (pq->head * pq->item_size), item, pq->item_size);
    pq->head = (pq->head + 1) % pq->depth;
    pq->count++;
    pthread_cond_signal(&pq->cond_not_empty);
    pthread_mutex_unlock(&pq->mtx);
    return OSAL_OK;
}

int osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms)
{
    posix_queue_t *pq = (posix_queue_t *)*q;
    pthread_mutex_lock(&pq->mtx);

    if (pq->count == 0) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&pq->mtx);
            return OSAL_EMPTY;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        if (timeout_ms == 0xFFFFFFFFU) {
            /* wait forever */
            while (pq->count == 0)
                pthread_cond_wait(&pq->cond_not_empty, &pq->mtx);
        } else {
            ts.tv_sec  += (time_t)(timeout_ms / 1000U);
            ts.tv_nsec += (long)((timeout_ms % 1000U) * 1000000L);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            while (pq->count == 0) {
                int r = pthread_cond_timedwait(&pq->cond_not_empty,
                                               &pq->mtx, &ts);
                if (r != 0) {
                    pthread_mutex_unlock(&pq->mtx);
                    return OSAL_EMPTY;
                }
            }
        }
    }

    memcpy(item, pq->buf + (pq->tail * pq->item_size), pq->item_size);
    pq->tail = (pq->tail + 1) % pq->depth;
    pq->count--;
    pthread_cond_signal(&pq->cond_not_full);
    pthread_mutex_unlock(&pq->mtx);
    return OSAL_OK;
}

uint32_t osal_queue_count(osal_queue_t *q)
{
    posix_queue_t *pq = (posix_queue_t *)*q;
    pthread_mutex_lock(&pq->mtx);
    uint32_t c = pq->count;
    pthread_mutex_unlock(&pq->mtx);
    return c;
}

void osal_queue_destroy(osal_queue_t *q)
{
    posix_queue_t *pq = (posix_queue_t *)*q;
    pthread_mutex_destroy(&pq->mtx);
    pthread_cond_destroy(&pq->cond_not_empty);
    pthread_cond_destroy(&pq->cond_not_full);
    free(pq->buf);
    free(pq);
    *q = NULL;
}

/* ================================================================
 * 8. SEMAPHORES
 * ================================================================ */

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
    uint32_t        count;
    uint32_t        max_count;
} posix_sem_t;

static int sem_create_internal(osal_sem_t *sem,
                                uint32_t max_count,
                                uint32_t initial_count)
{
    posix_sem_t *s = (posix_sem_t *)malloc(sizeof(posix_sem_t));
    if (!s) return OSAL_ERR;
    pthread_mutex_init(&s->mtx, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->count     = initial_count;
    s->max_count = max_count;
    *sem = (osal_sem_t)s;
    return OSAL_OK;
}

int osal_sem_create_binary(osal_sem_t *sem, uint8_t initial_value)
{
    return sem_create_internal(sem, 1, (uint32_t)initial_value);
}

int osal_sem_create_counting(osal_sem_t *sem,
                              uint32_t max_count, uint32_t initial_count)
{
    return sem_create_internal(sem, max_count, initial_count);
}

int osal_sem_take(osal_sem_t *sem, uint32_t timeout_ms)
{
    posix_sem_t *s = (posix_sem_t *)*sem;
    pthread_mutex_lock(&s->mtx);
    if (s->count == 0) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&s->mtx);
            return OSAL_TIMEOUT;
        }
        if (timeout_ms == 0xFFFFFFFFU) {
            while (s->count == 0)
                pthread_cond_wait(&s->cond, &s->mtx);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += (time_t)(timeout_ms / 1000U);
            ts.tv_nsec += (long)((timeout_ms % 1000U) * 1000000L);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            while (s->count == 0) {
                if (pthread_cond_timedwait(&s->cond, &s->mtx, &ts) != 0) {
                    pthread_mutex_unlock(&s->mtx);
                    return OSAL_TIMEOUT;
                }
            }
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->mtx);
    return OSAL_OK;
}

int osal_sem_give(osal_sem_t *sem)
{
    posix_sem_t *s = (posix_sem_t *)*sem;
    pthread_mutex_lock(&s->mtx);
    if (s->count < s->max_count) {
        s->count++;
        pthread_cond_signal(&s->cond);
    }
    pthread_mutex_unlock(&s->mtx);
    return OSAL_OK;
}

int osal_sem_give_from_isr(osal_sem_t *sem)
{
    /* No ISR context on POSIX — same as normal give */
    return osal_sem_give(sem);
}

void osal_sem_destroy(osal_sem_t *sem)
{
    posix_sem_t *s = (posix_sem_t *)*sem;
    pthread_mutex_destroy(&s->mtx);
    pthread_cond_destroy(&s->cond);
    free(s);
    *sem = NULL;
}

/* ================================================================
 * 9. MUTEXES
 * ================================================================ */

int osal_mutex_create(osal_mutex_t *mtx)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (!m) return OSAL_ERR;
    pthread_mutex_init(m, NULL);
    *mtx = (osal_mutex_t)m;
    return OSAL_OK;
}

int osal_mutex_lock(osal_mutex_t *mtx, uint32_t timeout_ms)
{
    (void)timeout_ms; /* POSIX trylock/timedlock omitted for simplicity */
    return (pthread_mutex_lock((pthread_mutex_t *)*mtx) == 0)
           ? OSAL_OK : OSAL_ERR;
}

int osal_mutex_unlock(osal_mutex_t *mtx)
{
    return (pthread_mutex_unlock((pthread_mutex_t *)*mtx) == 0)
           ? OSAL_OK : OSAL_ERR;
}

void osal_mutex_destroy(osal_mutex_t *mtx)
{
    pthread_mutex_destroy((pthread_mutex_t *)*mtx);
    free(*mtx);
    *mtx = NULL;
}

/* ================================================================
 * 10. EVENT GROUPS
 * Implemented with a mutex + condvar + 32-bit flag word.
 * ================================================================ */

typedef struct {
    pthread_mutex_t  mtx;
    pthread_cond_t   cond;
    uint32_t         bits;
} posix_eventgroup_t;

int osal_eventgroup_create(osal_eventgroup_t *eg)
{
    posix_eventgroup_t *e =
        (posix_eventgroup_t *)malloc(sizeof(posix_eventgroup_t));
    if (!e) return OSAL_ERR;
    pthread_mutex_init(&e->mtx, NULL);
    pthread_cond_init(&e->cond, NULL);
    e->bits = 0;
    *eg = (osal_eventgroup_t)e;
    return OSAL_OK;
}

int osal_eventgroup_set(osal_eventgroup_t *eg, osal_event_bits_t bits)
{
    posix_eventgroup_t *e = (posix_eventgroup_t *)*eg;
    pthread_mutex_lock(&e->mtx);
    e->bits |= bits;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mtx);
    return OSAL_OK;
}

int osal_eventgroup_set_from_isr(osal_eventgroup_t *eg,
                                  osal_event_bits_t bits)
{
    return osal_eventgroup_set(eg, bits);
}

int osal_eventgroup_clear(osal_eventgroup_t *eg, osal_event_bits_t bits)
{
    posix_eventgroup_t *e = (posix_eventgroup_t *)*eg;
    pthread_mutex_lock(&e->mtx);
    e->bits &= ~bits;
    pthread_mutex_unlock(&e->mtx);
    return OSAL_OK;
}

osal_event_bits_t osal_eventgroup_wait(osal_eventgroup_t *eg,
                                        osal_event_bits_t bits,
                                        uint8_t wait_all,
                                        uint8_t clear_on_exit,
                                        uint32_t timeout_ms)
{
    posix_eventgroup_t *e = (posix_eventgroup_t *)*eg;
    pthread_mutex_lock(&e->mtx);
    osal_event_bits_t result = 0;

    for (;;) {
        if (wait_all) {
            if ((e->bits & bits) == bits) { result = e->bits; break; }
        } else {
            if (e->bits & bits) { result = e->bits; break; }
        }
        if (timeout_ms == 0) break;
        if (timeout_ms == 0xFFFFFFFFU) {
            pthread_cond_wait(&e->cond, &e->mtx);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += (time_t)(timeout_ms / 1000U);
            ts.tv_nsec += (long)((timeout_ms % 1000U) * 1000000L);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++; ts.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&e->cond, &e->mtx, &ts) != 0)
                break;
        }
    }

    if (clear_on_exit) e->bits &= ~bits;
    pthread_mutex_unlock(&e->mtx);
    return result;
}

void osal_eventgroup_destroy(osal_eventgroup_t *eg)
{
    posix_eventgroup_t *e = (posix_eventgroup_t *)*eg;
    pthread_mutex_destroy(&e->mtx);
    pthread_cond_destroy(&e->cond);
    free(e);
    *eg = NULL;
}