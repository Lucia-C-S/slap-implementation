/* Test the CRC-16 against a known reference vector. The polynomial used (CCITT-16, 0x1021) has a standard test vector:
/* Standard CCITT-16 test vector: */
#include <stdint.h>
uint8_t data[] = "123456789"; /* 9 ASCII bytes */
uint16_t crc = slap_crc16(data, 9);
assert(crc == 0x29B1);        /* expected value — must match exactly */
