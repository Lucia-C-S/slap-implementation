// services/slap_service_time_management.c
#include "../slap_dispatcher.h"
#include "../slap_types.h"
#include "osal.h"
#include <string.h>

#define TM_MSG_SET_RATE    1
#define TM_MSG_TIME_REPORT 2
#define TM_MSG_TIME_REQ    3

/* Generation rate (seconds) stored in module-level state */
static uint32_t g_time_report_rate = 0; /* 0 = disabled */

int slap_service_time_management(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;

    /* 2.1 Set time report generation rate */
    if (msg == TM_MSG_SET_RATE) {
        if (req->data_len < 4)
            return SLAP_ERR_INVALID;
        g_time_report_rate = ((uint32_t)req->data[0] << 24)
                           | ((uint32_t)req->data[1] << 16)
                           | ((uint32_t)req->data[2] << 8)
                           |  (uint32_t)req->data[3];
        /* No response required per spec (it's a set command) */
        return SLAP_OK;
    }

    /* 2.3 On-board time request → respond with 2.2 */
    if (msg == TM_MSG_TIME_REQ) {
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x02;
        resp->primary_header.msg_type     = TM_MSG_TIME_REPORT;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->sec_header_len              = 0;

        /* Data: 7 bytes CUC 4.2 on-board time */
        osal_get_time_cuc(resp->data);
        resp->data_len = 7;

        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}

/* Call this from your periodic timer task if rate != 0 */
void slap_time_broadcast(uint8_t *tx_buf, uint16_t tx_buf_size)
{
    if (g_time_report_rate == 0)
        return;

    slap_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.primary_header.packet_ver   = SLAP_PACKET_VER;
    pkt.primary_header.app_id       = 0x00; /* broadcast address */
    pkt.primary_header.service_type = 0x02;
    pkt.primary_header.msg_type     = TM_MSG_TIME_REPORT;
    pkt.primary_header.ack          = SLAP_ACK;
    pkt.primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    pkt.sec_header_len              = 0;

    osal_get_time_cuc(pkt.data);
    pkt.data_len = 7;

    int len = slap_encode_packet(&pkt, tx_buf, tx_buf_size);
    if (len > 0)
        hal_send(tx_buf, (uint16_t)len);
}
