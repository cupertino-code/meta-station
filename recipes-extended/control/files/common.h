#ifndef _COMMON_H_INCLUDED
#define _COMMON_H_INCLUDED

#include "utils.h"
#include "shmem.h"

#define VERSION "1.0"

#define DEFAULT_PORT 5110

extern int verbose;

#ifndef BIT
#define BIT(n) (1U << (n))
#endif

#define SET_BIT(var, n)   ((var) |= BIT(n))
#define CLEAR_BIT(var, n) ((var) &= ~BIT(n))
#define CHECK_BIT(var, n) ((var) & BIT(n))

#define LOG1(format, ...) do {              \
        if (verbose)                        \
            printf(format, ##__VA_ARGS__);  \
    } while (0);

#define LOG2(format, ...) do {              \
        if (verbose >= 1)                   \
            printf(format, ##__VA_ARGS__);  \
    } while (0);

#endif // _COMMON_H_INCLUDED
