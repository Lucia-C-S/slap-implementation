// services/3_position_management.c
#include "../slap_dispatcher.h"
#include "../slap_types.h"
#include "osal.h"
#include <string.h>

#define POS_MSG_REQUEST 1
#define POS_MSG_REPORT  2

/* Application must implement this callback */
extern int position_get(uint8_t *str_buf, uint16_t max_len, uint16_t *written);

static void pack_float_be(uint8_t *b, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);  /* bit-exact copy, avoids aliasing UB */
    b[0] = (uint8_t)(bits >> 24);
    b[1] = (uint8_t)(bits >> 16);
    b[2] = (uint8_t)(bits >>  8);
    b[3] = (uint8_t)(bits);
}

static float unpack_float_be(const uint8_t *b)
{
    uint32_t bits = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
                  | ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

int slap_service_position_management(slap_packet_t *req,
                                      slap_packet_t *resp)
{
    if (req->primary_header.msg_type != 1) return SLAP_ERR_INVALID;

    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_POSITION_MANAGEMENT;
    resp->primary_header.msg_type     = 2;
    resp->primary_header.ack          = SLAP_ACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->data_len                    = 0;

    /* 3.2 has no secondary header — timestamp + position in data[] */
    osal_get_time_cuc(resp->data);         /* bytes [0..6]  */

    /* position_get() fills bytes [7..30] with 6×float32 ECI state */
    uint16_t written = 0;
    if (position_get(resp->data + 7, SLAP_MAX_DATA - 7, &written)
        == SLAP_OK) {
        resp->data_len = 7 + written; /* = 31 bytes */
    } else {
        resp->primary_header.ack = SLAP_NACK;
    }
    return SLAP_OK;
}