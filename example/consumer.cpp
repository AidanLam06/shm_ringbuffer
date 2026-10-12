#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <ringbuffer.hpp>

using RingBuffer = ringbuffer::RingBuffer<int, 256>;

int main() {
    int fd = shm_open("/buffer", O_RDWR, 0666);
    if (fd == -1) {
        std::perror("fd");
        return 1;
    }

    void* ptr = mmap(
        nullptr,
        sizeof(RingBuffer),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (ptr == MAP_FAILED) {
        std::perror("mmap");
        close(fd);
        return 1;
    }

    auto* rbuf = static_cast<RingBuffer*>(ptr);
    int value {0};
    int results[20]{};

    for (int i = 0; i < 20; i++) {
        rbuf->try_pop(value);
        results[i] = value;
    }

    for (auto n : results) {
        std::cout << n << std::endl;
    }

    if (munmap(ptr, sizeof(RingBuffer)) == -1) {
        std::perror("munmap");
    }

    if (close(fd) == -1) {
        std::perror("close");
    }
}