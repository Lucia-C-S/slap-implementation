/* contract boundary between the generic SLAP stack and BIXO-specific mission software.
Declare (not define) every application callback that a service calls externally.
All extern function declarations scattered across service files must be removed and
replaced with #include "slap_app_interface.h".
*/
/* slap_app_interface.h
 *
 * SLAP Application Interface — mission callback contract.
 *
 * This header declares EVERY function that the SLAP protocol stack
 * calls into the BIXO mission software. It is the single, explicit
 * boundary between the generic reusable protocol stack and the
 * mission-specific application code.
 *
 * ALL functions declared here MUST be implemented in:
 *   app/slap_app_callbacks.c
 *
 * Implementation rules:
 *   - Return SLAP_OK (0) on success, negative SLAP_ERR_xxx on failure.
 *   - Never call osal_malloc() or malloc() inside callbacks.
 *   - Never block indefinitely — use a timeout or return immediately.
 *   - Callbacks may be called from the SLAP receive task context
 *     (FreeRTOS). If they access shared hardware resources, protect
 *     them with the appropriate mutex in slap_app_callbacks.c.
 *
 * Position encoding (Item 0.2 resolved):
 *   CCSDS 502.0-B-2 OPM state vector, ECI J2000 frame.
 *   6 x IEEE-754 float32 big-endian = 24 bytes.
 *   Layout: [X_km(4)][Y_km(4)][Z_km(4)][Vx_kms(4)][Vy_kms(4)][Vz_kms(4)]
 */

#ifndef SLAP_APP_INTERFACE_H
#define SLAP_APP_INTERFACE_H

#include <stdint.h>
#include "slap_types.h"

/* ================================================================
 * TIME
 * ================================================================ */

/**
 * Return current seconds since the CUC epoch (1958-01-01 TAI).
 * TAI offset from Unix epoch: 378,691,200 seconds.
 *
 * Implementation: read STM32 HAL RTC and add TAI offset.
 * Must be callable from task context (not ISR-safe required).
 *
 * @return uint32_t seconds since 1958-01-01 TAI
 */
uint32_t rtc_get_seconds(void);

/* ================================================================
 * HOUSEKEEPING — Service 1
 * ================================================================ */

/**
 * Query the number of bytes of housekeeping/diagnostic data
 * currently available for the specified parameter.
 *
 * @param hk_type    0 = telemetry, 1 = diagnostic log
 * @param historical 0 = immediate (real-time), 1 = stored buffer
 * @param param_id   0 = all parameters, non-zero = specific parameter
 * @param size_out   output: available data size in bytes
 * @return SLAP_OK or SLAP_ERR_NODATA
 */
int hk_get_available_size(uint8_t  hk_type,
                           uint8_t  historical,
                           uint8_t  param_id,
                           uint32_t *size_out);

/**
 * Read serialised housekeeping or diagnostic data into buf.
 *
 * The data format (which parameters, byte order, encoding) is
 * defined by the BIXO mission ICD — outside the SLAP spec.
 * Populate buf with as many bytes as fit in max_len.
 *
 * @param hk_type    0 = telemetry, 1 = diagnostic log
 * @param historical 0 = immediate, 1 = historical circular buffer
 * @param param_id   0 = all parameters, non-zero = specific set
 * @param buf        output buffer
 * @param max_len    maximum bytes to write (= SLAP_MAX_DATA)
 * @param written    output: actual bytes written to buf
 * @return SLAP_OK or SLAP_ERR_NODATA
 */
int hk_read_data(uint8_t   hk_type,
                  uint8_t   historical,
                  uint8_t   param_id,
                  uint8_t  *buf,
                  uint16_t  max_len,
                  uint16_t *written);

/* ================================================================
 * POSITION MANAGEMENT — Service 3
 * ================================================================ */

/**
 * Return the current spacecraft ECI state vector in the SLAP
 * position wire format (CCSDS 502.0-B-2 compact binary).
 *
 * Wire format (24 bytes, all fields IEEE-754 float32 big-endian):
 *   bytes [0..3]   X position [km]
 *   bytes [4..7]   Y position [km]
 *   bytes [8..11]  Z position [km]
 *   bytes [12..15] Vx velocity [km/s]
 *   bytes [16..19] Vy velocity [km/s]
 *   bytes [20..23] Vz velocity [km/s]
 *
 * Source: onboard SGP4 propagator or ADCS subsystem.
 *
 * @param buf     output buffer (must be >= 24 bytes)
 * @param max_len available bytes in buf
 * @param written output: bytes written (always 24 on success)
 * @return SLAP_OK or SLAP_ERR_NODATA if propagator not ready
 */
int position_get(uint8_t  *buf,
                  uint16_t  max_len,
                  uint16_t *written);

/* ================================================================
 * TIME-BASED SCHEDULING — Service 4
 * ================================================================ */

/**
 * Execute a telecommand string immediately.
 *
 * Called by slap_scheduling_tick() when a scheduled telecommand's
 * release time is reached. Also used by Service 7 for real-time
 * execution.
 *
 * The command string format is mission-defined. It should be the
 * same format accepted by Service 7's direct telecommand interface.
 *
 * @param command  null-terminated UTF-8 command string
 * @param len      byte length of command (not including null)
 * @return SLAP_OK if command was recognised and dispatched,
 *         SLAP_ERR_INVALID if command is malformed or unknown
 */
int tc_execute(const char *command, uint16_t len);

/* ================================================================
 * LARGE PACKET TRANSFER — Service 5
 * ================================================================ */

/**
 * Return the size in bytes of the file at the given path.
 *
 * @param path     null-terminated UTF-8 repository path
 * @param name     null-terminated UTF-8 file name
 * @param size_out output: file size in bytes (uint64_t for >4 GB)
 * @return SLAP_OK or SLAP_ERR_NODATA if file not found
 */
int lpt_get_file_size(const char *path,
                       const char *name,
                       uint64_t   *size_out);

/**
 * Read one segment from the file into buf.
 *
 * Segment numbering starts at 1. Segment seq_id maps to file offset:
 *   offset = (seq_id - 1) * SLAP_MAX_DATA
 * The last segment may be shorter than SLAP_MAX_DATA bytes.
 *
 * @param path    null-terminated UTF-8 repository path
 * @param name    null-terminated UTF-8 file name
 * @param seq_id  1-based segment index
 * @param buf     output buffer (SLAP_MAX_DATA bytes available)
 * @param max_len maximum bytes to write
 * @param written output: actual bytes written (< max_len for last seg)
 * @return SLAP_OK or SLAP_ERR_INVALID on seek/read failure
 */
int lpt_read_segment(const char *path,
                      const char *name,
                      uint32_t    seq_id,
                      uint8_t    *buf,
                      uint16_t    max_len,
                      uint16_t   *written);

/* ================================================================
 * FILE MANAGEMENT — Service 6
 * ================================================================ */

/**
 * Return the total size of directory listing data plus counts.
 *
 * The list_size value is used by the ground station to compute
 * how many 6.3/6.4 iterations are required.
 *
 * @param path       null-terminated UTF-8 path to query
 * @param list_size  output: byte size of the full ls data
 * @param num_dirs   output: count of directories in path
 * @param num_files  output: count of files in path
 * @return SLAP_OK or SLAP_ERR_NODATA if path does not exist
 */
int fm_ls_size(const char *path,
                uint32_t   *list_size,
                uint16_t   *num_dirs,
                uint16_t   *num_files);

/**
 * Write serialised directory records into buf.
 *
 * Each record is packed as:
 *   entry_type(16b) | entry_size(64b) | name_length(8b) | name(Nb)
 * All multi-byte integers are big-endian.
 * entry_type: 0 = directory, 1 = file. entry_size = 0 for directories.
 *
 * @param path    null-terminated UTF-8 path to list
 * @param buf     output buffer
 * @param max_len maximum bytes to write (= SLAP_MAX_DATA)
 * @param written output: actual bytes written
 * @return SLAP_OK or SLAP_ERR_NODATA
 */
int fm_ls_records(const char *path,
                   uint8_t    *buf,
                   uint16_t    max_len,
                   uint16_t   *written);

/**
 * Move a file from source to destination with optional rename.
 *
 * @param src_path  null-terminated source repository path
 * @param src_name  null-terminated source file name
 * @param dst_path  null-terminated destination repository path
 * @param dst_name  null-terminated destination file name
 * @return SLAP_OK or SLAP_ERR_INVALID on failure
 */
int fm_mv(const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name);

/**
 * Copy a file from source to destination.
 * Parameters identical to fm_mv().
 */
int fm_cp(const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name);

/**
 * Delete a file or directory identified by path + name.
 *
 * @param path  null-terminated repository path
 * @param name  null-terminated file or directory name
 * @return SLAP_OK or SLAP_ERR_INVALID if not found or protected
 */
int fm_rm(const char *path, const char *name);

/**
 * Create a new directory at path/dir_name.
 *
 * @param path      null-terminated parent repository path
 * @param dir_name  null-terminated new directory name
 * @return SLAP_OK or SLAP_ERR_INVALID if path does not exist
 */
int fm_mkdir(const char *path, const char *dir_name);

#endif /* SLAP_APP_INTERFACE_H */