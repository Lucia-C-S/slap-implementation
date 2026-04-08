#include "slap_packet.h"

#define CRC_POLY 0x1021

uint16_t slap_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0x0000;

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC_POLY;
            else
                crc <<= 1;
        }
    }

    return crc;
    //based on polynomial x^16 + x^12 + x^5 + 1
}