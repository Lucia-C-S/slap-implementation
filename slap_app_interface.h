/* contract boundary between the generic SLAP stack and BIXO-specific mission software.
Declare (not define) every application callback that a service calls externally.
All extern function declarations scattered across service files must be removed and
replaced with #include "slap_app_interface.h".
*/
#ifndef SLAP_APP_INTERFACE_H
#define SLAP_APP_INTERFACE_H

#include <stdint.h>

/* ---- SERVICE 1: HOUSEKEEPING ---- */
int hk_get_available_size(uint8_t hk_type, uint8_t historical,
                           uint8_t param_id, uint32_t *size_out);
int hk_read_data(uint8_t hk_type, uint8_t historical,
                 uint8_t param_id, uint8_t *buf,
                 uint16_t max_len, uint16_t *written);

/* ---- SERVICE 3: POSITION ---- */
int position_get(uint8_t *str_buf, uint16_t max_len, uint16_t *written);

/* ---- SERVICE 4: SCHEDULING ---- */
int tc_execute(const char *command, uint16_t len);

/* ---- SERVICE 5: LARGE PACKET TRANSFER ---- */
int lpt_get_file_size(const char *path, const char *name, uint32_t *size_out);
int lpt_read_segment(const char *path, const char *name,
                     uint32_t seq_id, uint8_t *buf,
                     uint16_t max_len, uint16_t *written);

/* ---- SERVICE 6: FILE MANAGEMENT ---- */
int fm_ls_size(const char *path, uint32_t *list_size,
               uint16_t *num_dirs, uint16_t *num_files);
int fm_ls_records(const char *path, uint8_t *buf,
                  uint16_t max_len, uint16_t *written);
int fm_mv(const char *src_path, const char *src_name,
          const char *dst_path, const char *dst_name);
int fm_cp(const char *src_path, const char *src_name,
          const char *dst_path, const char *dst_name);
int fm_rm(const char *path, const char *name);
int fm_mkdir(const char *path, const char *dir_name);

#endif /* SLAP_APP_INTERFACE_H */