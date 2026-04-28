/*Test the encode/decode round-trip for four cases:
/* Case 1: Echo Ping — no secondary header, no data, ECF present */
/* Case 2: Telecommand send — secondary header + data + ECF */
/* Case 3: ecf_flag=0 — verify no CRC check is attempted on decode */
/* Case 4: Corrupted CRC byte — verify slap_decode_packet() returns SLAP_ERR_CRC */

// For each case:
/*slap_packet_t tx = {0}, rx = {0};
uint8_t buf[SLAP_MTU];
int enc_len = slap_encode_packet(&tx, buf, sizeof(buf));
assert(enc_len > 0);
int result = slap_decode_packet(buf, enc_len, &rx);
assert(result == SLAP_OK);
assert(rx.primary_header.service_type == tx.primary_header.service_type);
 ... assert all fields ... */

/* test/test_packet.c
 *
 * Build:
 *   gcc -Wall -Wextra -I. \
 *       test/test_packet.c \
 *       slap_packet.c ecf.c \
 *       osal/ports/osal_posix.c \
 *       -pthread -o test_packet && ./test_packet
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "slap_packet.h"
#include "slap_databank.h"
#include "osal/osal.h"

int main(void)
{
    osal_init();
    slap_databank_init();

    uint8_t wire[SLAP_MTU];

    /* ---- Test 1: Echo Ping (no secondary header, no data) ---- */
    slap_packet_t *tx = slap_databank_alloc();
    slap_packet_t *rx = slap_databank_alloc();
    assert(tx != NULL && rx != NULL);

    tx->primary_header.packet_ver   = SLAP_PACKET_VER;
    tx->primary_header.app_id       = 0x05;
    tx->primary_header.service_type = SLAP_SVC_ECHO;
    tx->primary_header.msg_type     = 0x01;
    tx->primary_header.ack          = SLAP_NACK;
    tx->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    tx->sec_wire_len                = 0;
    tx->data_len                    = 0;

    int enc_len = slap_encode_packet(tx, wire, sizeof(wire));
    /* Expected: 4 (primary) + 0 (sec) + 0 (data) + 2 (ECF) = 6 */
    assert(enc_len == 6);
    printf("[PASS] Echo ping encoded to %d bytes\n", enc_len);

    int dec_result = slap_decode_packet(wire, (uint16_t)enc_len, rx);
    assert(dec_result == SLAP_OK);
    assert(rx->primary_header.app_id       == 0x05);
    assert(rx->primary_header.service_type == SLAP_SVC_ECHO);
    assert(rx->primary_header.ecf_flag     == SLAP_ECF_PRESENT);
    printf("[PASS] Echo ping decoded correctly\n");

    /* ---- Test 2: CRC corruption is detected ---- */
    wire[2] ^= 0xFF;   /* corrupt one byte */
    int bad = slap_decode_packet(wire, (uint16_t)enc_len, rx);
    assert(bad == SLAP_ERR_CRC);
    printf("[PASS] Corrupted packet rejected with SLAP_ERR_CRC\n");

    /* ---- Test 3: CRC validation with known vector ---- */
    uint8_t test_vec[] = "123456789";
    uint16_t crc = slap_crc16(test_vec, 9);
    assert(crc == 0x29B1);
    printf("[PASS] CRC-16/CCITT known vector 0x%04X == 0x29B1\n", crc);

    slap_databank_free(tx);
    slap_databank_free(rx);

    printf("\nAll packet tests passed.\n");
    return 0;
}
