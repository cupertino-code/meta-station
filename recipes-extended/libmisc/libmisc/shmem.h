#ifndef _SHMEM_H_INCLUDED
#define _SHMEM_H_INCLUDED

#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "crsf_protocol.h"

struct shared_buffer {
    uint8_t flag;
    int aux;
    crsf_channels_t channels;
    uint8_t num_channels;
};

#define DEFAULT_SHARED_NAME "/channel_data"

struct shared_memory {
    int shm_fd;
    void *ptr;
};

#define SHM_SIZE 4096

int init_shared(const char *name, struct shared_memory *shm);
int deinit_shared(struct shared_memory *shm);

#endif // _SHMEM_H_INCLUDED
