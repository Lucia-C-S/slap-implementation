// services/slap_service_lpt.c
#include "slap_dispatcher.h"
#include "slap_types.h"
#include <string.h>

/* Sequence flag values */
#define SEQ_CONTINUATION 0x00
#define SEQ_FIRST        0x01
#define SEQ_LAST         0x02
#define SEQ_UNSEGMENTED  0x03

#define LPT_MSG_DATA_REQ   1
#define LPT_MSG_ACK_REPORT 2
#define LPT_MSG_SEG_REQ    3
#define LPT_MSG_SEG_REPORT 4
#define LPT_MSG_COMPLETE   5
#define LPT_MSG_COMPLETE_ACK 6

/* Application callback: get file size by path+name */
extern int lpt_get_file_size(const char *path, const char *name, uint32_t *size_out);
/* Application callback: read segment from file */
extern int lpt_read_segment(const char *path, const char *name,
                             uint32_t seq_id, uint8_t *buf,
                             uint16_t max_len, uint16_t *written);

int slap_service_large_packet_transfer(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;

    /* 5.1 Data transfer request → 5.2 ACK with file size */
    if (msg == LPT_MSG_DATA_REQ) {
        if (req->data_len < 3)
            return SLAP_ERR_INVALID;

        /* Parse secondary header from raw payload:
         * path_length(16b) | file_name_length(8b) | path(Xb) | filename(Yb) */
        uint16_t path_len = ((uint16_t)req->data[0] << 8) | req->data[1];
        uint8_t  name_len = req->data[2];

        if (req->data_len < (uint16_t)(3 + path_len + name_len))
            return SLAP_ERR_INVALID;

        char path[256] = {0};
        char name[128] = {0};
        if (path_len < sizeof(path) && name_len < sizeof(name)) {
            memcpy(path, req->data + 3,              path_len);
            memcpy(name, req->data + 3 + path_len,   name_len);
        }

        uint32_t file_size = 0;
        if (lpt_get_file_size(path, name, &file_size) != SLAP_OK)
            return SLAP_ERR_NODATA;

        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x05;
        resp->primary_header.msg_type     = LPT_MSG_ACK_REPORT;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

        /* Secondary header: file_size (2 bytes per spec = 16-bit — use 32-bit for large files) */
        resp->secondary_header[0] = (uint8_t)(file_size >> 24);
        resp->secondary_header[1] = (uint8_t)(file_size >> 16);
        resp->secondary_header[2] = (uint8_t)(file_size >> 8);
        resp->secondary_header[3] = (uint8_t)(file_size);
        resp->sec_header_len = 4;
        resp->data_len       = 0;
        return SLAP_OK;
    }

    /* 5.3 Segment request → 5.4 Segment report */
    if (msg == LPT_MSG_SEG_REQ) {
        if (req->data_len < 3)
            return SLAP_ERR_INVALID;

        /* Secondary header: seq_flag(2b) | seq_id(22b) — packed into 3 bytes */
        uint8_t  seq_flag = (req->data[0] >> 6) & 0x03;
        uint32_t seq_id   = ((uint32_t)(req->data[0] & 0x3F) << 16)
                          | ((uint32_t)req->data[1] << 8)
                          |  (uint32_t)req->data[2];

        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x05;
        resp->primary_header.msg_type     = LPT_MSG_SEG_REPORT;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;

        resp->secondary_header[0] = (uint8_t)((seq_flag << 6) | ((seq_id >> 16) & 0x3F));
        resp->secondary_header[1] = (uint8_t)(seq_id >> 8);
        resp->secondary_header[2] = (uint8_t)(seq_id);
        resp->sec_header_len = 3;

        /* Read data — path/name must be tracked from prior 5.1 exchange (add state) */
        /* For now, use a stub — in practice, save path/name in module-level state */
        uint16_t written = 0;
        lpt_read_segment("", "", seq_id, resp->data, SLAP_MAX_DATA, &written);
        resp->data_len = written;
        return SLAP_OK;
    }

    /* 5.5 Completed file transfer → 5.6 ACK */
    if (msg == LPT_MSG_COMPLETE) {
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x05;
        resp->primary_header.msg_type     = LPT_MSG_COMPLETE_ACK;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->sec_header_len = 0;
        resp->data_len       = 0;
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}


Service 6: FILE MANAGEMENT
// services/slap_service_file_mgmt.c
#include "slap_dispatcher.h"
#include "slap_types.h"
#include <string.h>
#include <stdio.h>

#define FM_MSG_LS_SIZE_REQ   1
#define FM_MSG_LS_SIZE_RESP  2
#define FM_MSG_LS_REC_REQ    3
#define FM_MSG_LS_REC_RESP   4
#define FM_MSG_MV_REQ        5
#define FM_MSG_MV_ACK        6
#define FM_MSG_CP_REQ        7
#define FM_MSG_CP_ACK        8
#define FM_MSG_RM_REQ        9
#define FM_MSG_RM_ACK        10
#define FM_MSG_MKDIR_REQ     11
#define FM_MSG_MKDIR_ACK     12

/* Application must implement these */
extern int fm_ls_size(const char *path, uint32_t *list_size,
                      uint16_t *num_dirs, uint16_t *num_files);
extern int fm_ls_records(const char *path, uint8_t *buf,
                         uint16_t max_len, uint16_t *written);
extern int fm_mv(const char *src_path, const char *src_name,
                 const char *dst_path, const char *dst_name);
extern int fm_cp(const char *src_path, const char *src_name,
                 const char *dst_path, const char *dst_name);
extern int fm_rm(const char *path, const char *name);
extern int fm_mkdir(const char *path, const char *dir_name);

static void build_fm_resp(slap_packet_t *resp, const slap_packet_t *req,
                          uint8_t msg_type, uint8_t ack)
{
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = 0x06;
    resp->primary_header.msg_type     = msg_type;
    resp->primary_header.ack          = ack;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
}

int slap_service_file_management(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;

    /* Helper: parse path from raw payload at given offset */
    /* payload layout for ls/rm/mkdir: path_length(16b) | path(Mb) */

    if (msg == FM_MSG_LS_SIZE_REQ || msg == FM_MSG_LS_REC_REQ) {
        if (req->data_len < 2) return SLAP_ERR_INVALID;
        uint16_t path_len = ((uint16_t)req->data[0] << 8) | req->data[1];
        if (req->data_len < (uint16_t)(2 + path_len)) return SLAP_ERR_INVALID;

        char path[256] = {0};
        if (path_len < sizeof(path))
            memcpy(path, req->data + 2, path_len);

        if (msg == FM_MSG_LS_SIZE_REQ) {
            uint32_t list_size = 0; uint16_t nd = 0, nf = 0;
            if (fm_ls_size(path, &list_size, &nd, &nf) != SLAP_OK)
                return SLAP_ERR_NODATA;

            build_fm_resp(resp, req, FM_MSG_LS_SIZE_RESP, SLAP_ACK);
            /* Secondary header: list_size(32b) | num_dirs(16b) | num_files(16b) */
            resp->secondary_header[0] = (uint8_t)(list_size >> 24);
            resp->secondary_header[1] = (uint8_t)(list_size >> 16);
            resp->secondary_header[2] = (uint8_t)(list_size >> 8);
            resp->secondary_header[3] = (uint8_t)(list_size);
            resp->secondary_header[4] = (uint8_t)(nd >> 8);
            resp->secondary_header[5] = (uint8_t)(nd);
            resp->secondary_header[6] = (uint8_t)(nf >> 8);
            resp->secondary_header[7] = (uint8_t)(nf);
            resp->sec_header_len = 8;
            resp->data_len = 0;
            return SLAP_OK;
        }

        /* FM_MSG_LS_REC_REQ → FM_MSG_LS_REC_RESP */
        build_fm_resp(resp, req, FM_MSG_LS_REC_RESP, SLAP_ACK);
        resp->sec_header_len = 0;
        uint16_t written = 0;
        fm_ls_records(path, resp->data, SLAP_MAX_DATA, &written);
        resp->data_len = written;
        return SLAP_OK;
    }

    /* MV, CP: src_path_len(16b)|src_name_len(8b)|dst_path_len(16b)|dst_name_len(8b)|... */
    if (msg == FM_MSG_MV_REQ || msg == FM_MSG_CP_REQ) {
        if (req->data_len < 6) return SLAP_ERR_INVALID;
        uint16_t spl = ((uint16_t)req->data[0] << 8) | req->data[1];
        uint8_t  snl = req->data[2];
        uint16_t dpl = ((uint16_t)req->data[3] << 8) | req->data[4];
        uint8_t  dnl = req->data[5];

        if (req->data_len < (uint16_t)(6 + spl + snl + dpl + dnl))
            return SLAP_ERR_INVALID;

        char sp[256]={0}, sn[128]={0}, dp[256]={0}, dn[128]={0};
        uint16_t off = 6;
        if (spl < sizeof(sp)) { memcpy(sp, req->data + off, spl); } off += spl;
        if (snl < sizeof(sn)) { memcpy(sn, req->data + off, snl); } off += snl;
        if (dpl < sizeof(dp)) { memcpy(dp, req->data + off, dpl); } off += dpl;
        if (dnl < sizeof(dn)) { memcpy(dn, req->data + off, dnl); }

        int r = (msg == FM_MSG_MV_REQ) ? fm_mv(sp, sn, dp, dn) : fm_cp(sp, sn, dp, dn);
        uint8_t resp_msg = (msg == FM_MSG_MV_REQ) ? FM_MSG_MV_ACK : FM_MSG_CP_ACK;
        build_fm_resp(resp, req, resp_msg, (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        resp->sec_header_len = 0;
        resp->data_len = 0;
        return SLAP_OK;
    }

    /* RM: path_len(16b)|name_len(8b)|path|name */
    if (msg == FM_MSG_RM_REQ) {
        if (req->data_len < 3) return SLAP_ERR_INVALID;
        uint16_t pl = ((uint16_t)req->data[0] << 8) | req->data[1];
        uint8_t  nl = req->data[2];
        char path[256]={0}, name[128]={0};
        if (pl < sizeof(path)) memcpy(path, req->data + 3, pl);
        if (nl < sizeof(name)) memcpy(name, req->data + 3 + pl, nl);
        int r = fm_rm(path, name);
        build_fm_resp(resp, req, FM_MSG_RM_ACK, (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        resp->sec_header_len = 0; resp->data_len = 0;
        return SLAP_OK;
    }

    /* MKDIR: path_len(16b)|dir_name_len(8b)|path|dir_name */
    if (msg == FM_MSG_MKDIR_REQ) {
        if (req->data_len < 3) return SLAP_ERR_INVALID;
        uint16_t pl = ((uint16_t)req->data[0] << 8) | req->data[1];
        uint8_t  nl = req->data[2];
        char path[256]={0}, dname[128]={0};
        if (pl < sizeof(path))  memcpy(path,  req->data + 3, pl);
        if (nl < sizeof(dname)) memcpy(dname, req->data + 3 + pl, nl);
        int r = fm_mkdir(path, dname);
        build_fm_resp(resp, req, FM_MSG_MKDIR_ACK, (r == SLAP_OK) ? SLAP_ACK : SLAP_NACK);
        resp->sec_header_len = 0; resp->data_len = 0;
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}

