/* services/0_echo.c
 *
 * SLAP Service 0 — Echo (0x00)
 *
 * Purpose: verify connectivity and basic communication integrity
 * between two nodes. The simplest possible service — its only job
 * is to prove the full encode→dispatch→decode→encode stack works.
 *
 * Transactions (§3.0.2):
 *   Receiver → Responder:  0.1 Ping
 *   Responder → Receiver:  0.2 Pong  (ACK = 1)
 *
 * Secondary header: NONE for both message types.
 * Data field:       NONE for both message types.
 * Wire size:        4 (primary) + 0 + 0 + 2 (ECF) = 6 bytes exactly.
 */

#include "slap_dispatcher.h"
#include "slap_types.h"
#include "slap_service_defs.h"

int slap_service_echo(slap_packet_t *req, slap_packet_t *resp)
{
    /* Only the Ping message type is a valid inbound request.
     * Any other message type is a protocol violation.               */
    if (req->primary_header.msg_type != SLAP_MSG_ECHO_PING)
        return SLAP_ERR_INVALID;

    /* Build the Pong response.
     * Per §3.0.1: ACK flag shall be set to '1' in the Pong packet.
     * Per §3.0: secondary header and data fields are empty (0 bytes). */
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_ECHO;
    resp->primary_header.msg_type     = SLAP_MSG_ECHO_PONG;
    resp->primary_header.ack          = SLAP_ACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->data_len                    = 0U;
    /* sec_header_len is not needed — slap_sec_pack(SVC_ECHO, msg)
     * returns 0 and writes nothing, so secondary_header[] is unused. */

    return SLAP_OK;
}
