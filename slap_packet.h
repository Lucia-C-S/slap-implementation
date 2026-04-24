#ifndef SLAP_PACKET_H
#define SLAP_PACKET_H
#include <string.h>  // for memcpy

#include <stdint.h>
#include <stdbool.h>  // For boolean data type (bool, true, false)

#define SLAP_MAX_SECONDARY 33 // max is either 9 bytes or file size to be downloaded
#define SLAP_MAX_DATA 2039 // MTU = 2048 - primary header (4 bytes) - secondary header (max 3 bytes in the service message type calculation) - trailer (2 bytes)

//primary header
typedef struct
{
    uint8_t packet_ver;
    uint8_t app_id;
    uint8_t service_type;
    uint8_t msg_type;
    uint16_t length; //bc MTU = 2048
    bool ack;
    bool ecf_flag;

} slap_primary_header_t;

//SLAP PACKET STRUCTURE:primary header + secondary header + data + trailer
typedef struct
{
    slap_primary_header_t primary_header;

    uint8_t secondary_header[SLAP_MAX_SECONDARY]; //secondary as raw buffer
    // we define external structs per message
    // Serialize/deserialize explicitly in encode/decode functions
    uint8_t data[SLAP_MAX_DATA]; //data as raw buffer, max size is MTU - primary header - secondary header - trailer

    uint16_t ecf;

} slap_packet_t;

/* ---------------- API ---------------- */

int slap_encode_packet(slap_packet_t *pkt, uint8_t *buffer);
int slap_decode_packet(uint8_t *buffer, slap_packet_t *pkt, uint16_t buffer_len);

/* CRC */
uint16_t slap_crc16(const uint8_t *data, uint16_t length);
#endif
