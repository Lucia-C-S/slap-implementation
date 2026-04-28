#ifndef SLAP_PACKET_H
#define SLAP_PACKET_H
#include <string.h>  // for memcpy

#include <stdint.h>
#include "slap_types.h" // For fixed-width types and other SLAP-specific types
#include "slap_secondary_headers.h" // For secondary header definitions

/* Primary header wire layout (32 bits, big-endian, MSB first):
 *
 *  3         2         1         0
 *  1098765 4321 09876 54321098765 4 3
 *  │      │    │     │           │ │
 *  VER    SVC  MSG   LENGTH      A E
 *  (3b)   (4b) (5b)  (11b)       C F
 *         APP_ID(7b)
 *
 * Bit positions (counting from MSB=31):
 *   [31:29]  PACKET_VER    3 bits
 *   [28:22]  APP_ID        7 bits
 *   [21:18]  SERVICE_TYPE  4 bits
 *   [17:13]  MSG_TYPE      5 bits
 *   [12: 2]  LENGTH       11 bits  (total packet size in bytes)
 *   [    1]  ACK           1 bit
 *   [    0]  ECF_FLAG      1 bit
 */
typedef struct {
    uint8_t  packet_ver;      /* 3-bit version — stored as full byte  */
    uint8_t  app_id;          /* 7-bit node/application identifier    */
    uint8_t  service_type;    /* 4-bit SLAP service (SLAP_SVC_xxx)    */
    uint8_t  msg_type;        /* 5-bit message type within service    */
    uint16_t length;          /* 11-bit total wire packet size        */
    uint8_t  ack;             /* 1-bit: SLAP_ACK / SLAP_NACK          */
    uint8_t  ecf_flag;        /* 1-bit: SLAP_ECF_PRESENT / _ABSENT    */
} slap_primary_header_t;

//SLAP PACKET STRUCTURE:primary header + secondary header + data + trailer
typedef struct {
    slap_primary_header_t    primary_header;
    slap_secondary_header_t  secondary_header;  /* typed union       */
    uint8_t                  sec_wire_len;       /* bytes on the wire */
    uint16_t                 data_len;
    uint8_t                  data[SLAP_MAX_DATA];
    uint16_t                 ecf;
} slap_packet_t;

/* ---------------- API ---------------- */

int slap_encode_packet(const slap_packet_t *pkt, uint8_t *buf, uint16_t buf_size);
int slap_decode_packet(uint8_t *buffer, uint16_t buffer_len, slap_packet_t *pkt);

/* CRC */
uint16_t slap_crc16(const uint8_t *data, uint16_t length);
#endif
