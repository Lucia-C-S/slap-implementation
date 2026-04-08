#ifndef SLAP_DISPATCHER_H
#define SLAP_DISPATCHER_H

#include "slap_packet.h"

/* ---------------- SERVICE TYPES ---------------- */

#define SLAP_SERVICE_ECHO                  0
#define SLAP_SERVICE_HOUSEKEEPING          1
#define SLAP_SERVICE_TIME_MANAGEMENT       2
#define SLAP_SERVICE_POSITION_MANAGEMENT   3
#define SLAP_SERVICE_TIME_BASED_SCHEDULING 4
#define SLAP_SERVICE_LARGE_PACKET_TRANSFER 5
#define SLAP_SERVICE_FILE_MANAGEMENT       6
#define SLAP_SERVICE_TELECOMMAND           7

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

