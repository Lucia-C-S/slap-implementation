// services/slap_service_housekeeping.h
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

// services/slap_service_housekeeping.c
#include "slap_service_housekeeping.h"
#include "slap_types.h"
#include "osal.h"
#include <string.h>

static void pack_hk_req_sec(const hk_request_sec_t *s, uint8_t *buf)
{
    /* Pack: [7] hk_type | [6] historical | [5:0] unused,  then param_id */
    buf[0] = ((s->hk_type & 0x01) << 7) | ((s->historical & 0x01) << 6);
    buf[1] = s->param_id;
}

static void unpack_hk_req_sec(const uint8_t *buf, hk_request_sec_t *s)
{
    s->hk_type   = (buf[0] >> 7) & 0x01;
    s->historical = (buf[0] >> 6) & 0x01;
    s->param_id  = buf[1];
}

int slap_service_housekeeping(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;

    /* 1.1 Available packet request → respond with 1.2 */
    if (msg == HK_MSG_AVAIL_REQ) {
        hk_request_sec_t sec;
        unpack_hk_req_sec(req->data, &sec); /* raw payload starts after primary hdr */

        uint32_t avail = 0;
        if (hk_get_available_size(sec.hk_type, sec.historical, sec.param_id, &avail) != SLAP_OK)
            return SLAP_ERR_NODATA;

        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x01;
        resp->primary_header.msg_type     = HK_MSG_AVAIL_RESP;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

        /* Build secondary header for 1.2:
         * hk_type(1b) | historical(1b) | param_id(8b) | avail_size(Xb) | timestamp(56b) */
        uint8_t *sh = resp->secondary_header;
        sh[0] = ((sec.hk_type & 0x01) << 7) | ((sec.historical & 0x01) << 6);
        sh[1] = sec.param_id;
        sh[2] = (uint8_t)(avail >> 24);
        sh[3] = (uint8_t)(avail >> 16);
        sh[4] = (uint8_t)(avail >> 8);
        sh[5] = (uint8_t)(avail);
        osal_get_time_cuc(sh + 6);   /* 7 bytes CUC 4.2 */
        resp->sec_header_len = 13;
        resp->data_len = 0;

        return SLAP_OK;
    }

    /* 1.3 Packet request → respond with 1.4 */
    if (msg == HK_MSG_PKT_REQ) {
        hk_request_sec_t sec;
        unpack_hk_req_sec(req->data, &sec);

        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x01;
        resp->primary_header.msg_type     = HK_MSG_PKT_SEND;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

        /* Secondary header for 1.4: hk_type|historical|param_id|timestamp */
        uint8_t *sh = resp->secondary_header;
        sh[0] = ((sec.hk_type & 0x01) << 7) | ((sec.historical & 0x01) << 6);
        sh[1] = sec.param_id;
        osal_get_time_cuc(sh + 2);
        resp->sec_header_len = 9;

        uint16_t written = 0;
        hk_read_data(sec.hk_type, sec.historical, sec.param_id,
                     resp->data, SLAP_MAX_DATA, &written);
        resp->data_len = written;

        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}

