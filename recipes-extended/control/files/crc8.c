#include <stdint.h>

static uint8_t crc8_dvb_s2(uint8_t crc, uint8_t a)
{
	crc = crc ^ a;
	for (int i=0; i < 8; i++)
		if (crc & 0x80)
			crc = (crc << 1) ^ 0xD5;
		else
			crc = crc << 1;
	return crc & 0xFF;
}

uint8_t crc8_data(const uint8_t *data, int len)
{
	uint8_t crc = 0;
	for (volatile int i=0; i<len; i++) 
		crc = crc8_dvb_s2(crc, data[i]);
	return crc;
}
