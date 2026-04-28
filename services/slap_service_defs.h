/* slap_service_defs.h
 *
 * Message type constants for all SLAP services (§3, Table 1).
 *
 * WHY A SEPARATE HEADER:
 *   Constants defined only in a .c file are invisible to other
 *   translation units. The dispatcher, tests, and ground-side tools
 *   all need these values. Centralising them here prevents duplication
 *   and makes the spec → code mapping auditable in one place.
 *
 * NAMING CONVENTION:
 *   SLAP_MSG_<SERVICE_ABBREVIATION>_<DESCRIPTION>
 *   Example: SLAP_MSG_HK_AVAIL_REQ = Housekeeping Available Request
 */

#ifndef SLAP_SERVICE_DEFS_H
#define SLAP_SERVICE_DEFS_H

/* ================================================================
 * SERVICE 0 — ECHO (0x00)
 * ================================================================ */
#define SLAP_MSG_ECHO_PING          1U   /* 0.1 requesting node → target */
#define SLAP_MSG_ECHO_PONG          2U   /* 0.2 target → requesting node */

/* ================================================================
 * SERVICE 1 — HOUSEKEEPING (0x01)
 * ================================================================ */
#define SLAP_MSG_HK_AVAIL_REQ       1U   /* 1.1 query data availability  */
#define SLAP_MSG_HK_AVAIL_RESP      2U   /* 1.2 report data availability */
#define SLAP_MSG_HK_PKT_REQ         3U   /* 1.3 request one data segment */
#define SLAP_MSG_HK_PKT_SEND        4U   /* 1.4 deliver one data segment */

/* HK type field values (§3.1.3) */
#define SLAP_HK_TYPE_TELEMETRY      0U   /* voltage, current, temp, etc. */
#define SLAP_HK_TYPE_LOG            1U   /* diagnostic / failure log     */

/* Historical field values (§3.1.3) */
#define SLAP_HK_IMMEDIATE           0U   /* real-time, not stored        */
#define SLAP_HK_HISTORICAL          1U   /* from circular buffer         */

/* ================================================================
 * SERVICE 2 — TIME MANAGEMENT (0x02)
 * ================================================================ */
#define SLAP_MSG_TM_SET_RATE        1U   /* 2.1 configure broadcast rate */
#define SLAP_MSG_TM_TIME_REPORT     2U   /* 2.2 on-board time report     */
#define SLAP_MSG_TM_TIME_REQ        3U   /* 2.3 request current time     */

/* ================================================================
 * SERVICE 3 — POSITION MANAGEMENT (0x03)
 * ================================================================ */
#define SLAP_MSG_POS_REQUEST        1U   /* 3.1 request position report  */
#define SLAP_MSG_POS_REPORT         2U   /* 3.2 position + velocity data */

/* Size of position payload in data[] (6 × float32, §3.3.3 + Item 0.2) */
#define SLAP_POS_STATE_VECTOR_BYTES 24U  /* 6 × 4 bytes, big-endian      */
/* Total data[] size for 3.2: 7 (CUC timestamp) + 24 (state vector) = 31 */
#define SLAP_POS_DATA_TOTAL_BYTES   31U

/* ================================================================
 * SERVICE 4 — TIME-BASED SCHEDULING (0x04)
 * ================================================================ */
#define SLAP_MSG_SCHED_ENABLE       1U   /* 4.1  enable scheduler        */
#define SLAP_MSG_SCHED_DISABLE      2U   /* 4.2  disable scheduler       */
#define SLAP_MSG_SCHED_INSERT       3U   /* 4.3  insert telecommand      */
#define SLAP_MSG_SCHED_INSERT_RESP  4U   /* 4.4  insert receipt          */
#define SLAP_MSG_SCHED_UPDATE       5U   /* 4.5  update release time     */
#define SLAP_MSG_SCHED_RESET        6U   /* 4.6  reset schedule table    */
#define SLAP_MSG_SCHED_TBL_SZ_REQ   7U   /* 4.7  table size request      */
#define SLAP_MSG_SCHED_TBL_SZ_RESP  8U   /* 4.8  table size report       */
#define SLAP_MSG_SCHED_TBL_DATA_REQ 9U   /* 4.9  table data request      */
#define SLAP_MSG_SCHED_TBL_DATA_RESP 10U /* 4.10 table data report (CSV) */

/* Schedule table capacity — tune to available RAM */
#define SLAP_SCHED_MAX_ENTRIES      16U
#define SLAP_SCHED_TC_MAX_LEN       128U /* max UTF-8 command string bytes */

/* ================================================================
 * SERVICE 5 — LARGE PACKET TRANSFER (0x05)
 * ================================================================ */
#define SLAP_MSG_LPT_DATA_REQ       1U   /* 5.1 request file transfer    */
#define SLAP_MSG_LPT_ACK            2U   /* 5.2 ACK + file size (40-bit) */
#define SLAP_MSG_LPT_SEG_REQ        3U   /* 5.3 request one segment      */
#define SLAP_MSG_LPT_SEG_REPORT     4U   /* 5.4 deliver one segment      */
#define SLAP_MSG_LPT_COMPLETE       5U   /* 5.5 transfer complete signal */
#define SLAP_MSG_LPT_COMPLETE_ACK   6U   /* 5.6 completion acknowledgement */

/* Sequence flag values for seg_flag field (§3.5.3) */
#define SLAP_SEQ_CONTINUATION       0x00U /* middle segment               */
#define SLAP_SEQ_FIRST              0x01U /* first segment of the file    */
#define SLAP_SEQ_LAST               0x02U /* last segment of the file     */
#define SLAP_SEQ_UNSEGMENTED        0x03U /* entire file in one packet    */

/* Max file size representable in 5.2 secondary header (40-bit field):
 * 2^40 − 1 = 1,099,511,627,775 bytes ≈ 1 TB                           */
#define SLAP_LPT_MAX_FILE_SIZE      0xFFFFFFFFFFULL

/* Max data bytes in a 5.4 segment (specific to this service):
 * 2048 - 4 (primary) - 3 (sec header 5.4) - 2 (ECF) = 2039 bytes      */
#define SLAP_LPT_SEGMENT_MAX_DATA   2039U

/* Max path / filename buffer sizes for safe extraction */
#define SLAP_LPT_MAX_PATH_LEN       255U
#define SLAP_LPT_MAX_NAME_LEN       127U

/* ================================================================
 * SERVICE 6 — FILE MANAGEMENT (0x06)
 * ================================================================ */
#define SLAP_MSG_FM_LS_SIZE_REQ     1U   /* 6.1  ls size request         */
#define SLAP_MSG_FM_LS_SIZE_RESP    2U   /* 6.2  ls size report          */
#define SLAP_MSG_FM_LS_REC_REQ      3U   /* 6.3  ls records request      */
#define SLAP_MSG_FM_LS_REC_RESP     4U   /* 6.4  ls records report       */
#define SLAP_MSG_FM_MV_REQ          5U   /* 6.5  mv request              */
#define SLAP_MSG_FM_MV_ACK          6U   /* 6.6  mv acknowledgement      */
#define SLAP_MSG_FM_CP_REQ          7U   /* 6.7  cp request              */
#define SLAP_MSG_FM_CP_ACK          8U   /* 6.8  cp acknowledgement      */
#define SLAP_MSG_FM_RM_REQ          9U   /* 6.9  rm request              */
#define SLAP_MSG_FM_RM_ACK          10U  /* 6.10 rm acknowledgement      */
#define SLAP_MSG_FM_MKDIR_REQ       11U  /* 6.11 mkdir request           */
#define SLAP_MSG_FM_MKDIR_ACK       12U  /* 6.12 mkdir acknowledgement   */

/* Entry type field in 6.4 ls records report data (§3.6.3) */
#define SLAP_FM_ENTRY_DIRECTORY     0U
#define SLAP_FM_ENTRY_FILE          1U

/* Max path / name buffer sizes for safe string extraction */
#define SLAP_FM_MAX_PATH_LEN        255U
#define SLAP_FM_MAX_NAME_LEN        127U

/* ================================================================
 * SERVICE 7 — TELECOMMAND (0x07)
 * ================================================================ */
#define SLAP_MSG_TC_SEND            1U   /* 7.1 command to spacecraft     */
#define SLAP_MSG_TC_ACK             2U   /* 7.2 command acknowledgement   */

#endif /* SLAP_SERVICE_DEFS_H */