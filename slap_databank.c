/* slap_databank.c */
#include "slap_databank.h"
#include "osal/osal_mutex.h"
#include <string.h>

/* The pool itself — lives in BSS, zero-initialised at program start */
static slap_packet_t pool[SLAP_POOL_SIZE];
static uint8_t       pool_used[SLAP_POOL_SIZE];
static osal_mutex_t  pool_mutex;

void slap_databank_init(void)
{
    memset(pool,      0, sizeof(pool));
    memset(pool_used, 0, sizeof(pool_used));
    osal_mutex_create(&pool_mutex);
}

slap_packet_t *slap_databank_alloc(void)
{
    slap_packet_t *result = NULL;

    /* Lock the pool before reading or modifying pool_used[].
     * Without this, two concurrent tasks can both see pool_used[i]==0
     * and both claim the same slot — a race condition that produces
     * silent memory corruption.                                    */
    osal_mutex_lock(&pool_mutex, 50); /* 50 ms timeout */

    for (int i = 0; i < SLAP_POOL_SIZE; i++) {
        if (!pool_used[i]) {
            pool_used[i] = 1;
            memset(&pool[i], 0, sizeof(slap_packet_t));
            result = &pool[i];
            break;
        }
    }

    osal_mutex_unlock(&pool_mutex);
    return result; /* NULL = pool exhausted */
}

void slap_databank_free(slap_packet_t *pkt)
{
    if (pkt == NULL)
        return; /* safe no-op */

    osal_mutex_lock(&pool_mutex, 50);

    for (int i = 0; i < SLAP_POOL_SIZE; i++) {
        if (&pool[i] == pkt) {
            pool_used[i] = 0;
            break;
        }
    }

    osal_mutex_unlock(&pool_mutex);
}