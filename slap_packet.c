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
/* slap_packet.c
 *
 * SLAP packet encode and decode implementation.
 *
 * ARCHITECTURE — how typed union and wire bytes interact:
 *
 *   slap_packet_t holds:
 *     primary_header      — unpacked struct (one field per byte)
 *     secondary_header    — typed union (set named fields directly)
 *     data[]              — raw payload bytes
 *
 *   slap_encode_packet():
 *     1. Packs primary_header  bits → 4 wire bytes
 *     2. Calls slap_sec_pack() to convert secondary_header union → wire bytes
 *     3. Copies data[]
 *     4. Appends CRC
 *
 *   slap_decode_packet():
 *     1. Unpacks 4 wire bytes → primary_header fields
 *     2. Calls slap_sec_unpack() to convert wire bytes → secondary_header union
 *     3. Copies remaining bytes → data[]
 *     4. Verifies CRC if ECF_FLAG is set
 *
 *   SERVICE RULE: services NEVER call slap_sec_pack/unpack.
 *     On request:  read req->secondary_header.xxx_member directly.
 *     On response: set resp->secondary_header.xxx_member directly.
 *     slap_encode_packet handles serialisation automatically.
 */

#include "slap_packet.h"
#include "slap_secondary_headers.h"
#include <string.h>

/* ================================================================
 * PRIMARY HEADER PACK / UNPACK
 *
 * Wire layout (32 bits, big-endian, MSB first):
 *   [31:29] PACKET_VER    3 bits
 *   [28:22] APP_ID        7 bits
 *   [21:18] SERVICE_TYPE  4 bits
 *   [17:13] MSG_TYPE      5 bits
 *   [12: 2] LENGTH       11 bits  (total packet size in bytes)
 *   [    1] ACK           1 bit
 *   [    0] ECF_FLAG      1 bit
 * ================================================================ */

static void pack_primary_header(const slap_primary_header_t *hdr,
                                  uint8_t out[4])
{
    /* Assemble all fields into a single 32-bit word, then write
     * big-endian. Masking each field prevents wider values from
     * corrupting adjacent bits if a caller passes a wrong value.  */
    uint32_t w = 0U;

    w |= ((uint32_t)(hdr->packet_ver   & 0x07U)) << 29U; /* [31:29] */
    w |= ((uint32_t)(hdr->app_id       & 0x7FU)) << 22U; /* [28:22] */
    w |= ((uint32_t)(hdr->service_type & 0x0FU)) << 18U; /* [21:18] */
    w |= ((uint32_t)(hdr->msg_type     & 0x1FU)) << 13U; /* [17:13] */
    w |= ((uint32_t)(hdr->length       & 0x7FFU)) << 2U; /* [12: 2] */
    w |= ((uint32_t)(hdr->ack          & 0x01U)) << 1U;  /* [    1] */
    w |= ((uint32_t)(hdr->ecf_flag     & 0x01U));        /* [    0] */

    out[0] = (uint8_t)(w >> 24U);
    out[1] = (uint8_t)(w >> 16U);
    out[2] = (uint8_t)(w >>  8U);
    out[3] = (uint8_t)(w);
}

static void unpack_primary_header(const uint8_t in[4],
                                    slap_primary_header_t *hdr)
{
    uint32_t w = ((uint32_t)in[0] << 24U)
               | ((uint32_t)in[1] << 16U)
               | ((uint32_t)in[2] <<  8U)
               | ((uint32_t)in[3]);

    hdr->packet_ver   = (uint8_t)((w >> 29U) & 0x07U);
    hdr->app_id       = (uint8_t)((w >> 22U) & 0x7FU);
    hdr->service_type = (uint8_t)((w >> 18U) & 0x0FU);
    hdr->msg_type     = (uint8_t)((w >> 13U) & 0x1FU);
    hdr->length       = (uint16_t)((w >>  2U) & 0x7FFU);
    hdr->ack          = (uint8_t)((w >>  1U) & 0x01U);
    hdr->ecf_flag     = (uint8_t)(w & 0x01U);
}

/* ================================================================
 * slap_encode_packet
 *
 * Serialises a slap_packet_t into a contiguous wire-format byte buffer.
 *
 * The caller fills:
 *   pkt->primary_header   (all fields except `length`)
 *   pkt->secondary_header (set the appropriate typed union member)
 *   pkt->data[]           (raw payload bytes)
 *   pkt->data_len         (how many bytes in data[] are valid)
 *
 * slap_encode_packet fills `length` automatically.
 *
 * Returns number of bytes written to buf, or negative SLAP_ERR_xxx.
 * ================================================================ */
int slap_encode_packet(const slap_packet_t *pkt,
                         uint8_t             *buf,
                         uint16_t             buf_size)
{
    /* ---- Step 1: serialise secondary header to a local wire buffer ----
     *
     * slap_sec_pack() converts the typed union into the minimal wire
     * byte sequence for this service/message combination.
     * sec_wire[] is a local stack buffer — max 13 bytes, safe.       */
    uint8_t  sec_wire[SLAP_MAX_SEC_HEADER];
    int      sec_len;

    sec_len = slap_sec_pack(
        pkt->primary_header.service_type,
        pkt->primary_header.msg_type,
        &pkt->secondary_header,   /* typed union → function reads correct member */
        sec_wire,                 /* output: packed wire bytes */
        (uint8_t)sizeof(sec_wire)
    );

    /* sec_len == 0 is valid (messages with no secondary header, e.g. Echo).
     * sec_len < 0 means unknown service/message combination.          */
    if (sec_len < 0) return SLAP_ERR_INVALID;

    /* ---- Step 2: compute total wire packet length ---- */
    uint16_t ecf_bytes = (pkt->primary_header.ecf_flag == SLAP_ECF_PRESENT)
                        ? (uint16_t)SLAP_TRAILER_SIZE
                        : 0U;

    uint16_t total = (uint16_t)SLAP_PRIMARY_HEADER_SIZE
                   + (uint16_t)sec_len
                   + pkt->data_len
                   + ecf_bytes;

    if (total > buf_size)   return SLAP_ERR_OVERFLOW; /* buffer too small */
    if (total > SLAP_MTU)   return SLAP_ERR_OVERFLOW; /* exceeds MTU      */

    /* ---- Step 3: write primary header with auto-filled LENGTH field ---- */
    slap_primary_header_t hdr = pkt->primary_header; /* local copy       */
    hdr.length = total;                               /* fill LENGTH field */
    pack_primary_header(&hdr, buf);

    uint16_t offset = (uint16_t)SLAP_PRIMARY_HEADER_SIZE;

    /* ---- Step 4: write secondary header wire bytes ---- */
    if (sec_len > 0) {
        memcpy(buf + offset, sec_wire, (size_t)sec_len);
        offset += (uint16_t)sec_len;
    }

    /* ---- Step 5: write data payload ---- */
    if (pkt->data_len > 0U) {
        memcpy(buf + offset, pkt->data, pkt->data_len);
        offset += pkt->data_len;
    }

    /* ---- Step 6: append ECF (CRC-16/CCITT) ---- */
    if (pkt->primary_header.ecf_flag == SLAP_ECF_PRESENT) {
        /* CRC covers all bytes from the start of the primary header up
         * to (but not including) the ECF itself.                      */
        uint16_t crc = slap_crc16(buf, offset);
        buf[offset]     = (uint8_t)(crc >> 8U); /* MSB first per §2.4  */
        buf[offset + 1] = (uint8_t)(crc);
        offset += (uint16_t)SLAP_TRAILER_SIZE;
    }

    return (int)offset; /* total bytes written */
}

/* ================================================================
 * slap_decode_packet
 *
 * Deserialises a wire-format byte buffer into a slap_packet_t.
 *
 * After successful return:
 *   pkt->primary_header   is fully populated
 *   pkt->secondary_header typed union is populated (correct member set)
 *   pkt->data[]           contains the payload bytes
 *   pkt->data_len         is the valid byte count in data[]
 *
 * If ECF_FLAG is set in the primary header, the CRC is verified before
 * any further processing. A mismatch returns SLAP_ERR_CRC immediately.
 *
 * Returns SLAP_OK or negative SLAP_ERR_xxx.
 * ================================================================ */
int slap_decode_packet(const uint8_t *buf,
                         uint16_t       buf_len,
                         slap_packet_t *pkt)
{
    /* ---- Step 1: sanity check minimum size ---- */
    if (buf_len < (uint16_t)SLAP_PRIMARY_HEADER_SIZE)
        return SLAP_ERR_INVALID;

    /* ---- Step 2: unpack primary header ---- */
    unpack_primary_header(buf, &pkt->primary_header);

    uint16_t total_len = pkt->primary_header.length;

    /* The LENGTH field in the primary header must match the actual
     * number of bytes we received. If it doesn't, the packet is
     * truncated or padded — either way reject it.                  */
    if (total_len > buf_len || total_len < (uint16_t)SLAP_PRIMARY_HEADER_SIZE)
        return SLAP_ERR_INVALID;

    /* ---- Step 3: verify CRC if ECF is present ---- */
    if (pkt->primary_header.ecf_flag == SLAP_ECF_PRESENT) {
        /* Need room for at least the primary header plus 2 ECF bytes */
        if (total_len < (uint16_t)(SLAP_PRIMARY_HEADER_SIZE + SLAP_TRAILER_SIZE))
            return SLAP_ERR_INVALID;

        /* ECF occupies the last 2 bytes of the packet */
        uint16_t ecf_offset = total_len - (uint16_t)SLAP_TRAILER_SIZE;
        uint16_t rx_crc     = ((uint16_t)buf[ecf_offset] << 8U)
                             | (uint16_t)buf[ecf_offset + 1U];
        uint16_t calc_crc   = slap_crc16(buf, ecf_offset);

        if (rx_crc != calc_crc) return SLAP_ERR_CRC;
    }

    /* ---- Step 4: compute payload region bounds ---- */
    /* Payload = everything between primary header and ECF (if present) */
    uint16_t ecf_bytes  = (pkt->primary_header.ecf_flag == SLAP_ECF_PRESENT)
                         ? (uint16_t)SLAP_TRAILER_SIZE : 0U;
    uint16_t payload_end = total_len - ecf_bytes;       /* offset of ECF or end */
    uint16_t payload_len = payload_end - (uint16_t)SLAP_PRIMARY_HEADER_SIZE;

    const uint8_t *payload = buf + SLAP_PRIMARY_HEADER_SIZE;

    /* ---- Step 5: unpack secondary header from the start of payload ----
     *
     * slap_sec_unpack() reads the correct number of wire bytes for this
     * service/message type and populates the correct union member in
     * pkt->secondary_header. Returns bytes consumed (0 if no sec hdr). */
    memset(&pkt->secondary_header, 0, sizeof(pkt->secondary_header));

    int sec_len = slap_sec_unpack(
        pkt->primary_header.service_type,
        pkt->primary_header.msg_type,
        payload,
        (payload_len < 255U) ? (uint8_t)payload_len : 255U,
        &pkt->secondary_header
    );

    if (sec_len < 0) return SLAP_ERR_INVALID;

    /* ---- Step 6: copy remaining bytes into data[] ---- */
    uint16_t data_offset = (uint16_t)sec_len;

    /* Sanity: data cannot extend beyond the payload region */
    if (data_offset > payload_len) return SLAP_ERR_INVALID;

    uint16_t data_len = payload_len - data_offset;

    if (data_len > (uint16_t)SLAP_MAX_DATA) return SLAP_ERR_OVERFLOW;

    pkt->data_len = data_len;

    if (data_len > 0U)
        memcpy(pkt->data, payload + data_offset, data_len);

    return SLAP_OK;
}