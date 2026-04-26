/* slap_secondary_headers.c
 *
 * Explicit big-endian serialisation/deserialisation for every
 * secondary header layout in SLAP v0.5.
 *
 * Design principle: the functions below contain no loops and no
 * dynamic allocation. Every byte offset is a compile-time constant
 * derived from the wire layout tables in the SLAP specification.
 * This makes them auditable line-by-line against the spec.
 */

#include "slap_secondary_headers.h"
#include "slap_types.h"
#include <string.h>

/* ----------------------------------------------------------------
 * Internal helpers — big-endian multi-byte field read/write.
 * Using explicit shifts instead of memcpy avoids endianness bugs
 * on both big- and little-endian MCUs.
 * ---------------------------------------------------------------- */

static void put_u16_be(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v);
}

static void put_u32_be(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >>  8);
    b[3] = (uint8_t)(v);
}

static uint16_t get_u16_be(const uint8_t *b)
{
    return (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
}

static uint32_t get_u32_be(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
         | ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

/* 5-byte (40-bit) big-endian write/read */
static void put_u40_be(uint8_t *b, uint64_t v)
{
    b[0] = (uint8_t)(v >> 32);
    b[1] = (uint8_t)(v >> 24);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >>  8);
    b[4] = (uint8_t)(v);
}

static uint64_t get_u40_be(const uint8_t *b)
{
    return ((uint64_t)b[0] << 32)
         | ((uint64_t)b[1] << 24)
         | ((uint64_t)b[2] << 16)
         | ((uint64_t)b[3] <<  8)
         | ((uint64_t)b[4]);
}

/* ----------------------------------------------------------------
 * Wire size lookup
 * ---------------------------------------------------------------- */

int slap_sec_wire_size(uint8_t svc, uint8_t msg)
{
    switch (svc) {

    case SLAP_SVC_ECHO:
        return 0;   /* Echo: no secondary header for any message type */

    case SLAP_SVC_HOUSEKEEPING:
        switch (msg) {
        case 1: return  2;  /* 1.1 */
        case 2: return 13;  /* 1.2 */
        case 3: return  2;  /* 1.3 */
        case 4: return  9;  /* 1.4 */
        default: return SLAP_ERR_INVALID;
        }

    case SLAP_SVC_TIME_MANAGEMENT:
        switch (msg) {
        case 1: return 4;   /* 2.1 Set rate */
        case 2: return 0;   /* 2.2 Time report — time is in data[] */
        case 3: return 0;   /* 2.3 Time request — no header */
        default: return SLAP_ERR_INVALID;
        }

    case SLAP_SVC_POSITION_MANAGEMENT:
        return 0;   /* 3.1 and 3.2 — position data is in data[] */

    case SLAP_SVC_TIME_BASED_SCHEDULING:
        switch (msg) {
        case 1:  return 0;  /* 4.1 Enable  */
        case 2:  return 0;  /* 4.2 Disable */
        case 3:  return 9;  /* 4.3 Insert  */
        case 4:  return 2;  /* 4.4 Receipt */
        case 5:  return 9;  /* 4.5 Update  */
        case 6:  return 0;  /* 4.6 Reset   */
        case 7:  return 0;  /* 4.7 Table size request */
        case 8:  return 4;  /* 4.8 Table size report  */
        case 9:  return 0;  /* 4.9 Table data request */
        case 10: return 0;  /* 4.10 Table data report — CSV in data[] */
        default: return SLAP_ERR_INVALID;
        }

    case SLAP_SVC_LARGE_PACKET_TRANSFER:
        switch (msg) {
        case 1: return 3;   /* 5.1 Data transfer request */
        case 2: return 5;   /* 5.2 File transfer ACK   — 40-bit file siz  */
        case 3: return 3;   /* 5.3 Segment request       */
        case 4: return 3;   /* 5.4 Segment report        */
        case 5: return 3;   /* 5.5 Completed transfer    */
        case 6: return 0;   /* 5.6 Completion ACK        */
        default: return SLAP_ERR_INVALID;
        }

    case SLAP_SVC_FILE_MANAGEMENT:
        switch (msg) {
        case 1:  return 2;  /* 6.1 ls size request    */
        case 2:  return 8;  /* 6.2 ls size report     */
        case 3:  return 2;  /* 6.3 ls records request */
        case 4:  return 0;  /* 6.4 ls records report — data[] only */
        case 5:  return 6;  /* 6.5 mv request  */
        case 6:  return 0;  /* 6.6 mv ACK      */
        case 7:  return 6;  /* 6.7 cp request  */
        case 8:  return 0;  /* 6.8 cp ACK      */
        case 9:  return 3;  /* 6.9 rm request  */
        case 10: return 0;  /* 6.10 rm ACK     */
        case 11: return 3;  /* 6.11 mkdir request */
        case 12: return 0;  /* 6.12 mkdir ACK     */
        default: return SLAP_ERR_INVALID;
        }

    case SLAP_SVC_TELECOMMAND:
        switch (msg) {
        case 1: return 2;   /* 7.1 TC send */
        case 2: return 2;   /* 7.2 TC ACK  */
        default: return SLAP_ERR_INVALID;
        }

    default:
        return SLAP_ERR_INVALID;
    }
}

/* ----------------------------------------------------------------
 * Pack (struct → wire bytes)
 * ---------------------------------------------------------------- */

int slap_sec_pack(uint8_t svc, uint8_t msg,
                  const slap_secondary_header_t *sec,
                  uint8_t *out, uint8_t max_len)
{
    int wire_len = slap_sec_wire_size(svc, msg);
    if (wire_len < 0)           return SLAP_ERR_INVALID;
    if (wire_len == 0)          return 0;
    if ((uint8_t)wire_len > max_len) return SLAP_ERR_OVERFLOW;

    switch (svc) {

    /* ---- Service 1: Housekeeping ---- */
    case SLAP_SVC_HOUSEKEEPING:

        if (msg == 1 || msg == 3) {
            /* 1.1 / 1.3: 2 bytes
             * byte[0]: [7]=hk_type [6]=historical [5:0]=0
             * byte[1]: param_id                                    */
            out[0] = (uint8_t)(
                ((sec->hk_req.hk_type    & 0x01U) << 7) |
                ((sec->hk_req.historical & 0x01U) << 6)
            );
            out[1] = sec->hk_req.param_id;
            return 2;
        }

        if (msg == 2) {
            /* 1.2: 13 bytes */
            out[0] = (uint8_t)(
                ((sec->hk_avail.hk_type    & 0x01U) << 7) |
                ((sec->hk_avail.historical & 0x01U) << 6)
            );
            out[1] = sec->hk_avail.param_id;
            put_u32_be(out + 2, sec->hk_avail.avail_size);
            memcpy(out + 6, sec->hk_avail.timestamp, 7);
            return 13;
        }

        if (msg == 4) {
            /* 1.4: 9 bytes */
            out[0] = (uint8_t)(
                ((sec->hk_send.hk_type    & 0x01U) << 7) |
                ((sec->hk_send.historical & 0x01U) << 6)
            );
            out[1] = sec->hk_send.param_id;
            memcpy(out + 2, sec->hk_send.timestamp, 7);
            return 9;
        }
        break;

    /* ---- Service 2: Time Management ---- */
    case SLAP_SVC_TIME_MANAGEMENT:
        if (msg == 1) {
            /* 2.1: 4 bytes */
            put_u32_be(out, sec->time_rate.generation_rate);
            return 4;
        }
        break;

    /* ---- Service 4: Time-Based Scheduling ---- */
    case SLAP_SVC_TIME_BASED_SCHEDULING:
        if (msg == 3) {
            /* 4.3: 9 bytes
             * byte[0..6]: release_time CUC 4.2
             * byte[7..8]: tc_length big-endian                     */
            memcpy(out, sec->sched_insert.release_time, 7);
            put_u16_be(out + 7, sec->sched_insert.tc_length);
            return 9;
        }
        if (msg == 4) {
            /* 4.4: 2 bytes */
            put_u16_be(out, sec->sched_receipt.entry_id);
            return 2;
        }
        if (msg == 5) {
            /* 4.5: 9 bytes
             * byte[0..1]: entry_id big-endian
             * byte[2..8]: release_time CUC 4.2                     */
            put_u16_be(out, sec->sched_update.entry_id);
            memcpy(out + 2, sec->sched_update.release_time, 7);
            return 9;
        }
        if (msg == 8) {
            /* 4.8: 4 bytes */
            put_u32_be(out, sec->sched_tbl_size.list_size);
            return 4;
        }
        break;

    /* ---- Service 5: Large Packet Transfer ---- */
    case SLAP_SVC_LARGE_PACKET_TRANSFER:
        if (msg == 1 || msg == 5) {
            /* 5.1 / 5.5: 3 bytes */
            put_u16_be(out,     sec->lpt_file_ref.path_length);
            out[2] = sec->lpt_file_ref.file_name_length;
            return 3;
        }
        if (msg == 2) {
            /* 5.2: 5 bytes */
            put_u40_be(out, sec->lpt_ack.file_size);
            return 5;
        }
        if (msg == 3 || msg == 4) {
            /* 5.3 / 5.4: 3 bytes
             * byte[0]: [7:6]=seq_flag [5:0]=seq_id[21:16]
             * byte[1]: seq_id[15:8]
             * byte[2]: seq_id[7:0]                                 */
            out[0] = (uint8_t)(
                ((sec->lpt_segment.seq_flag & 0x03U) << 6) |
                ((sec->lpt_segment.seq_id >> 16)     & 0x3FU)
            );
            out[1] = (uint8_t)(sec->lpt_segment.seq_id >> 8);
            out[2] = (uint8_t)(sec->lpt_segment.seq_id);
            return 3;
        }
        break;

    /* ---- Service 6: File Management ---- */
    case SLAP_SVC_FILE_MANAGEMENT:
        if (msg == 1 || msg == 3) {
            /* 6.1 / 6.3: 2 bytes */
            put_u16_be(out, sec->fm_path.path_length);
            return 2;
        }
        if (msg == 2) {
            /* 6.2: 8 bytes */
            put_u32_be(out,     sec->fm_ls_size.list_size);
            put_u16_be(out + 4, sec->fm_ls_size.num_directories);
            put_u16_be(out + 6, sec->fm_ls_size.num_files);
            return 8;
        }
        if (msg == 5 || msg == 7) {
            /* 6.5 / 6.7: 6 bytes */
            put_u16_be(out,     sec->fm_mv_cp.src_path_length);
            out[2] = sec->fm_mv_cp.src_file_name_length;
            put_u16_be(out + 3, sec->fm_mv_cp.dst_path_length);
            out[5] = sec->fm_mv_cp.dst_file_name_length;
            return 6;
        }
        if (msg == 9 || msg == 11) {
            /* 6.9 / 6.11: 3 bytes */
            put_u16_be(out, sec->fm_rm_mkdir.path_length);
            out[2] = sec->fm_rm_mkdir.name_length;
            return 3;
        }
        break;

    /* ---- Service 7: Telecommand ---- */
    case SLAP_SVC_TELECOMMAND:
        if (msg == 1 || msg == 2) {
            /* 7.1 / 7.2: 2 bytes */
            put_u16_be(out, sec->tc.tc_length);
            return 2;
        }
        break;
    }

    return SLAP_ERR_INVALID;
}

/* ----------------------------------------------------------------
 * Unpack (wire bytes → struct)
 * ---------------------------------------------------------------- */

int slap_sec_unpack(uint8_t svc, uint8_t msg,
                    const uint8_t *in, uint8_t in_len,
                    slap_secondary_header_t *sec)
{
    int wire_len = slap_sec_wire_size(svc, msg);
    if (wire_len < 0)                  return SLAP_ERR_INVALID;
    if (wire_len == 0)                 return 0;
    if (in_len < (uint8_t)wire_len)   return SLAP_ERR_INVALID;

    switch (svc) {

    case SLAP_SVC_HOUSEKEEPING:
        if (msg == 1 || msg == 3) {
            sec->hk_req.hk_type    = (in[0] >> 7) & 0x01U;
            sec->hk_req.historical = (in[0] >> 6) & 0x01U;
            sec->hk_req.param_id   =  in[1];
            return 2;
        }
        if (msg == 2) {
            sec->hk_avail.hk_type    = (in[0] >> 7) & 0x01U;
            sec->hk_avail.historical = (in[0] >> 6) & 0x01U;
            sec->hk_avail.param_id   =  in[1];
            sec->hk_avail.avail_size = get_u32_be(in + 2);
            memcpy(sec->hk_avail.timestamp, in + 6, 7);
            return 13;
        }
        if (msg == 4) {
            sec->hk_send.hk_type    = (in[0] >> 7) & 0x01U;
            sec->hk_send.historical = (in[0] >> 6) & 0x01U;
            sec->hk_send.param_id   =  in[1];
            memcpy(sec->hk_send.timestamp, in + 2, 7);
            return 9;
        }
        break;

    case SLAP_SVC_TIME_MANAGEMENT:
        if (msg == 1) {
            sec->time_rate.generation_rate = get_u32_be(in);
            return 4;
        }
        break;

    case SLAP_SVC_TIME_BASED_SCHEDULING:
        if (msg == 3) {
            memcpy(sec->sched_insert.release_time, in, 7);
            sec->sched_insert.tc_length = get_u16_be(in + 7);
            return 9;
        }
        if (msg == 4) {
            sec->sched_receipt.entry_id = get_u16_be(in);
            return 2;
        }
        if (msg == 5) {
            sec->sched_update.entry_id = get_u16_be(in);
            memcpy(sec->sched_update.release_time, in + 2, 7);
            return 9;
        }
        if (msg == 8) {
            sec->sched_tbl_size.list_size = get_u32_be(in);
            return 4;
        }
        break;

    case SLAP_SVC_LARGE_PACKET_TRANSFER:
        if (msg == 1 || msg == 5) {
            sec->lpt_file_ref.path_length      = get_u16_be(in);
            sec->lpt_file_ref.file_name_length = in[2];
            return 3;
        }
        if (msg == 2) {
            sec->lpt_ack.file_size = get_u40_be(in);
            return 5;
        }
        if (msg == 3 || msg == 4) {
            sec->lpt_segment.seq_flag = (in[0] >> 6) & 0x03U;
            sec->lpt_segment.seq_id   =
                ((uint32_t)(in[0] & 0x3FU) << 16) |
                ((uint32_t) in[1]           <<  8) |
                ((uint32_t) in[2]);
            return 3;
        }
        break;

    case SLAP_SVC_FILE_MANAGEMENT:
        if (msg == 1 || msg == 3) {
            sec->fm_path.path_length = get_u16_be(in);
            return 2;
        }
        if (msg == 2) {
            sec->fm_ls_size.list_size        = get_u32_be(in);
            sec->fm_ls_size.num_directories  = get_u16_be(in + 4);
            sec->fm_ls_size.num_files        = get_u16_be(in + 6);
            return 8;
        }
        if (msg == 5 || msg == 7) {
            sec->fm_mv_cp.src_path_length      = get_u16_be(in);
            sec->fm_mv_cp.src_file_name_length = in[2];
            sec->fm_mv_cp.dst_path_length      = get_u16_be(in + 3);
            sec->fm_mv_cp.dst_file_name_length = in[5];
            return 6;
        }
        if (msg == 9 || msg == 11) {
            sec->fm_rm_mkdir.path_length  = get_u16_be(in);
            sec->fm_rm_mkdir.name_length  = in[2];
            return 3;
        }
        break;

    case SLAP_SVC_TELECOMMAND:
        if (msg == 1 || msg == 2) {
            sec->tc.tc_length = get_u16_be(in);
            return 2;
        }
        break;
    }

    return SLAP_ERR_INVALID;
}