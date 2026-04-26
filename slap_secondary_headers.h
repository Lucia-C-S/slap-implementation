/* slap_secondary_headers.h
 *
 * One struct per distinct secondary header layout in SLAP v0.5.
 * Fields are stored UNPACKED (sub-byte fields occupy a full byte)
 * for CPU-friendly access. Serialisation packs them to minimum
 * wire bytes. This mirrors the same philosophy as the primary header.
 *
 * Naming convention: slap_sec_<service>_<msgtype>_t
 * Where the layout is shared across message types (e.g. 1.1 and 1.3
 * are identical), a single struct covers both.
 */

#ifndef SLAP_SECONDARY_HEADERS_H
#define SLAP_SECONDARY_HEADERS_H

#include <stdint.h>

/* ----------------------------------------------------------------
 * SERVICE 1 — HOUSEKEEPING
 * ---------------------------------------------------------------- */

/* Used by: 1.1 (Available request) and 1.3 (Packet request)
 * Wire layout: HK_type(1b) | Historical(1b) | Param_ID(8b)
 * Wire size: 2 bytes (10 bits packed into 2 bytes, 6 bits wasted)
 *
 * Packing: byte[0] = [7]=HK_type [6]=Historical [5:0]=unused
 *          byte[1] = Param_ID
 */
typedef struct {
    uint8_t hk_type;      /* 0=telemetry  1=log                      */
    uint8_t historical;   /* 0=immediate  1=historical                */
    uint8_t param_id;     /* 0=all params, non-zero=specific param    */
} slap_sec_hk_req_t;      /* 1.1 and 1.3 share this layout           */

/* Used by: 1.2 (Available packet report)
 * Wire layout:
 *   HK_type(1b) | Historical(1b) | Param_ID(8b) |
 *   Avail_size(32b) | Timestamp(56b)
 * Wire size: 13 bytes
 *
 * Packing: byte[0]    = [7]=HK_type [6]=Historical [5:0]=unused
 *          byte[1]    = Param_ID
 *          byte[2..5] = Avail_size big-endian uint32
 *          byte[6..12]= Timestamp CUC 4.2 (4 coarse + 3 fine)
 */
typedef struct {
    uint8_t  hk_type;
    uint8_t  historical;
    uint8_t  param_id;
    uint32_t avail_size;    /* bytes of HK data available for downlink */
    uint8_t  timestamp[7];  /* CUC 4.2: [0..3]=coarse [4..6]=fine     */
} slap_sec_hk_avail_report_t;  /* 1.2                                  */

/* Used by: 1.4 (Packet sending)
 * Wire layout: HK_type(1b) | Historical(1b) | Param_ID(8b) | Timestamp(56b)
 * Wire size: 9 bytes
 *
 * Packing: byte[0]   = [7]=HK_type [6]=Historical [5:0]=unused
 *          byte[1]   = Param_ID
 *          byte[2..8]= Timestamp CUC 4.2
 */
typedef struct {
    uint8_t hk_type;
    uint8_t historical;
    uint8_t param_id;
    uint8_t timestamp[7];
} slap_sec_hk_send_t;      /* 1.4                                     */

/* ----------------------------------------------------------------
 * SERVICE 2 — TIME MANAGEMENT
 * ---------------------------------------------------------------- */

/* Used by: 2.1 (Set time report generation rate)
 * Wire layout: Generation_rate(32b)
 * Wire size: 4 bytes
 *
 * A value of 0 disables autonomous time reporting.
 * Value is in seconds (uint32 → max ~49710 days).
 */
typedef struct {
    uint32_t generation_rate;  /* seconds between autonomous 2.2 reports */
} slap_sec_time_set_rate_t;    /* 2.1                                    */

/* ----------------------------------------------------------------
 * SERVICE 4 — TIME-BASED SCHEDULING
 * ---------------------------------------------------------------- */

/* Used by: 4.3 (Insert telecommand)
 * Wire layout: Release_time(56b) | TC_length(16b)
 * Wire size: 9 bytes
 *
 * TC_length indicates byte count of the UTF-8 TC string in data[].
 * Release_time is CUC 4.2.
 */
typedef struct {
    uint8_t  release_time[7]; /* CUC 4.2 execution time               */
    uint16_t tc_length;       /* byte length of TC string in data[]   */
} slap_sec_sched_insert_t;    /* 4.3                                   */

/* Used by: 4.4 (Received scheduled TC report)
 * Wire layout: Entry_ID(16b)
 * Wire size: 2 bytes
 */
typedef struct {
    uint16_t entry_id;        /* on-board assigned ID, starts at 1    */
} slap_sec_sched_receipt_t;   /* 4.4                                   */

/* Used by: 4.5 (Update release time)
 * Wire layout: Entry_ID(16b) | Release_time(56b)
 * Wire size: 9 bytes
 *
 * Release_time = 0 cancels the telecommand.
 */
typedef struct {
    uint16_t entry_id;
    uint8_t  release_time[7];
} slap_sec_sched_update_t;    /* 4.5                                   */

/* Used by: 4.8 (Schedule table size report)
 * Wire layout: List_size(32b)
 * Wire size: 4 bytes
 */
typedef struct {
    uint32_t list_size;       /* byte length of full CSV schedule string */
} slap_sec_sched_table_size_t; /* 4.8                                  */

/* ----------------------------------------------------------------
 * SERVICE 5 — LARGE PACKET TRANSFER
 * ---------------------------------------------------------------- */

/* Used by: 5.1 (Data transfer request) and 5.5 (Completed transfer)
 * Wire layout: Path_length(16b) | File_name_length(8b)
 * Wire size: 3 bytes
 *
 * The actual path and file name strings follow in data[].
 */
typedef struct {
    uint16_t path_length;      /* byte length of path string in data[]  */
    uint8_t  file_name_length; /* byte length of file name in data[]    */
} slap_sec_lpt_file_ref_t;     /* 5.1 and 5.5 share this layout         */

/* Used by: 5.2 (File transfer ACK report)
 * Wire layout: File_size(40b) — 5 bytes big-endian
 * Wire size: 5 bytes
 *
 * Derivation: max file = 2^22 segments × 2039 bytes = 8,552,171,456 bytes
 * Requires 33 bits → 5 bytes (40 bits) minimum byte-aligned.
 * Stored as uint64_t in RAM (no C type for 40-bit integers).
 * Maximum representable: 2^40 − 1 ≈ 1 TB.
 */
typedef struct {
    uint64_t file_size;   /* wire: 5 bytes big-endian (bits [39:0])  */
} slap_sec_lpt_ack_t;         /* 5.2                                    */

/* Used by: 5.3 (Segment request) and 5.4 (Segment report)
 * Wire layout: Seq_flag(2b) | Seq_ID(22b)
 * Wire size: 3 bytes (24 bits exactly)
 *
 * Packing: byte[0] = [7:6]=Seq_flag [5:0]=Seq_ID[21:16]
 *          byte[1] = Seq_ID[15:8]
 *          byte[2] = Seq_ID[7:0]
 *
 * Seq_flag encoding (per SLAP spec §3.5.3):
 *   0b00 = continuation segment
 *   0b01 = first segment
 *   0b10 = last segment
 *   0b11 = unsegmented (entire file fits in one packet)
 *
 * Seq_ID starts at 1, increments per segment.
 * Maximum segments per transfer: 2^22 = 4,194,304.
 * At 2039 bytes per segment: max file = ~8.55 GB (per spec §3.5.3).
 */
typedef struct {
    uint8_t  seq_flag;  /* 2-bit: SEQ_FIRST / SEQ_LAST / etc.          */
    uint32_t seq_id;    /* 22-bit sequence number (stored as 32-bit)   */
} slap_sec_lpt_segment_t;  /* 5.3 and 5.4 share this layout            */

/* Seq_flag named constants */
#define SLAP_SEQ_CONTINUATION 0x00U
#define SLAP_SEQ_FIRST        0x01U
#define SLAP_SEQ_LAST         0x02U
#define SLAP_SEQ_UNSEGMENTED  0x03U

/* ----------------------------------------------------------------
 * SERVICE 6 — FILE MANAGEMENT
 * ---------------------------------------------------------------- */

/* Used by: 6.1 (ls size request) and 6.3 (ls records request)
 * Wire layout: Path_length(16b)
 * Wire size: 2 bytes
 */
typedef struct {
    uint16_t path_length;
} slap_sec_fm_path_only_t;     /* 6.1 and 6.3                          */

/* Used by: 6.2 (ls size report)
 * Wire layout: List_size(32b) | Num_directories(16b) | Num_files(16b)
 * Wire size: 8 bytes  ← this is the maximum secondary header in SLAP
 */
typedef struct {
    uint32_t list_size;       /* byte size of the complete ls listing   */
    uint16_t num_directories; /* count of directories in the path       */
    uint16_t num_files;       /* count of files in the path             */
} slap_sec_fm_ls_size_report_t; /* 6.2                                  */

/* Used by: 6.5 (mv request) and 6.7 (cp request)
 * Wire layout:
 *   Src_path_length(16b) | Src_file_name_length(8b) |
 *   Dst_path_length(16b) | Dst_file_name_length(8b)
 * Wire size: 6 bytes
 *
 * Source and destination path+name strings follow in data[].
 */
typedef struct {
    uint16_t src_path_length;
    uint8_t  src_file_name_length;
    uint16_t dst_path_length;
    uint8_t  dst_file_name_length;
} slap_sec_fm_mv_cp_t;         /* 6.5 and 6.7 share this layout        */

/* Used by: 6.9 (rm request) and 6.11 (mkdir request)
 * Wire layout: Path_length(16b) | Name_length(8b)
 * Wire size: 3 bytes
 */
typedef struct {
    uint16_t path_length;
    uint8_t  name_length;      /* file/folder/directory name length    */
} slap_sec_fm_rm_mkdir_t;      /* 6.9 and 6.11 share this layout       */

/* ----------------------------------------------------------------
 * SERVICE 7 — TELECOMMAND
 * ---------------------------------------------------------------- */

/* Used by: 7.1 (TC send) and 7.2 (TC ack)
 * Wire layout: TC_length(16b)
 * Wire size: 2 bytes
 */
typedef struct {
    uint16_t tc_length;        /* byte length of UTF-8 TC string       */
} slap_sec_tc_t;               /* 7.1 and 7.2                          */

/* ----------------------------------------------------------------
 * UNION — the in-memory secondary header
 *
 * The union's total size equals its largest member:
 *   slap_sec_hk_avail_report_t = 13 bytes (largest)
 *
 * Usage: populate the appropriate named member, then call
 * slap_sec_pack() to serialise it to the wire. On receive, call
 * slap_sec_unpack() after decoding the service and message type.
 * ---------------------------------------------------------------- */
typedef union {
    slap_sec_hk_req_t            hk_req;         /* 1.1, 1.3  2 bytes */
    slap_sec_hk_avail_report_t   hk_avail;       /* 1.2      13 bytes */
    slap_sec_hk_send_t           hk_send;        /* 1.4       9 bytes */
    slap_sec_time_set_rate_t     time_rate;      /* 2.1       4 bytes */
    slap_sec_sched_insert_t      sched_insert;   /* 4.3       9 bytes */
    slap_sec_sched_receipt_t     sched_receipt;  /* 4.4       2 bytes */
    slap_sec_sched_update_t      sched_update;   /* 4.5       9 bytes */
    slap_sec_sched_table_size_t  sched_tbl_size; /* 4.8       4 bytes */
    slap_sec_lpt_file_ref_t      lpt_file_ref;   /* 5.1, 5.5  3 bytes */
    slap_sec_lpt_ack_t           lpt_ack;        /* 5.2       2 bytes */
    slap_sec_lpt_segment_t       lpt_segment;    /* 5.3, 5.4  3 bytes */
    slap_sec_fm_path_only_t      fm_path;        /* 6.1, 6.3  2 bytes */
    slap_sec_fm_ls_size_report_t fm_ls_size;     /* 6.2       8 bytes */
    slap_sec_fm_mv_cp_t          fm_mv_cp;       /* 6.5, 6.7  6 bytes */
    slap_sec_fm_rm_mkdir_t       fm_rm_mkdir;    /* 6.9, 6.11 3 bytes */
    slap_sec_tc_t                tc;             /* 7.1, 7.2  2 bytes */
} slap_secondary_header_t;

/* ----------------------------------------------------------------
 * SERIALISATION API
 * ---------------------------------------------------------------- */

/**
 * Serialise a secondary header to the wire.
 * @param svc       service type  (SLAP_SVC_xxx)
 * @param msg       message type
 * @param sec       populated union (input)
 * @param out       destination buffer (output)
 * @param max_len   size of out in bytes
 * @return number of bytes written, or SLAP_ERR_INVALID
 */
int slap_sec_pack(uint8_t svc, uint8_t msg,
                  const slap_secondary_header_t *sec,
                  uint8_t *out, uint8_t max_len);

/**
 * Deserialise a secondary header from the wire.
 * @param svc       service type (from decoded primary header)
 * @param msg       message type (from decoded primary header)
 * @param in        source buffer (wire bytes)
 * @param in_len    available bytes in in[]
 * @param sec       populated union (output)
 * @return number of bytes consumed, or SLAP_ERR_INVALID
 */
int slap_sec_unpack(uint8_t svc, uint8_t msg,
                    const uint8_t *in, uint8_t in_len,
                    slap_secondary_header_t *sec);

/**
 * Return the wire byte count of the secondary header for this
 * service/message combination.
 * Returns 0 for message types with no secondary header.
 * Returns SLAP_ERR_INVALID for unknown combinations.
 */
int slap_sec_wire_size(uint8_t svc, uint8_t msg);

#endif /* SLAP_SECONDARY_HEADERS_H */