//  Dispatcher (Handles ALL Services)
#include "slap_dispatcher.h"

/* ---------------- DISPATCH TABLE ---------------- */

static const slap_service_handler_t dispatch_table[] =
{
    slap_service_echo,
    slap_service_housekeeping,
    slap_service_time_management,
    slap_service_position_management,
    slap_service_time_based_scheduling,
    slap_service_large_packet_transfer,
    slap_service_file_management,
    slap_service_telecommand
};
//Equivalent to:
//switch(service)
    //case 0: slap_service_echo(...); break;
    //case 1: slap_service_housekeeping(...); break;
    //...

int slap_dispatch_packet(slap_packet_t *req, slap_packet_t *resp)
{
    if (req->primary_header.service_type > 7)
        return -1; // “I could NOT process this packet”

    return dispatch_table[req->primary_header.service_type](req, resp);
}

//Execution flow
//Packet arrives already decoded
//Dispatcher checks service_type

//Calls corresponding function:
//slap_service_X(req, resp)

//That function returns: 0 = success // <0 = error



//Role of the Dispatcher
//The dispatcher is responsible for:
// - Decoding (service_type, msg_type)
// - Routing packets to the correct handler
// - Managing request/report logic
// - Generating response packets
