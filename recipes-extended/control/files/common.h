#ifndef _COMMON_H_INCLUDED
#define _COMMON_H_INCLUDED

#define VERSION "1.0"

#define DEFAULT_PORT 5110

extern int verbose;

#ifndef BIT
#define BIT(n) (1U << (n))
#endif

struct antenna_status {
    int updated;
    int angle;
    int power_status;
    int connect_status;
};

extern struct antenna_status antenna_status;

void dump(uint8_t *buf, int len);
uint8_t crc8_data(const uint8_t *data, int len);

#define MAYBE_UNUSED    __attribute__((unused))
#define PACKED __attribute__((packed))

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define _unlikely(cond) __builtin_expect ((cond), 0)
#define _likely(cond)   __builtin_expect ((cond), 1)

#define LOG1(format, ...) do {              \
        if (verbose)                        \
            printf(format, ##__VA_ARGS__);  \
    } while (0);

#define LOG2(format, ...) do {              \
        if (verbose >= 1)                   \
            printf(format, ##__VA_ARGS__);  \
    } while (0);

#endif // _COMMON_H_INCLUDED