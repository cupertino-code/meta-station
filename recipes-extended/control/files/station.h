#ifndef STATION_H_INCLUDED
#define STATION_H_INCLUDED

#include "shmem.h"

struct antenna_status {
    int updated;
    int angle;
    int power_status;
    int connect_status;
    int vbat;
    struct shared_memory shm;
};

extern struct antenna_status antenna_status;

#endif  // STATION_H_INCLUDED