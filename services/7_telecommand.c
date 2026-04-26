/* services/7_telecommand.c
 *
 * SLAP Service 7 — Telecommand (0x07)
 *
 * Transmits direct execution telecommands from ground to spacecraft
 * for immediate on-board action. Commands execute without temporal
 * delay, independently of the time-based scheduling service.
 *
 * This service is for REAL-TIME operations only. For deferred or
 * scheduled execution, use Service 4 (Time-Based Scheduling).
 *
 * Transactions (§3.7.2):
 *   7.1 Telecommand sending:        ground → OBC
 *   7.2 Telecommand acknowledgement: OBC → ground (echoes TC + ACK)
 *
 * Secondary header for both 7.1 and 7.2: tc_length(16b) = 2 bytes.
 * Data: UTF-8 encoded telecommand string (tc_length bytes).
 *
 * Security note: this service executes commands immediately on
 * reception without authentication. For flight, the application
 * layer (tc_execute callback) should verify command integrity
 * (e.g. HMAC or command whitelist) before execution.
 *
 * Application callbacks required (slap_app_interface.h):
 *   tc_execute() — parse and dispatch the command string
 */

#include "../slap_dispatcher.h"
#include "../slap_secondary_headers.h"
#include "../slap_types.h"
#include "../slap_app_interface.h"
#include <string.h>

/* Telecommand message type identifiers (§3.7.1) */
#define TC_MSG_SEND  1U   /* 7.1 — ground → OBC: command to execute      */
#define TC_MSG_ACK   2U   /* 7.2 — OBC → ground: echoed command + result */

int slap_service_telecommand(slap_packet_t *req, slap_packet_t *resp)
{
    /* Only 7.1 (Telecommand sending) is a valid inbound request */
    if (req->primary_header.msg_type != TC_MSG_SEND)
        return SLAP_ERR_INVALID;

    /* Unpack the secondary header (2 bytes: tc_length field) */
    slap_secondary_header_t sec_in  = {0};
    slap_secondary_header_t sec_out = {0};

    int sec_in_len = slap_sec_unpack(SLAP_SVC_TELECOMMAND,
                                      TC_MSG_SEND,
                                      req->data,
                                      (uint8_t)req->data_len,
                                      &sec_in);
    if (sec_in_len < 0) return SLAP_ERR_INVALID;

    uint16_t tc_len = sec_in.tc.tc_length;

    /* Validate: the declared tc_length must fit in the remaining payload */
    uint16_t payload_available = req->data_len - (uint16_t)sec_in_len;
    if (tc_len == 0U || tc_len > payload_available)
        return SLAP_ERR_INVALID;

    /* Pointer to the UTF-8 command string (NOT null-terminated on wire) */
    const char *tc_str = (const char *)(req->data + sec_in_len);

    /* Execute the telecommand via application callback.
     * tc_execute() receives the exact bytes from the wire.
     * The application is responsible for null-terminating internally
     * if it needs to pass the string to string functions.           */
    int exec_result = tc_execute(tc_str, tc_len);

    /* Build 7.2 Telecommand acknowledgement report.
     * Per §3.7.1: echoes back the telecommand + ACK/NACK result.   */
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_TELECOMMAND;
    resp->primary_header.msg_type     = TC_MSG_ACK;
    resp->primary_header.ack          = (exec_result == SLAP_OK)
                                       ? SLAP_ACK : SLAP_NACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

    /* Secondary header: tc_length mirrors the received command length */
    sec_out.tc.tc_length = tc_len;
    int sl = slap_sec_pack(SLAP_SVC_TELECOMMAND, TC_MSG_ACK,
                            &sec_out, resp->secondary_header,
                            SLAP_MAX_SEC_HEADER);
    if (sl < 0) return SLAP_ERR_INVALID;

    /* Data: echo the telecommand string back to the ground */
    if (tc_len <= SLAP_MAX_DATA) {
        memcpy(resp->data, tc_str, tc_len);
        resp->data_len = tc_len;
    } else {
        resp->data_len = 0U; /* safety: should never happen given earlier check */
    }

    return SLAP_OK;
}