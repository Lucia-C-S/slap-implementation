
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
        uint32_t avail = 0;
        if (hk_get_available_size(sec_in.hk_req.hk_type,
                                   sec_in.hk_req.historical,
                                   sec_in.hk_req.param_id,
                                   &avail) != SLAP_OK)
            return SLAP_ERR_NODATA;

        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = SLAP_SVC_HOUSEKEEPING;
        resp->primary_header.msg_type     = SLAP_MSG_HK_AVAIL_RESP;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

        sec_out.hk_avail.hk_type    = sec_in.hk_req.hk_type;
        sec_out.hk_avail.historical = sec_in.hk_req.historical;
        sec_out.hk_avail.param_id   = sec_in.hk_req.param_id;
        sec_out.hk_avail.avail_size = avail;
        osal_get_time_cuc(sec_out.hk_avail.timestamp);

        int sl = slap_sec_pack(SLAP_SVC_HOUSEKEEPING, 2, &sec_out,
                                resp->secondary_header, SLAP_MAX_SEC_HEADER);
        if (sl < 0) return SLAP_ERR_INVALID;
        resp->data_len = 0;
        return SLAP_OK;
    }

    /* -- 1.3 Packet request → 1.4 Packet sending -- */
    if (msg == SLAP_MSG_HK_PKT_REQ) {
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = SLAP_SVC_HOUSEKEEPING;
        resp->primary_header.msg_type     = SLAP_MSG_HK_PKT_SEND;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

        sec_out.hk_send.hk_type    = sec_in.hk_req.hk_type;
        sec_out.hk_send.historical = sec_in.hk_req.historical;
        sec_out.hk_send.param_id   = sec_in.hk_req.param_id;
        osal_get_time_cuc(sec_out.hk_send.timestamp);

        int sl = slap_sec_pack(SLAP_SVC_HOUSEKEEPING, 4, &sec_out,
                                resp->secondary_header, SLAP_MAX_SEC_HEADER);
        if (sl < 0) return SLAP_ERR_INVALID;

        uint16_t written = 0;
        int r = hk_read_data(sec_in.hk_req.hk_type,
                              sec_in.hk_req.historical,
                              sec_in.hk_req.param_id,
                              resp->data, SLAP_MAX_DATA, &written);
        if (r != SLAP_OK) {
            resp->primary_header.ack = SLAP_NACK;
            resp->data_len = 0;
        } else {
            resp->data_len = written;
            /* Signal truncation if data was larger than one packet */
            if (written == SLAP_MAX_DATA)
                resp->primary_header.ack = SLAP_NACK; /* more data pending */
        }
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}

