
// services/slap_service_housekeeping.c
#include "../slap_types.h"
#include "../slap_secondary_headers.h"
#include "1_housekeeping.h"
#include "slap_service_defs.h"

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

/* services/slap_service_housekeeping.c — corrected dispatch */
int slap_service_housekeeping(slap_packet_t *req, slap_packet_t *resp)
{
    slap_secondary_header_t sec_in  = {0};
    slap_secondary_header_t sec_out = {0};
    uint8_t svc = SLAP_SVC_HOUSEKEEPING;
    uint8_t msg = req->primary_header.msg_type;

    int sec_in_len = slap_sec_unpack(svc, msg, req->data,
                                     (uint8_t)req->data_len, &sec_in);
    if (sec_in_len < 0) return SLAP_ERR_INVALID;

    /* -- 1.1 Available packet request → 1.2 Available packet report -- */
    if (msg == SLAP_MSG_HK_AVAIL_REQ) {
                uint32_t avail_bytes = 0U;
 
        int r = hk_get_available_size(
                    req->secondary_header.hk_req.hk_type,
                    req->secondary_header.hk_req.historical,
                    req->secondary_header.hk_req.param_id,
                    &avail_bytes);
 
        resp->primary_header.msg_type = SLAP_MSG_HK_AVAIL_RESP;
        resp->primary_header.ack      = (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK;
 
        /* Write directly into the union member — no slap_sec_pack call.
         * slap_encode_packet will call slap_sec_pack on this after return. */
        resp->secondary_header.hk_avail.hk_type    =
            req->secondary_header.hk_req.hk_type;
        resp->secondary_header.hk_avail.historical =
            req->secondary_header.hk_req.historical;
        resp->secondary_header.hk_avail.param_id   =
            req->secondary_header.hk_req.param_id;
        resp->secondary_header.hk_avail.avail_size = avail_bytes;
        osal_get_time_cuc(resp->secondary_header.hk_avail.timestamp);
 
        return SLAP_OK;
    }

    /* -- 1.3 Packet request → 1.4 Packet sending -- */
    if (msg == SLAP_MSG_HK_PKT_REQ) {
        resp->primary_header.msg_type = SLAP_MSG_HK_PKT_SEND;
 
        resp->secondary_header.hk_send.hk_type    =
            req->secondary_header.hk_req.hk_type;
        resp->secondary_header.hk_send.historical =
            req->secondary_header.hk_req.historical;
        resp->secondary_header.hk_send.param_id   =
            req->secondary_header.hk_req.param_id;
        osal_get_time_cuc(resp->secondary_header.hk_send.timestamp);
 
        uint16_t written = 0U;
        int r = hk_read_data(
                    req->secondary_header.hk_req.hk_type,
                    req->secondary_header.hk_req.historical,
                    req->secondary_header.hk_req.param_id,
                    resp->data,
                    SLAP_MAX_DATA,
                    &written);
 
        if (r != SLAP_OK) {
            resp->primary_header.ack = SLAP_NACK;
            resp->data_len           = 0U;
        } else {
            resp->data_len           = written;
            /* Full buffer means there may be more — signal pending data */
            resp->primary_header.ack = (written == SLAP_MAX_DATA)
                                      ? SLAP_NACK : SLAP_ACK;
        }
        return SLAP_OK;
    }
 
    return SLAP_ERR_INVALID;
}

