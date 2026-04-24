// osal/osal_eventgroup.h
#ifndef OSAL_EVENTGROUP_H
#define OSAL_EVENTGROUP_H

#include <stdint.h>

typedef void*    osal_eventgroup_t;
typedef uint32_t osal_event_bits_t;

/* Define your system event bits here */
#define SLAP_EVENT_RX_READY        (1UL << 0)  /* bytes received from HAL */
#define SLAP_EVENT_HK_DATA_READY   (1UL << 1)  /* HK buffer updated */
#define SLAP_EVENT_LINK_UP         (1UL << 2)  /* radio link established */
#define SLAP_EVENT_SCHED_TICK      (1UL << 3)  /* scheduling timer fired */
#define SLAP_EVENT_FILE_OP_DONE    (1UL << 4)  /* file system op complete */
/* Add more as your mission needs */

int  osal_eventgroup_create(osal_eventgroup_t *eg);

/* Set one or more bits. Can be called from any task. */
int  osal_eventgroup_set(osal_eventgroup_t *eg, osal_event_bits_t bits);

/* Set bits from an ISR */
int  osal_eventgroup_set_from_isr(osal_eventgroup_t *eg,
                                   osal_event_bits_t   bits);

/* Clear one or more bits. */
int  osal_eventgroup_clear(osal_eventgroup_t *eg, osal_event_bits_t bits);

/**
 * Wait for bits to be set.
 * @param wait_all   1 = wait for ALL bits, 0 = wait for ANY bit
 * @param clear_on_exit  1 = auto-clear the bits when they are satisfied
 * @param timeout_ms
 * @return the event bits that were set when the function returned
 */
osal_event_bits_t osal_eventgroup_wait(osal_eventgroup_t *eg,
                                        osal_event_bits_t   bits,
                                        uint8_t             wait_all,
                                        uint8_t             clear_on_exit,
                                        uint32_t            timeout_ms);

void osal_eventgroup_destroy(osal_eventgroup_t *eg);

#endif