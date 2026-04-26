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
/*
 * Wire format (32-bit primary header, big-endian):
 *  Bits [31:29] = packet_ver  (3)
 *  Bits [28:22] = app_id      (7)
 *  Bits [21:18] = svc_type    (4)
 *  Bits [17:13] = msg_type    (5)
 *  Bits [12:2]  = length     (11)
 *  Bit  [1]     = ack         (1)
 *  Bit  [0]     = ecf_flag    (1)
 */

int slap_encode_packet(const slap_packet_t *pkt,
                        uint8_t *buf, uint16_t buf_size)
{
    /* Pack secondary header to a local wire buffer */
    uint8_t sec_wire[SLAP_MAX_SEC_HEADER];
    int sec_len = slap_sec_pack(
        pkt->primary_header.service_type,
        pkt->primary_header.msg_type,
        &pkt->secondary_header,
        sec_wire, sizeof(sec_wire)
    );
    if (sec_len < 0) return SLAP_ERR_INVALID;

    uint16_t total = (uint16_t)(SLAP_PRIMARY_HEADER_SIZE + sec_len
                   + pkt->data_len
                   + (pkt->primary_header.ecf_flag
                      ? SLAP_TRAILER_SIZE : 0));

    if (total > buf_size || total > SLAP_MTU)
        return SLAP_ERR_OVERFLOW;

    /* Write primary header with auto-filled length */
    slap_primary_header_t hdr = pkt->primary_header;
    hdr.length = total;
    pack_primary_header(&hdr, buf);

    uint16_t off = SLAP_PRIMARY_HEADER_SIZE;
    if (sec_len > 0) {
        memcpy(buf + off, sec_wire, (uint16_t)sec_len);
        off += (uint16_t)sec_len;
    }
    if (pkt->data_len > 0) {
        memcpy(buf + off, pkt->data, pkt->data_len);
        off += pkt->data_len;
    }
    if (pkt->primary_header.ecf_flag == SLAP_ECF_PRESENT) {
        uint16_t crc = slap_crc16(buf, off);
        buf[off]     = (uint8_t)(crc >> 8);
        buf[off + 1] = (uint8_t)(crc);
        off += 2;
    }
    return (int)off;
}

int slap_decode_packet(uint8_t *buffer, slap_packet_t *pkt, uint16_t buffer_len)
{
    if (buffer_len < 8 + SLAP_MAX_SEC_HEADER + 2) return -1;

    uint8_t *p = buffer;
    pkt->primary_header.packet_ver = *p++;
    pkt->primary_header.app_id = *p++;
    pkt->primary_header.service_type = *p++;
    pkt->primary_header.msg_type = *p++;
    pkt->primary_header.length = *(uint16_t*)p; p += 2;
    pkt->primary_header.ack = *p++;
    pkt->primary_header.ecf_flag = *p++;
    if (pkt->primary_header.length > SLAP_MAX_DATA) return -1;

    if (buffer_len < 8 + SLAP_MAX_SEC_HEADER + pkt->primary_header.length + 2) return -1;

    memcpy(pkt->secondary_header, p, SLAP_MAX_SEC_HEADER); p += SLAP_MAX_SEC_HEADER;
    memcpy(pkt->data, p, pkt->primary_header.length); p += pkt->primary_header.length;
    pkt->ecf = *(uint16_t*)p; p += 2;
    uint16_t crc = slap_crc16(buffer, p - buffer - 2);
    if (crc != pkt->ecf) return -1;

    return 0;
}
