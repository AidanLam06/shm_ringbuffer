#pragma once

#include <ringbuffer.hpp>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <new>
#include <string>
#include <system_error>
#include <typeinfo>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

template<typename T, std::size_t Capacity>
class IPC {
public:
    using RingBuffer = ringbuffer::RingBuffer<T, Capacity>;

    RingBuffer* create(const std::string& name) {
        const std::string shm_name = "/" + name;
        const int fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd == -1) {
            throw_error("shm_open failed : create()");
        }
        if (ftruncate(fd, sizeof(SharedMemory)) == -1) {
            const int error = errno;
            close(fd);
            shm_unlink(shm_name.c_str());
            throw std::system_error(error, std::generic_category(),
                                    "ftruncate failed : create()");
        }
        void* ptr = mmap(nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            const int error = errno;
            close(fd);
            shm_unlink(shm_name.c_str());
            throw std::system_error(error, std::generic_category(),
                                    "mmap failed : create()");
        }
        close(fd);

        auto* memory = new (ptr) SharedMemory{};
        memory->state.store(State::ready, std::memory_order_release);
        return &memory->buffer;
    }

    RingBuffer* open(const std::string& name) {
        const std::string shm_name = "/" + name;
        const int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
        if (fd == -1) {
            throw_error("shm_open failed : open()");
        }
        struct stat status {};
        if (fstat(fd, &status) == -1 ||
            status.st_size != static_cast<off_t>(sizeof(SharedMemory))) {
            const int error = errno == 0 ? EINVAL : errno;
            close(fd);
            throw std::system_error(error, std::generic_category(),
                                    "shared memory layout mismatch : open()");
        }
        void* ptr = mmap(nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        const int mmap_error = errno;
        close(fd);
        if (ptr == MAP_FAILED) {
            throw std::system_error(mmap_error, std::generic_category(),
                                    "mmap failed : open()");
        }
        auto* memory = static_cast<SharedMemory*>(ptr);
        if (memory->magic_value != magic || memory->type_tag != typeid(T).hash_code() ||
            memory->element_size != sizeof(T) ||
            memory->capacity != Capacity || memory->buffer_size != sizeof(RingBuffer)) {
            munmap(memory, sizeof(SharedMemory));
            throw std::system_error(EINVAL, std::generic_category(),
                                    "shared memory layout mismatch : open()");
        }
        while (memory->state.load(std::memory_order_acquire) == State::initializing) {
            sched_yield();
        }
        return &memory->buffer;
    }

    bool close_buffer(RingBuffer* buffer) {
        auto* memory = reinterpret_cast<SharedMemory*>(
            reinterpret_cast<std::byte*>(buffer) - offsetof(SharedMemory, buffer));
        if (munmap(memory, sizeof(SharedMemory)) == -1) {
            throw_error("munmap failed : close()");
        }
        return true;
    }

    bool unlink(const std::string& name) {
        const std::string shm_name = "/" + name;
        if (shm_unlink(shm_name.c_str()) == -1) {
            throw_error("shm_unlink failed : unlink()");
        }
        return true;
    }

private:
    enum class State : std::uint32_t { initializing, ready };
    static constexpr std::uint64_t magic = 0x534D495043000001ULL;

    struct SharedMemory {
        std::uint64_t magic_value = magic;
        std::size_t type_tag = typeid(T).hash_code();
        std::size_t element_size = sizeof(T);
        std::size_t capacity = Capacity;
        std::size_t buffer_size = sizeof(RingBuffer);
        std::atomic<State> state{State::initializing};
        alignas(RingBuffer) RingBuffer buffer;
    };

    static void throw_error(const char* message) {
        throw std::system_error(errno, std::generic_category(), message);
    }
};
