// Hardware Abstraction Layer (HAL)
// The protocol must not depend on specific hardware.

#ifndef HAL_H
#define HAL_H

#include <stdint.h>

void hal_init(void);

int hal_send(uint8_t *data, uint16_t length);

int hal_receive(uint8_t *buffer, uint16_t max_len);

#endif