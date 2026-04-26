/*Implement every function declared in slap_app_interface.h. Use this stub pattern
until real drivers are connected:
Apply the same #warning + stub pattern to every callback.*/
#include <stdint.h>
#include "../slap_types.h"
#warning "hk_read_data() is a stub — connect to your sensor driver"
int hk_read_data(uint8_t hk_type, uint8_t historical,
                 uint8_t param_id, uint8_t *buf,
                 uint16_t max_len, uint16_t *written)
{
    (void)hk_type; (void)historical; (void)param_id;
    (void)buf; (void)max_len;
    *written = 0;
    return SLAP_ERR_NODATA; /* TODO: connect to sensor/log driver */
}

