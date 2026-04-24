// services/7_telecommand.c
#include "slap_dispatcher.h"
#include "slap_types.h"
#include <string.h>

#define TC_MSG_SEND 1
#define TC_MSG_ACK  2

/* Application must implement this */
extern int tc_execute(const char *command, uint16_t len);

int slap_service_telecommand(slap_packet_t *req, slap_packet_t *resp)
{
    if (req->primary_header.msg_type != TC_MSG_SEND)
        return SLAP_ERR_INVALID;
    if (req->data_len < 2)
        return SLAP_ERR_INVALID;

    /* Secondary header embedded in raw payload: tc_length(16b) */
    uint16_t tc_len = ((uint16_t)req->data[0] << 8) | req->data[1];

    if (req->data_len < (uint16_t)(2 + tc_len))
        return SLAP_ERR_INVALID;

    const char *tc_str = (const char *)(req->data + 2);
    int exec_result = tc_execute(tc_str, tc_len);

    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = 0x07;
    resp->primary_header.msg_type     = TC_MSG_ACK;
    resp->primary_header.ack          = (exec_result == SLAP_OK) ? SLAP_ACK : SLAP_NACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

    /* Echo back the telecommand in the response */
    resp->secondary_header[0] = (uint8_t)(tc_len >> 8);
    resp->secondary_header[1] = (uint8_t)(tc_len);
    resp->sec_header_len = 2;
    memcpy(resp->data, tc_str, tc_len);
    resp->data_len = tc_len;

    return SLAP_OK;
}
