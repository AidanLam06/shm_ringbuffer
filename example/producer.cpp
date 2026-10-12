#include <cstdio>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <chrono>

#include <ringbuffer.hpp>

using RingBuffer = ringbuffer::RingBuffer<int, 256>;

int main() {
    // the producer is responsible for creating the share memory object and also destroying it.
    int fd = shm_open("/buffer", O_CREAT | O_RDWR, 0666);

    if (fd == -1) {
        std::perror("fd");
        return 1;
    }

    if (ftruncate(fd, sizeof(ringbuffer::RingBuffer<int, 256>)) == -1) {
        std::perror("ftruncate");
        close(fd);
        shm_unlink("/buffer");
        return 1;
    }

    void* ptr = mmap(
        nullptr,
        sizeof(ringbuffer::RingBuffer<int, 256>),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (ptr == MAP_FAILED) {
        std::perror("mmap");
        close(fd);
        shm_unlink("/buffer");
        return 1;
    }

    auto* rbuf = new (ptr) RingBuffer{};

    for (int i = 0; i < 20; i++) {
        rbuf->try_push(i);
    }

    std::this_thread::sleep_for(std::chrono::seconds(8));

    if (munmap(ptr, 4096) == -1) {
        std::perror("munmap");
    }

    if (close(fd) == -1) {
        std::perror("close");
    }

    if (shm_unlink("/buffer") == -1) {
        std::perror("shm_unlink");
    }
}