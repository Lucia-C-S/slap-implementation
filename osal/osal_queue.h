// osal/osal_queue.h
#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include <stdint.h>

typedef void* osal_queue_t;

/* Create a queue.
 * depth:     max number of items it can hold simultaneously
 * item_size: size in bytes of each item */
int  osal_queue_create(osal_queue_t *q,
                       uint32_t      depth,
                       uint32_t      item_size);

/* Send an item. Copies item_size bytes from `item` into the queue.
 * timeout_ms: 0 = non-blocking, 0xFFFFFFFF = wait forever */
int  osal_queue_send(osal_queue_t *q,
                     const void   *item,
                     uint32_t      timeout_ms);

/* Receive an item into `item`. Blocks up to timeout_ms. */
int  osal_queue_recv(osal_queue_t *q,
                     void         *item,
                     uint32_t      timeout_ms);

/* How many items are currently in the queue */
uint32_t osal_queue_count(osal_queue_t *q);

void osal_queue_destroy(osal_queue_t *q);

#endif