// services/3_position_management.c
#include "slap_dispatcher.h"
#include "slap_types.h"
#include "osal.h"
#include <string.h>

#define POS_MSG_REQUEST 1
#define POS_MSG_REPORT  2

/* Application must implement this callback */
extern int position_get(uint8_t *str_buf, uint16_t max_len, uint16_t *written);

int slap_service_position_management(slap_packet_t *req, slap_packet_t *resp)
{
    if (req->primary_header.msg_type != POS_MSG_REQUEST)
        return SLAP_ERR_INVALID;

    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = 0x03;
    resp->primary_header.msg_type     = POS_MSG_REPORT;
    resp->primary_header.ack          = SLAP_ACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->sec_header_len              = 0;

    /* Data: Timestamp (7 bytes CUC 4.2) + Position string */
    osal_get_time_cuc(resp->data);
    uint16_t written = 0;
    position_get(resp->data + 7, SLAP_MAX_DATA - 7, &written);
    resp->data_len = 7 + written;

    return SLAP_OK;
}
