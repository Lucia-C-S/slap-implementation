// services/4_time_based_scheduling.h
#ifndef SLAP_SERVICE_SCHED_H
#define SLAP_SERVICE_SCHED_H

#include "slap_packet.h"

#define SCHED_MAX_ENTRIES  16
#define SCHED_TC_MAX_LEN   128

typedef struct {
    uint16_t entry_id;
    uint64_t release_time;   /* CUC 4.2 stored as 56-bit value in uint64 */
    uint16_t tc_len;
    char     tc[SCHED_TC_MAX_LEN];
    uint8_t  valid;
} sched_entry_t;

int slap_service_time_based_scheduling(slap_packet_t *req, slap_packet_t *resp);
void slap_scheduling_tick(uint64_t current_time); /* call from timer task */

#endif