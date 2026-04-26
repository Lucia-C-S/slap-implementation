/* services/5_large_packet_transfer.c
 *
 * SLAP Service 5 — Large Packet Transfer (0x05)
 *
 * Provides application-level segmentation and reassembly for data
 * objects exceeding the MTU. Designed for on-board file downloads.
 *
 * Transaction sequence (§3.5.2):
 *   Ground → OBC  5.1 Data transfer request  (path + filename)
 *   OBC → Ground  5.2 File transfer ACK       (file size, 5 bytes)
 *   loop N times:
 *     Ground → OBC  5.3 Segment request       (seq_flag + seq_id)
 *     OBC → Ground  5.4 Segment report         (seq_flag + seq_id + data)
 *   Ground → OBC  5.5 Completed file transfer
 *   OBC → Ground  5.6 Completion ACK
 *
 * N = ⌈file_size / SLAP_MAX_DATA⌉
 * SLAP_MAX_DATA for this service specifically:
 *   2048 − 4 (primary) − 3 (sec header 5.4) − 2 (ECF) = 2039 bytes
 *   But we use SLAP_MAX_DATA (2029) for conservative allocation.
 */

#include "slap_dispatcher.h"
#include "slap_secondary_headers.h"
#include "slap_types.h"
#include "slap_app_interface.h"
#include "osal/osal.h"
#include <string.h>

/* ----------------------------------------------------------------
 * TRANSFER STATE CONTEXT
 *
 * Persists the path and filename from the 5.1 request across the
 * multiple 5.3 segment requests that follow. Without this, each
 * segment request would have no way to know which file to read.
 *
 * One context is sufficient: SLAP does not support concurrent LPT
 * transfers (the ground station must complete one before starting
 * another). A second simultaneous 5.1 overwrites the state.
 * ---------------------------------------------------------------- */
typedef struct {
    uint8_t  active;           /* 1 = transfer in progress             */
    char     path[256];        /* UTF-8 repository path from 5.1       */
    char     filename[128];    /* UTF-8 file name from 5.1             */
    uint64_t file_size;        /* bytes, from lpt_get_file_size()      */
    uint32_t segments_total;   /* ⌈file_size / SLAP_MAX_DATA⌉          */
} lpt_ctx_t;

static lpt_ctx_t g_lpt_ctx = {0};

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/* Populate response primary header fields common to all LPT replies */
static void build_lpt_resp_hdr(slap_packet_t *resp,
                                const slap_packet_t *req,
                                uint8_t msg_type, uint8_t ack)
{
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_LARGE_PACKET_TRANSFER;
    resp->primary_header.msg_type     = msg_type;
    resp->primary_header.ack          = ack;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
}

/* ----------------------------------------------------------------
 * SERVICE HANDLER
 * ---------------------------------------------------------------- */

int slap_service_large_packet_transfer(slap_packet_t *req,
                                        slap_packet_t *resp)
{
    /* Unpack the secondary header using the typed union system.
     * Per design decision 3.1 (Option A): the raw payload in data[]
     * starts with the secondary header bytes. We unpack them here
     * and then the actual data starts at data[sec_wire_len].         */
    slap_secondary_header_t sec_in  = {0};
    slap_secondary_header_t sec_out = {0};
    uint8_t svc = req->primary_header.service_type;
    uint8_t msg = req->primary_header.msg_type;

    int sec_in_len = slap_sec_unpack(svc, msg,
                                      req->data,
                                      (uint8_t)req->data_len,
                                      &sec_in);
    if (sec_in_len < 0 && msg != 5 && msg != 6)
        return SLAP_ERR_INVALID; /* malformed secondary header */

    /* Payload data pointer — everything after the secondary header */
    const uint8_t *payload     = req->data + sec_in_len;
    uint16_t       payload_len = (sec_in_len >= 0)
                                ? req->data_len - (uint16_t)sec_in_len
                                : 0;

    /* ----------------------------------------------------------
     * MSG 5.1 — Data transfer request
     * Ground requests file identified by path + filename.
     * We respond with file size (5.2) or NACK if not found.
     * ---------------------------------------------------------- */
    if (msg == 1) {
        /* Extract path and filename from payload.
         * Layout: path (path_length bytes) | filename (file_name_length bytes) */
        uint16_t pl = sec_in.lpt_file_ref.path_length;
        uint8_t  nl = sec_in.lpt_file_ref.file_name_length;

        if (payload_len < (uint16_t)(pl + nl))
            return SLAP_ERR_INVALID;

        /* Bounds check before copying to fixed-size buffers */
        if (pl >= sizeof(g_lpt_ctx.path) || nl >= sizeof(g_lpt_ctx.filename))
            return SLAP_ERR_OVERFLOW;

        /* Store transfer context for subsequent segment requests */
        memcpy(g_lpt_ctx.path,     payload,      pl);
        g_lpt_ctx.path[pl] = '\0';
        memcpy(g_lpt_ctx.filename, payload + pl, nl);
        g_lpt_ctx.filename[nl] = '\0';
        g_lpt_ctx.active = 0; /* not active until ACK is sent */

        /* Query file size via application callback */
        uint64_t fsize = 0;
        if (lpt_get_file_size(g_lpt_ctx.path, g_lpt_ctx.filename,
                               &fsize) != SLAP_OK) {
            /* File not found or inaccessible — send NACK */
            build_lpt_resp_hdr(resp, req, 2, SLAP_NACK);
            resp->data_len = 0;
            sec_out.lpt_ack.file_size = 0;
            slap_sec_pack(svc, 2, &sec_out,
                          resp->secondary_header,
                          SLAP_MAX_SEC_HEADER);
            return SLAP_OK;
        }

        /* File found — activate context and compute segment count */
        g_lpt_ctx.file_size      = fsize;
        g_lpt_ctx.segments_total = (uint32_t)(
            (fsize + SLAP_MAX_DATA - 1) / SLAP_MAX_DATA
        );
        g_lpt_ctx.active = 1;

        /* Build 5.2 ACK response with 40-bit file size */
        build_lpt_resp_hdr(resp, req, 2, SLAP_ACK);
        sec_out.lpt_ack.file_size = fsize;
        int sec_out_len = slap_sec_pack(SLAP_SVC_LARGE_PACKET_TRANSFER, 2,
                                         &sec_out,
                                         resp->secondary_header,
                                         SLAP_MAX_SEC_HEADER);
        if (sec_out_len < 0) return SLAP_ERR_INVALID;
        resp->data_len = 0;
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 5.3 — Segment request
     * Ground requests one specific segment identified by seq_id.
     * We read that segment from the file and return it in 5.4.
     * ---------------------------------------------------------- */
    if (msg == 3) {
        if (!g_lpt_ctx.active)
            return SLAP_ERR_INVALID; /* no transfer in progress */

        uint8_t  seq_flag = sec_in.lpt_segment.seq_flag;
        uint32_t seq_id   = sec_in.lpt_segment.seq_id;

        /* seq_id starts at 1 — validate range */
        if (seq_id == 0 || seq_id > g_lpt_ctx.segments_total)
            return SLAP_ERR_INVALID;

        /* Read the requested segment from the application layer */
        uint16_t written = 0;
        if (lpt_read_segment(g_lpt_ctx.path, g_lpt_ctx.filename,
                              seq_id, resp->data,
                              SLAP_MAX_DATA, &written) != SLAP_OK) {
            build_lpt_resp_hdr(resp, req, 4, SLAP_NACK);
            resp->data_len = 0;
            return SLAP_OK;
        }

        /* Determine the correct sequence flag for this segment.
         * The ground may request segments in any order, but we
         * compute the flag based on position for correct framing. */
        uint8_t resp_flag;
        if (g_lpt_ctx.segments_total == 1) {
            resp_flag = SLAP_SEQ_UNSEGMENTED; /* entire file fits in one */
        } else if (seq_id == 1) {
            resp_flag = SLAP_SEQ_FIRST;
        } else if (seq_id == g_lpt_ctx.segments_total) {
            resp_flag = SLAP_SEQ_LAST;
        } else {
            resp_flag = SLAP_SEQ_CONTINUATION;
        }
        (void)seq_flag; /* ground's flag is informational; we set ours */

        /* Build 5.4 segment report */
        build_lpt_resp_hdr(resp, req, 4, SLAP_ACK);
        sec_out.lpt_segment.seq_flag = resp_flag;
        sec_out.lpt_segment.seq_id   = seq_id;
        int sec_out_len = slap_sec_pack(SLAP_SVC_LARGE_PACKET_TRANSFER, 4,
                                         &sec_out,
                                         resp->secondary_header,
                                         SLAP_MAX_SEC_HEADER);
        if (sec_out_len < 0) return SLAP_ERR_INVALID;
        resp->data_len = written;
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 5.5 — Completed file transfer
     * Ground signals that all segments were received.
     * We clear the context and send 5.6 ACK.
     * ---------------------------------------------------------- */
    if (msg == 5) {
        /* Clear transfer state */
        memset(&g_lpt_ctx, 0, sizeof(g_lpt_ctx));

        /* 5.6 completion ACK — no secondary header, no data */
        build_lpt_resp_hdr(resp, req, 6, SLAP_ACK);
        resp->data_len = 0;
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}