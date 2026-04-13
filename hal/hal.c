#include "hal.h"

/* Replace with real driver */

void hal_init(void)
{
    // inicialización de la conexión csp
}

int hal_send(uint8_t *data, uint16_t length)
{
    for(int i = 0; i < length; i++)
    {
        /* send byte  to cps_send */
    }
    return length;
}

int hal_receive(uint8_t *buffer, uint16_t max_len)
{
    int count = 0;

    while(count < max_len)
    {
        /* read byte if available */
        break;
    }

    return count;
}
