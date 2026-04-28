/* services/slap_service_scheduling.c
 *
 * SLAP Service 4 — Time-Based Scheduling (0x04)
 *
 * Allows ground operators to pre-load telecommands on-board, each
 * with a future release time. The scheduler executes them autonomously
 * when the release time is reached, independently of ground contact.
 *
 * Schedule table: static array of SCHED_MAX_ENTRIES entries.
 * Entry IDs: assigned on-board, start at 1, auto-increment.
 * All times use CUC 4.2 format (7-byte big-endian binary counter).
 *
 * Transactions (§3.4.2):
 *   4.3 Insert → 4.4 Insert report
 *   4.5 Update release time   (no response)
 *   4.1 Enable / 4.2 Disable  (echo same msg with ACK)
 *   4.6 Reset                 (no response)
 *   4.7 Table size req → 4.8 Table size report
 *   loop ⌈list_size/SLAP_MAX_DATA⌉:
 *     4.9 Table data req → 4.10 Table data report (CSV)
 *
 * Application callbacks required (slap_app_interface.h):
 *   tc_execute() — execute a telecommand string immediately
 *
 * CSV format for 4.10 (§3.4.3):
 *   EntryID,Telecommand,ReleaseTime\n  (one row per entry)
 */

#include "../slap_dispatcher.h"
#include "../slap_secondary_headers.h"
#include "../slap_types.h"
#include "../slap_app_interface.h"
#include "../osal/osal.h"
#include "slap_service_defs.h"
#include <string.h>
#include <stdio.h>

/* ----------------------------------------------------------------
 * SCHEDULE TABLE ENTRY
 * ---------------------------------------------------------------- */
typedef struct {
    uint16_t entry_id;                   /* on-board assigned unique ID  */
    uint64_t release_time;               /* CUC 4.2 encoded as uint64_t  */
    uint16_t tc_len;                     /* byte length of tc_str        */
    char     tc_str[SLAP_SCHED_TC_MAX_LEN];   /* null-terminated UTF-8 string */
    uint8_t  valid;                      /* 1 = occupied, 0 = free slot  */
} sched_entry_t;

/* ----------------------------------------------------------------
 * MODULE STATE
 * ---------------------------------------------------------------- */
static sched_entry_t g_schedule[SLAP_SCHED_MAX_ENTRIES]; /* static table     */
static uint8_t       g_sched_enabled = 0U;          /* 0=off, 1=running */
static uint16_t      g_next_entry_id = 1U;          /* auto-increment   */

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/* Pack a 7-byte CUC array into uint64_t for arithmetic comparisons.
 * Encoding: bits[63:24] = coarse seconds, bits[23:0] = fine.       */
static uint64_t cuc_to_u64(const uint8_t cuc[7])
{
    uint32_t coarse = ((uint32_t)cuc[0] << 24)
                    | ((uint32_t)cuc[1] << 16)
                    | ((uint32_t)cuc[2] <<  8)
                    |  (uint32_t)cuc[3];
    uint32_t fine   = ((uint32_t)cuc[4] << 16)
                    | ((uint32_t)cuc[5] <<  8)
                    |  (uint32_t)cuc[6];
    return ((uint64_t)coarse << 24) | (uint64_t)fine;
}

/* Build common response header fields */
static void build_sched_resp(slap_packet_t *resp,
                              const slap_packet_t *req,
                              uint8_t msg_type, uint8_t ack)
{
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_TIME_BASED_SCHEDULING;
    resp->primary_header.msg_type     = msg_type;
    resp->primary_header.ack          = ack;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->data_len                    = 0U;
}

/* ----------------------------------------------------------------
 * SERVICE HANDLER
 * ---------------------------------------------------------------- */

int slap_service_time_based_scheduling(slap_packet_t *req,
                                        slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;
    slap_secondary_header_t sec_in  = {0};
    slap_secondary_header_t sec_out = {0};

    /* Unpack secondary header (may be zero bytes for some messages) */
    int sec_in_len = slap_sec_unpack(SLAP_SVC_TIME_BASED_SCHEDULING,
                                      msg, req->data,
                                      (uint8_t)req->data_len, &sec_in);
    /* sec_in_len == 0 is valid for messages with no secondary header */
    if (sec_in_len < 0) return SLAP_ERR_INVALID;

    /* Pointer to actual payload data (after secondary header) */
    const uint8_t *payload     = req->data + sec_in_len;
    uint16_t       payload_len = req->data_len - (uint16_t)sec_in_len;

    /* ----------------------------------------------------------
     * MSG 4.1 — Enable time-based scheduling function
     * MSG 4.2 — Disable time-based scheduling function
     * Both echo the same message type back with ACK = 1.
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_SCHED_ENABLE || msg == SLAP_MSG_SCHED_DISABLE) {
        g_sched_enabled = (msg == SLAP_MSG_SCHED_ENABLE) ? 1U : 0U;
        build_sched_resp(resp, req, msg, SLAP_ACK);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 4.3 — Insert telecommand into the time-based schedule
     * Secondary header: release_time(56b CUC) + tc_length(16b)
     * Data:             UTF-8 telecommand string
     * Response 4.4:     entry_id(16b) in secondary header
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_SCHED_INSERT) {
        uint16_t tc_len = sec_in.sched_insert.tc_length;

        /* Validate the TC string fits in the payload */
        if (payload_len < tc_len || tc_len >= SLAP_SCHED_TC_MAX_LEN)
            return SLAP_ERR_INVALID;

        /* Find a free slot in the static schedule table */
        int slot = -1;
        for (int i = 0; i < (int)SLAP_SCHED_MAX_ENTRIES; i++) {
            if (!g_schedule[i].valid) { slot = i; break; }
        }
        if (slot < 0) return SLAP_ERR_NOMEM; /* table full */

        /* Populate the entry */
        g_schedule[slot].entry_id     = g_next_entry_id++;
        g_schedule[slot].release_time = cuc_to_u64(
                                         sec_in.sched_insert.release_time);
        g_schedule[slot].tc_len       = tc_len;
        memcpy(g_schedule[slot].tc_str, payload, tc_len);
        g_schedule[slot].tc_str[tc_len] = '\0'; /* null-terminate */
        g_schedule[slot].valid          = 1U;

        /* Build 4.4 Insert report: Entry_ID in secondary header */
        sec_out.sched_receipt.entry_id = g_schedule[slot].entry_id;
        int sl = slap_sec_pack(SLAP_SVC_TIME_BASED_SCHEDULING,
                                SLAP_MSG_SCHED_INSERT_RESP, &sec_out,
                                resp->secondary_header, SLAP_MAX_SEC_HEADER);
        if (sl < 0) return SLAP_ERR_INVALID;

        build_sched_resp(resp, req, SLAP_MSG_SCHED_INSERT_RESP, SLAP_ACK);
        /* Re-apply secondary header after build_sched_resp cleared data_len */
        slap_sec_pack(SLAP_SVC_TIME_BASED_SCHEDULING,
                       SLAP_MSG_SCHED_INSERT_RESP, &sec_out,
                       resp->secondary_header, SLAP_MAX_SEC_HEADER);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 4.5 — Update release time of a scheduled telecommand
     * Secondary header: entry_id(16b) + release_time(56b CUC)
     * release_time == 0 cancels and removes the telecommand.
     * No response required.
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_SCHED_UPDATE) {
        uint16_t target_id = sec_in.sched_update.entry_id;
        uint64_t new_time  = cuc_to_u64(sec_in.sched_update.release_time);

        for (int i = 0; i < (int)SLAP_SCHED_MAX_ENTRIES; i++) {
            if (g_schedule[i].valid &&
                g_schedule[i].entry_id == target_id) {
                if (new_time == 0ULL) {
                    /* Release time = 0 → cancel this command */
                    g_schedule[i].valid = 0U;
                } else {
                    g_schedule[i].release_time = new_time;
                }
                (void)resp;
                return SLAP_OK;
            }
        }
        return SLAP_ERR_INVALID; /* entry_id not found */
    }

    /* ----------------------------------------------------------
     * MSG 4.6 — Reset the time-based schedule
     * 1. Set execution function to "disabled".
     * 2. Delete all scheduled telecommands.
     * No response required.
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_SCHED_RESET) {
        g_sched_enabled = 0U;
        memset(g_schedule, 0, sizeof(g_schedule));
        g_next_entry_id = 1U;
        (void)resp;
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 4.7 — Schedule table size request → 4.8 Table size report
     * Computes the byte length of the complete CSV-encoded table.
     * Ground uses List_size to calculate Nsegments for 4.9/4.10 loop.
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_SCHED_TBL_SZ_REQ) {
        /* Calculate exact CSV byte count:
         * Format per row: "entryID,tc_str,release_time\n"
         * entry_id: up to 5 digits, release_time: up to 20 digits    */
        uint32_t csv_size = 0U;
        for (int i = 0; i < (int)SLAP_SCHED_MAX_ENTRIES; i++) {
            if (!g_schedule[i].valid) continue;
            /* Conservative estimate: 5 + 1 + tc_len + 1 + 20 + 1    */
            csv_size += 5U + 1U + g_schedule[i].tc_len + 1U + 20U + 1U;
        }

        sec_out.sched_tbl_size.list_size = csv_size;
        slap_sec_pack(SLAP_SVC_TIME_BASED_SCHEDULING,
                       SLAP_MSG_SCHED_TBL_SZ_RESP, &sec_out,
                       resp->secondary_header, SLAP_MAX_SEC_HEADER);
        build_sched_resp(resp, req, SLAP_MSG_SCHED_TBL_SZ_RESP, SLAP_ACK);
        slap_sec_pack(SLAP_SVC_TIME_BASED_SCHEDULING,
                       SLAP_MSG_SCHED_TBL_SZ_RESP, &sec_out,
                       resp->secondary_header, SLAP_MAX_SEC_HEADER);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 4.9 — Schedule table data request → 4.10 Table data report
     * Encodes all valid schedule entries as CSV in data[].
     * For large tables, the ground must iterate: send 4.9 as many
     * times as Nsegments computed from the 4.8 response.
     * This implementation returns all data in one packet; if it
     * exceeds SLAP_MAX_DATA, the response is truncated and NACK is
     * set — the ground must issue further 4.9 requests.
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_SCHED_TBL_DATA_REQ) {
        uint16_t offset = 0U;
        uint8_t  truncated = 0U;

        for (int i = 0; i < (int)SLAP_SCHED_MAX_ENTRIES; i++) {
            if (!g_schedule[i].valid) continue;

            int written = snprintf(
                (char *)resp->data + offset,
                (size_t)(SLAP_MAX_DATA - offset),
                "%u,%s,%llu\n",
                (unsigned)g_schedule[i].entry_id,
                g_schedule[i].tc_str,
                (unsigned long long)g_schedule[i].release_time
            );

            if (written <= 0 ||
                offset + (uint16_t)written >= SLAP_MAX_DATA) {
                truncated = 1U;
                break; /* no room for more entries in this packet */
            }
            offset += (uint16_t)written;
        }

        build_sched_resp(resp, req, SLAP_MSG_SCHED_TBL_DATA_RESP,
                          truncated ? SLAP_NACK : SLAP_ACK);
        resp->data_len = offset;
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}

/* ----------------------------------------------------------------
 * SCHEDULING TICK — call from slap_sched_task_fn() every second
 *
 * Compares each valid entry's release_time against current time.
 * Executes and removes any commands whose time has arrived.
 *
 * @param current_time  current time from osal_get_time_raw()
 *                      (bits[63:24] = coarse seconds, [23:0] = fine)
 * ---------------------------------------------------------------- */
void slap_scheduling_tick(uint64_t current_time)
{
    if (!g_sched_enabled) return;

    for (int i = 0; i < (int)SLAP_SCHED_MAX_ENTRIES; i++) {
        if (!g_schedule[i].valid) continue;
        if (g_schedule[i].release_time > current_time) continue;

        /* Release time has arrived — execute the telecommand */
        tc_execute(g_schedule[i].tc_str,
                   g_schedule[i].tc_len);

        /* Remove from schedule regardless of execution result */
        g_schedule[i].valid = 0U;
    }
}