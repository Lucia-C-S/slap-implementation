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
#define SLAP_PRIMARY_HEADER_SIZE     4      /* primary header: 4 bytes */
#define SLAP_TRAILER_SIZE    2      /* ECF: 2 bytes */
#define SLAP_MAX_SEC_HEADER  13      /* largest secondary header */
#define SLAP_MAX_DATA       (SLAP_MTU - SLAP_PRIMARY_HEADER_SIZE \
                             - SLAP_MAX_SEC_HEADER - SLAP_TRAILER_SIZE)
/* = 2048 - 4 - 13 - 2 = 2029 bytes */
/*Note: SLAP_MAX_DATA becomes 2029 bytes (not 2034 or 2039).
The 2039 bytes in the spec is the per-message maximum for 5.4 
specifically (which has only a 3-byte secondary header).
 SLAP_MAX_DATA in the struct must be the worst-case across
  all messages.*/

#define SLAP_PACKET_VER      0x01

//#define SLAP_MAX_SECONDARY 33 // max is either 9 bytes or file size to be downloaded


/* ACK values */
#define SLAP_NACK   0
#define SLAP_ACK    1

/* ECF flag */
#define SLAP_ECF_ABSENT  0
#define SLAP_ECF_PRESENT 1

/* SLAP services (service_type values) */
#define SLAP_SVC_ECHO                 0x00
#define SLAP_SVC_HOUSEKEEPING         0x01
#define SLAP_SVC_TIME_MANAGEMENT      0x02
#define SLAP_SVC_POSITION_MANAGEMENT  0x03
#define SLAP_SVC_TIME_BASED_SCHEDULING 0x04
#define SLAP_SVC_LARGE_PACKET_TRANSFER 0x05
#define SLAP_SVC_FILE_MANAGEMENT      0x06
#define SLAP_SVC_TELECOMMAND             0x07


#endif

