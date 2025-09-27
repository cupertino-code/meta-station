#ifndef _UTILS_H_INCLUDED
#define _UTILS_H_INCLUDED

#include <stdint.h>

extern int verbose;

void dump(const char *title, uint8_t *buf, int len);                                                                                                                                                                       
uint8_t crc8_data(const uint8_t *data, uint8_t len);

#define MAYBE_UNUSED    __attribute__((unused))
#define PACKED __attribute__((packed))

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define _unlikely(cond) __builtin_expect ((cond), 0)
#define _likely(cond)   __builtin_expect ((cond), 1)

#endif // _UTILS_H_INCLUDED
