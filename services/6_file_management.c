/* services/6_file_management.c
 *
 * SLAP Service 6 — File Management (0x06)
 *
 * Provides remote management of on-board file systems (TRISKEL and
 * PAYLOAD computer) through well-defined TC/TM exchanges. This service
 * does NOT transfer file contents — it only manages file system metadata
 * and structure. File content transfer uses Service 5 (LPT).
 *
 * All nodes implement a structured directory tree file system.
 * All operations use the object path = repository path + file/dir name.
 *
 * Functional capabilities defined (§3.6):
 *   ls   — directory listing (two-step: size → records)
 *   mv   — file/directory move
 *   cp   — file/directory copy
 *   rm   — file/directory deletion
 *   mkdir — directory creation
 *
 * 6.4 ls records report data format (per record, repeated):
 *   entry_type (16b): 0=directory, 1=file
 *   entry_size (64b): file size in bytes; 0 for directories
 *   name_length (8b): byte length of the name string
 *   name (N bytes):   UTF-8 name string (NOT null-terminated on wire)
 *
 * Application callbacks required (slap_app_interface.h):
 *   fm_ls_size(), fm_ls_records(), fm_mv(), fm_cp(), fm_rm(), fm_mkdir()
 */

#include "../slap_dispatcher.h"
#include "../slap_secondary_headers.h"
#include "../slap_types.h"
#include "../slap_app_interface.h"
#include <string.h>

/* File Management message type identifiers (§3.6.1) */
#define FM_MSG_LS_SIZE_REQ    1U
#define FM_MSG_LS_SIZE_RESP   2U
#define FM_MSG_LS_REC_REQ     3U
#define FM_MSG_LS_REC_RESP    4U
#define FM_MSG_MV_REQ         5U
#define FM_MSG_MV_ACK         6U
#define FM_MSG_CP_REQ         7U
#define FM_MSG_CP_ACK         8U
#define FM_MSG_RM_REQ         9U
#define FM_MSG_RM_ACK         10U
#define FM_MSG_MKDIR_REQ      11U
#define FM_MSG_MKDIR_ACK      12U

/* Maximum path and name lengths accepted from wire data.
 * Must fit in stack-allocated buffers without overflow.             */
#define FM_MAX_PATH_LEN   255U
#define FM_MAX_NAME_LEN   127U

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/* Build common fields for all File Management response headers */
static void build_fm_resp(slap_packet_t *resp,
                           const slap_packet_t *req,
                           uint8_t msg_type, uint8_t ack)
{
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_FILE_MANAGEMENT;
    resp->primary_header.msg_type     = msg_type;
    resp->primary_header.ack          = ack;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->data_len                    = 0U;
}

/* Safely extract a path string from the wire payload into a fixed buffer.
 * Returns SLAP_OK and fills buf[0..len] (null-terminated) on success.  */
static int extract_string(const uint8_t *src, uint16_t str_len,
                           char *buf, uint16_t buf_size)
{
    if (str_len >= buf_size) return SLAP_ERR_OVERFLOW;
    memcpy(buf, src, str_len);
    buf[str_len] = '\0';
    return SLAP_OK;
}

/* ----------------------------------------------------------------
 * SERVICE HANDLER
 * ---------------------------------------------------------------- */

int slap_service_file_management(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;
    slap_secondary_header_t sec_in  = {0};
    slap_secondary_header_t sec_out = {0};

    /* Unpack the secondary header from data[0..sec_in_len-1].
     * Payload (path/name strings) follows at data[sec_in_len].     */
    int sec_in_len = slap_sec_unpack(SLAP_SVC_FILE_MANAGEMENT,
                                      msg, req->data,
                                      (uint8_t)req->data_len, &sec_in);
    if (sec_in_len < 0) return SLAP_ERR_INVALID;

    const uint8_t *payload     = req->data + sec_in_len;
    uint16_t       payload_len = req->data_len - (uint16_t)sec_in_len;

    /* Temporary string buffers — stack-allocated, bounded by FM_MAX_xxx */
    char path[FM_MAX_PATH_LEN + 1U];
    char name[FM_MAX_NAME_LEN + 1U];
    char dst_path[FM_MAX_PATH_LEN + 1U];
    char dst_name[FM_MAX_NAME_LEN + 1U];

    /* ----------------------------------------------------------
     * MSG 6.1 — ls size request → 6.2 ls size report
     *
     * Ground requests: "how many files/dirs and how many bytes
     * will the full directory listing of this path occupy?"
     * Secondary header: path_length(16b)
     * Data: path string
     * Response 6.2 secondary header: list_size(32b) | num_dirs(16b) | num_files(16b)
     * ---------------------------------------------------------- */
    if (msg == FM_MSG_LS_SIZE_REQ) {
        uint16_t pl = sec_in.fm_path.path_length;
        if (payload_len < pl) return SLAP_ERR_INVALID;
        if (extract_string(payload, pl, path, sizeof(path)) != SLAP_OK)
            return SLAP_ERR_OVERFLOW;

        uint32_t list_size  = 0U;
        uint16_t num_dirs   = 0U;
        uint16_t num_files  = 0U;

        int r = fm_ls_size(path, &list_size, &num_dirs, &num_files);

        build_fm_resp(resp, req, FM_MSG_LS_SIZE_RESP,
                       (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);

        sec_out.fm_ls_size.list_size        = list_size;
        sec_out.fm_ls_size.num_directories  = num_dirs;
        sec_out.fm_ls_size.num_files        = num_files;
        slap_sec_pack(SLAP_SVC_FILE_MANAGEMENT, FM_MSG_LS_SIZE_RESP,
                       &sec_out, resp->secondary_header, SLAP_MAX_SEC_HEADER);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 6.3 — ls records request → 6.4 ls records report
     *
     * Ground requests the actual directory entries. The response
     * data field contains records in the binary format described
     * in the file header above. Large directories may require
     * multiple 6.3/6.4 iterations (ground computes Nsegments
     * from the 6.2 list_size).
     *
     * Secondary header: path_length(16b)
     * Data: path string
     * Response 6.4 data: packed records (no secondary header)
     * ---------------------------------------------------------- */
    if (msg == FM_MSG_LS_REC_REQ) {
        uint16_t pl = sec_in.fm_path.path_length;
        if (payload_len < pl) return SLAP_ERR_INVALID;
        if (extract_string(payload, pl, path, sizeof(path)) != SLAP_OK)
            return SLAP_ERR_OVERFLOW;

        build_fm_resp(resp, req, FM_MSG_LS_REC_RESP, SLAP_ACK);

        uint16_t written = 0U;
        int r = fm_ls_records(path, resp->data, SLAP_MAX_DATA, &written);

        if (r != SLAP_OK) {
            resp->primary_header.ack = SLAP_NACK;
            resp->data_len = 0U;
        } else {
            resp->data_len = written;
        }
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 6.5 — mv request → 6.6 mv acknowledgement report
     *
     * Move a file or directory from source to destination.
     * Secondary header: src_path_len(16b) | src_name_len(8b) |
     *                   dst_path_len(16b) | dst_name_len(8b)
     * Data: src_path | src_name | dst_path | dst_name
     * ---------------------------------------------------------- */
    if (msg == FM_MSG_MV_REQ) {
        uint16_t spl = sec_in.fm_mv_cp.src_path_length;
        uint8_t  snl = sec_in.fm_mv_cp.src_file_name_length;
        uint16_t dpl = sec_in.fm_mv_cp.dst_path_length;
        uint8_t  dnl = sec_in.fm_mv_cp.dst_file_name_length;

        if (payload_len < (uint16_t)(spl + snl + dpl + dnl))
            return SLAP_ERR_INVALID;

        uint16_t off = 0U;
        if (extract_string(payload + off, spl, path, sizeof(path))      != SLAP_OK) return SLAP_ERR_OVERFLOW; off += spl;
        if (extract_string(payload + off, snl, name, sizeof(name))      != SLAP_OK) return SLAP_ERR_OVERFLOW; off += snl;
        if (extract_string(payload + off, dpl, dst_path, sizeof(dst_path)) != SLAP_OK) return SLAP_ERR_OVERFLOW; off += dpl;
        if (extract_string(payload + off, dnl, dst_name, sizeof(dst_name)) != SLAP_OK) return SLAP_ERR_OVERFLOW;

        int r = fm_mv(path, name, dst_path, dst_name);
        build_fm_resp(resp, req, FM_MSG_MV_ACK,
                       (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 6.7 — cp request → 6.8 cp acknowledgement report
     *
     * Copy a file or directory. Identical layout to mv request.
     * ---------------------------------------------------------- */
    if (msg == FM_MSG_CP_REQ) {
        uint16_t spl = sec_in.fm_mv_cp.src_path_length;
        uint8_t  snl = sec_in.fm_mv_cp.src_file_name_length;
        uint16_t dpl = sec_in.fm_mv_cp.dst_path_length;
        uint8_t  dnl = sec_in.fm_mv_cp.dst_file_name_length;

        if (payload_len < (uint16_t)(spl + snl + dpl + dnl))
            return SLAP_ERR_INVALID;

        uint16_t off = 0U;
        if (extract_string(payload + off, spl, path, sizeof(path))         != SLAP_OK) return SLAP_ERR_OVERFLOW; off += spl;
        if (extract_string(payload + off, snl, name, sizeof(name))         != SLAP_OK) return SLAP_ERR_OVERFLOW; off += snl;
        if (extract_string(payload + off, dpl, dst_path, sizeof(dst_path)) != SLAP_OK) return SLAP_ERR_OVERFLOW; off += dpl;
        if (extract_string(payload + off, dnl, dst_name, sizeof(dst_name)) != SLAP_OK) return SLAP_ERR_OVERFLOW;

        int r = fm_cp(path, name, dst_path, dst_name);
        build_fm_resp(resp, req, FM_MSG_CP_ACK,
                       (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 6.9 — rm request → 6.10 rm acknowledgement report
     *
     * Delete a file or directory identified by object path.
     * Secondary header: path_length(16b) | name_length(8b)
     * Data: path | name
     * ---------------------------------------------------------- */
    if (msg == FM_MSG_RM_REQ) {
        uint16_t pl = sec_in.fm_rm_mkdir.path_length;
        uint8_t  nl = sec_in.fm_rm_mkdir.name_length;

        if (payload_len < (uint16_t)(pl + nl)) return SLAP_ERR_INVALID;

        if (extract_string(payload,      pl, path, sizeof(path)) != SLAP_OK)
            return SLAP_ERR_OVERFLOW;
        if (extract_string(payload + pl, nl, name, sizeof(name)) != SLAP_OK)
            return SLAP_ERR_OVERFLOW;

        int r = fm_rm(path, name);
        build_fm_resp(resp, req, FM_MSG_RM_ACK,
                       (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 6.11 — mkdir request → 6.12 mkdir acknowledgement report
     *
     * Create a new directory within an existing path.
     * Secondary header: path_length(16b) | dir_name_length(8b)
     * Data: path | dir_name
     * ---------------------------------------------------------- */
    if (msg == FM_MSG_MKDIR_REQ) {
        uint16_t pl = sec_in.fm_rm_mkdir.path_length;
        uint8_t  nl = sec_in.fm_rm_mkdir.name_length;

        if (payload_len < (uint16_t)(pl + nl)) return SLAP_ERR_INVALID;

        if (extract_string(payload,      pl, path, sizeof(path)) != SLAP_OK)
            return SLAP_ERR_OVERFLOW;
        if (extract_string(payload + pl, nl, name, sizeof(name)) != SLAP_OK)
            return SLAP_ERR_OVERFLOW;

        int r = fm_mkdir(path, name);
        build_fm_resp(resp, req, FM_MSG_MKDIR_ACK,
                       (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}