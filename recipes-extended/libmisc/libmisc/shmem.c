#include "shmem.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int init_shared(const char *name, struct shared_memory *shm)
{
    shm->shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm->shm_fd < 0) {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(shm->shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        close(shm->shm_fd);
        return -1;
    }
    void *addr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm->shm_fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        close(shm->shm_fd);
        return -1;
    }
    shm->ptr = addr;
    return 0;
}

int deinit_shared(struct shared_memory *shm)
{
    if (shm->ptr) {
        munmap(shm->ptr, SHM_SIZE);
        shm->ptr = NULL;
    }
    if (shm->shm_fd >= 0) {
        close(shm->shm_fd);
        shm->shm_fd = -1;
    }
    return 0;
}