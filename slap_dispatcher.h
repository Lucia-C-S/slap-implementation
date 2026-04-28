#ifndef SLAP_DISPATCHER_H
#define SLAP_DISPATCHER_H

#include "slap_packet.h"

/* ---------------- SERVICE TYPES ---------------- */

#define SLAP_SERVICE_ECHO                  0x00u
#define SLAP_SERVICE_HOUSEKEEPING          0x01u
#define SLAP_SERVICE_TIME_MANAGEMENT       0x02u
#define SLAP_SERVICE_POSITION_MANAGEMENT   0x03u
#define SLAP_SERVICE_TIME_BASED_SCHEDULING 0x04u
#define SLAP_SERVICE_LARGE_PACKET_TRANSFER 0x05u
#define SLAP_SERVICE_FILE_MANAGEMENT       0x06u
#define SLAP_SERVICE_TELECOMMAND           0x07u
#define SLAP_NUM_SERVICES                  8u

/* ---------------- GENERIC HANDLER TYPE ---------------- */

typedef int (*slap_service_handler_t)(slap_packet_t *req, slap_packet_t *resp);

/* ---------------- DISPATCHER API ---------------- */

int slap_dispatch_packet(slap_packet_t *req, slap_packet_t *resp);

/* ---------------- SERVICE HANDLERS ---------------- */

int slap_service_echo(slap_packet_t *req, slap_packet_t *resp);
int slap_service_housekeeping(slap_packet_t *req, slap_packet_t *resp);
int slap_service_time_management(slap_packet_t *req, slap_packet_t *resp);
int slap_service_position_management(slap_packet_t *req, slap_packet_t *resp);
int slap_service_time_based_scheduling(slap_packet_t *req, slap_packet_t *resp);
int slap_service_large_packet_transfer(slap_packet_t *req, slap_packet_t *resp);
int slap_service_file_management(slap_packet_t *req, slap_packet_t *resp);
int slap_service_telecommand(slap_packet_t *req, slap_packet_t *resp);

#endif

