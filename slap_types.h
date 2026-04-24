// slap_types.h
#ifndef SLAP_TYPES_H
#define SLAP_TYPES_H

#include <stdint.h>

/* Return codes (mirror SPP returntypes.h pattern) */
#define SLAP_OK              0
#define SLAP_ERR_INVALID    -1
#define SLAP_ERR_OVERFLOW   -2
#define SLAP_ERR_NOMEM      -3
#define SLAP_ERR_CRC        -4
#define SLAP_ERR_NODATA     -5

/* Packet constants */
#define SLAP_MTU             2048
#define SLAP_HEADER_SIZE     4      /* primary header: 4 bytes */
#define SLAP_TRAILER_SIZE    2      /* ECF: 2 bytes */
#define SLAP_MAX_SEC_HEADER  8      /* largest secondary header */
#define SLAP_MAX_DATA        2039   /* MTU - 4 - 3 - 2 (generous sec hdr) */
#define SLAP_PACKET_VER      0x01

/* ACK values */
#define SLAP_NACK   0
#define SLAP_ACK    1

/* ECF flag */
#define SLAP_ECF_ABSENT  0
#define SLAP_ECF_PRESENT 1

#endif

