#include "slap_packet.h"

#define CRC_POLY 0x1021

uint16_t slap_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0x0000;

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC_POLY;
            else
                crc <<= 1;
        }
    }

    return crc;
    //based on polynomial x^16 + x^12 + x^5 + 1
}

int slap_encode_packet(slap_packet_t *pkt, uint8_t *buffer)
{
    if (pkt->primary_header.length > SLAP_MAX_DATA) return -1;

    uint8_t *p = buffer;
    *p++ = pkt->primary_header.packet_ver;
    *p++ = pkt->primary_header.app_id;
    *p++ = pkt->primary_header.service_type;
    *p++ = pkt->primary_header.msg_type;
    *(uint16_t*)p = pkt->primary_header.length; p += 2;
    *p++ = pkt->primary_header.ack;
    *p++ = pkt->primary_header.ecf_flag;
    memcpy(p, pkt->secondary_header, SLAP_MAX_SECONDARY); p += SLAP_MAX_SECONDARY;
    memcpy(p, pkt->data, pkt->primary_header.length); p += pkt->primary_header.length;
    uint16_t crc = slap_crc16(buffer, p - buffer);
    *(uint16_t*)p = crc; p += 2;
    return p - buffer;
}

int slap_decode_packet(uint8_t *buffer, slap_packet_t *pkt, uint16_t buffer_len)
{
    if (buffer_len < 8 + SLAP_MAX_SECONDARY + 2) return -1;

    uint8_t *p = buffer;
    pkt->primary_header.packet_ver = *p++;
    pkt->primary_header.app_id = *p++;
    pkt->primary_header.service_type = *p++;
    pkt->primary_header.msg_type = *p++;
    pkt->primary_header.length = *(uint16_t*)p; p += 2;
    pkt->primary_header.ack = *p++;
    pkt->primary_header.ecf_flag = *p++;
    if (pkt->primary_header.length > SLAP_MAX_DATA) return -1;

    if (buffer_len < 8 + SLAP_MAX_SECONDARY + pkt->primary_header.length + 2) return -1;

    memcpy(pkt->secondary_header, p, SLAP_MAX_SECONDARY); p += SLAP_MAX_SECONDARY;
    memcpy(pkt->data, p, pkt->primary_header.length); p += pkt->primary_header.length;
    pkt->ecf = *(uint16_t*)p; p += 2;
    uint16_t crc = slap_crc16(buffer, p - buffer - 2);
    if (crc != pkt->ecf) return -1;

    return 0;
}
