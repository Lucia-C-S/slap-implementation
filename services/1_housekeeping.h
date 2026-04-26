// services/1_housekeeping.h
#ifndef SLAP_SERVICE_HK_H
#define SLAP_SERVICE_HK_H

#include "slap_packet.h"

/* HK type values */
#define HK_TYPE_TELEMETRY  0
#define HK_TYPE_LOG        1

/* Message types */
#define HK_MSG_AVAIL_REQ   1
#define HK_MSG_AVAIL_RESP  2
#define HK_MSG_PKT_REQ     3
#define HK_MSG_PKT_SEND    4

/* Secondary header for 1.1, 1.3 (request messages): 10 bits = 2 bytes packed */
typedef struct {
    uint8_t hk_type;      /* 1 bit */
    uint8_t historical;   /* 1 bit */
    uint8_t param_id;     /* 8 bits */
} hk_request_sec_t;

/* Secondary header for 1.2 (available packet report) */
typedef struct {
    uint8_t  hk_type;       /* 1 bit  */
    uint8_t  historical;    /* 1 bit  */
    uint8_t  param_id;      /* 8 bits */
    uint32_t avail_size;    /* X bytes (use 32 bits for size) */
    uint8_t  timestamp[7];  /* 56 bits CUC 4.2 */
} hk_avail_report_sec_t;

int slap_service_housekeeping(slap_packet_t *req, slap_packet_t *resp);

/* Application must implement these callbacks */
int  hk_get_available_size(uint8_t hk_type, uint8_t historical,
                            uint8_t param_id, uint32_t *size_out);
int  hk_read_data(uint8_t hk_type, uint8_t historical,
                  uint8_t param_id, uint8_t *buf, uint16_t max_len,
                  uint16_t *written);

#endif