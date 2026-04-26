/*Test the encode/decode round-trip for four cases:
/* Case 1: Echo Ping — no secondary header, no data, ECF present */
/* Case 2: Telecommand send — secondary header + data + ECF */
/* Case 3: ecf_flag=0 — verify no CRC check is attempted on decode */
/* Case 4: Corrupted CRC byte — verify slap_decode_packet() returns SLAP_ERR_CRC */
#include "../slap_packet.h"
#include <assert.h>
// For each case:
slap_packet_t tx = {0}, rx = {0};
uint8_t buf[SLAP_MTU];
int enc_len = slap_encode_packet(&tx, buf, sizeof(buf));
assert(enc_len > 0);
int result = slap_decode_packet(buf, enc_len, &rx);
assert(result == SLAP_OK);
assert(rx.primary_header.service_type == tx.primary_header.service_type);
/* ... assert all fields ... */

