// services/0_echo.c
#include "slap_dispatcher.h"
#include "slap_types.h"

#define ECHO_MSG_PING 1
#define ECHO_MSG_PONG 2

int slap_service_echo(slap_packet_t *req, slap_packet_t *resp)
{
    if (req->primary_header.msg_type != ECHO_MSG_PING)
        return SLAP_ERR_INVALID;

    /* SLAP spec: secondary header and data are empty for Echo */
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = 0x00;
    resp->primary_header.msg_type     = ECHO_MSG_PONG;
    resp->primary_header.ack          = SLAP_ACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->sec_header_len              = 0;
    resp->data_len                    = 0;

    return SLAP_OK;
}
