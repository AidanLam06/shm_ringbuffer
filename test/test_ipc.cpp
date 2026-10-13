#include <cassert>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

#include <shared_mem_queue.hpp>

std::size_t open_fd_count() {
    std::size_t count = 0;
    for (const auto& ignored : std::filesystem::directory_iterator("/proc/self/fd")) {
        (void)ignored;
        ++count;
    }
    return count;
}

std::size_t mapping_count() {
    std::ifstream maps("/proc/self/maps");
    return static_cast<std::size_t>(std::count(std::istreambuf_iterator<char>(maps),
                                               std::istreambuf_iterator<char>(), '\n'));
}

int ipc_ringbuffer_lifecycle() {
    const std::string name = "ipc_lifecycle_" + std::to_string(getpid());
    IPC<int, 8> ipc;

    auto* producer = ipc.create(name);
    auto* consumer = ipc.open(name);

    assert(producer->isEmpty());
    assert(producer->try_push(10));

    int value = 0;
    assert(consumer->try_pop(value));
    assert(value == 10);
    assert(consumer->isEmpty());

    ipc.close_buffer(consumer);
    ipc.close_buffer(producer);
    ipc.unlink(name);

    return EXIT_SUCCESS;
}

int ringbuffer_non_power_of_two_capacity() {
    ringbuffer::RingBuffer<int, 7> buffer;

    for (int value = 0; value < 7; ++value) {
        assert(buffer.try_push(value));
    }
    assert(!buffer.try_push(7));

    for (int expected = 0; expected < 7; ++expected) {
        int value = 0;
        assert(buffer.try_pop(value));
        assert(value == expected);
    }

    for (int value = 7; value < 14; ++value) {
        assert(buffer.try_push(value));
    }
    for (int expected = 7; expected < 14; ++expected) {
        int value = 0;
        assert(buffer.try_pop(value));
        assert(value == expected);
    }
    assert(buffer.isEmpty());
    return EXIT_SUCCESS;
}

int ipc_rejects_invalid_open_and_duplicate_creation() {
    const std::string name = "ipc_failures_" + std::to_string(getpid());
    const std::string missing = name + "_missing";
    IPC<int, 8> ipc;

    bool threw = false;
    try {
        (void)ipc.open(missing);
    } catch (const std::system_error&) {
        threw = true;
    }
    assert(threw);

    auto* producer = ipc.create(name);
    assert(producer->try_push(17));

    threw = false;
    try {
        (void)ipc.create(name);
    } catch (const std::system_error&) {
        threw = true;
    }
    assert(threw);

    // A failed compatibility check must not leak its descriptor or mapping.
    const std::size_t fd_count_before = open_fd_count();
    const std::size_t mapping_count_before = mapping_count();
    threw = false;
    try {
        IPC<unsigned int, 8>{}.open(name);
    } catch (const std::system_error&) {
        threw = true;
    }
    assert(threw);
    threw = false;
    try {
        IPC<int, 16>{}.open(name);
    } catch (const std::system_error&) {
        threw = true;
    }
    assert(threw);
    const std::size_t fd_count_after = open_fd_count();
    assert(fd_count_after == fd_count_before);
    assert(mapping_count() == mapping_count_before);

    auto* consumer = ipc.open(name);
    int value = 0;
    assert(consumer->try_pop(value));
    assert(value == 17);
    assert(ipc.close_buffer(consumer));
    assert(ipc.close_buffer(producer));
    assert(ipc.unlink(name));
    return EXIT_SUCCESS;
}

int ipc_open_racing_initialization() {
    const std::string name = "ipc_initializing_" + std::to_string(getpid());
    int result_pipe[2];
    assert(pipe(result_pipe) == 0);
    const pid_t child = fork();
    assert(child != -1);
    if (child == 0) {
        close(result_pipe[0]);
        IPC<int, 8> ipc;
        for (;;) {
            try {
                auto* consumer = ipc.open(name);
                const char result = consumer->isEmpty() ? 'r' : 'f';
                assert(ipc.close_buffer(consumer));
                assert(write(result_pipe[1], &result, sizeof(result)) == sizeof(result));
                close(result_pipe[1]);
                _exit(result == 'r' ? EXIT_SUCCESS : EXIT_FAILURE);
            } catch (const std::system_error&) {
                sched_yield();
            }
        }
    }
    close(result_pipe[1]);
    IPC<int, 8> ipc;
    auto* producer = ipc.create(name);
    char result = 0;
    assert(read(result_pipe[0], &result, sizeof(result)) == sizeof(result));
    assert(result == 'r');
    close(result_pipe[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
    assert(ipc.close_buffer(producer));
    assert(ipc.unlink(name));
    return EXIT_SUCCESS;
}

int ipc_parent_produces_child_consumes() {
    const std::string name = "ipc_ppcc_" + std::to_string(getpid());
    constexpr int value_count = 32;
    IPC<int, 8> ipc;

    auto* producer = ipc.create(name);
    int ready_pipe[2];
    assert(pipe(ready_pipe) == 0);

    assert(producer->isEmpty());
    const pid_t child = fork();
    assert(child != -1);

    if (child == 0) {
        close(ready_pipe[0]);
        auto* consumer = ipc.open(name);
        const char ready = 'r';
        assert(write(ready_pipe[1], &ready, sizeof(ready)) == sizeof(ready));
        close(ready_pipe[1]);
        for (int expected = 0; expected < value_count; ++expected) {
            int value = 0;
            while (!consumer->try_pop(value)) {
                sched_yield();
            }
            assert(value == expected);
        }
        assert(ipc.close_buffer(consumer));
        _exit(EXIT_SUCCESS);
    }

    close(ready_pipe[1]);
    char ready = 0;
    assert(read(ready_pipe[0], &ready, sizeof(ready)) == sizeof(ready));
    assert(ready == 'r');
    close(ready_pipe[0]);
    for (int value = 0; value < value_count; ++value) {
        while (!producer->try_push(value)) {
            sched_yield();
        }
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == EXIT_SUCCESS);
    assert(ipc.close_buffer(producer));
    assert(ipc.unlink(name));

    return EXIT_SUCCESS;
}

int ipc_child_produces_parent_consumes() {
    const std::string name = "ipc_cppc_" + std::to_string(getpid());
    constexpr int value_count = 32;
    IPC<int, 8> ipc;

    auto* consumer = ipc.create(name);
    int ready_pipe[2];
    assert(pipe(ready_pipe) == 0);
    assert(consumer->isEmpty());

    const pid_t child = fork();
    assert(child != -1);

    if (child == 0) {
        close(ready_pipe[0]);
        auto* producer = ipc.open(name);
        const char ready = 'r';
        assert(write(ready_pipe[1], &ready, sizeof(ready)) == sizeof(ready));
        close(ready_pipe[1]);

        for (int value = 0; value < value_count; ++value) {
            while (!producer->try_push(value)) {
                sched_yield();
            }
        }

        assert(ipc.close_buffer(producer));
        _exit(EXIT_SUCCESS);
    }

    close(ready_pipe[1]);
    char ready = 0;
    assert(read(ready_pipe[0], &ready, sizeof(ready)) == sizeof(ready));
    assert(ready == 'r');
    close(ready_pipe[0]);

    for (int expected = 0; expected < value_count; ++expected) {
        int value = 0;
        while (!consumer->try_pop(value)) {
            sched_yield();
        }
        assert(value == expected);
    }

    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == EXIT_SUCCESS);
    assert(ipc.close_buffer(consumer));
    assert(ipc.unlink(name));
    return EXIT_SUCCESS;
}

int ipc_large_cross_process_transfer() {
    constexpr std::size_t capacity = 64;
    constexpr int value_count = 100'000;
    const std::string name = "ipc_large_" + std::to_string(getpid());

    int ready_pipe[2];
    assert(pipe(ready_pipe) == 0);

    IPC<int, capacity> ipc;
    auto* producer = ipc.create(name);
    const pid_t child = fork();
    assert(child != -1);

    if (child == 0) {
        close(ready_pipe[0]);
        auto* consumer = ipc.open(name);

        const char ready = 'r';
        assert(write(ready_pipe[1], &ready, sizeof(ready)) == sizeof(ready));
        close(ready_pipe[1]);

        for (int expected = 0; expected < value_count; ++expected) {
            int value = 0;
            while (!consumer->try_pop(value)) {
                sched_yield();
            }
            assert(value == expected);
        }

        assert(ipc.close_buffer(consumer));
        _exit(EXIT_SUCCESS);
    }

    close(ready_pipe[1]);
    char ready = 0;
    assert(read(ready_pipe[0], &ready, sizeof(ready)) == sizeof(ready));
    assert(ready == 'r');
    close(ready_pipe[0]);

    for (int value = 0; value < value_count; ++value) {
        while (!producer->try_push(value)) {
            sched_yield();
        }
    }

    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == EXIT_SUCCESS);

    assert(ipc.close_buffer(producer));
    assert(ipc.unlink(name));
    return EXIT_SUCCESS;
}

int ipc_multiple_independent_queues() {
    constexpr std::size_t queue_count = 4;
    constexpr std::size_t capacity = 64;
    constexpr int value_count = 10'000;

    IPC<int, capacity> ipc;
    std::array<std::string, queue_count> names;
    std::array<IPC<int, capacity>::RingBuffer*, queue_count> producers;
    std::array<pid_t, queue_count> children;
    std::array<std::array<int, 2>, queue_count> ready_pipes;

    for (std::size_t queue = 0; queue < queue_count; ++queue) {
        names[queue] = "ipc_multi_" + std::to_string(getpid()) + "_" +
                       std::to_string(queue);
        producers[queue] = ipc.create(names[queue]);
        assert(pipe(ready_pipes[queue].data()) == 0);

        children[queue] = fork();
        assert(children[queue] != -1);

        if (children[queue] == 0) {
            close(ready_pipes[queue][0]);
            auto* consumer = ipc.open(names[queue]);

            const char ready = 'r';
            assert(write(ready_pipes[queue][1], &ready, sizeof(ready)) ==
                   sizeof(ready));
            close(ready_pipes[queue][1]);

            const int first_value = static_cast<int>(queue) * value_count;
            for (int expected = first_value;
                 expected < first_value + value_count;
                 ++expected) {
                int value = 0;
                while (!consumer->try_pop(value)) {
                    sched_yield();
                }
                assert(value == expected);
            }

            assert(ipc.close_buffer(consumer));
            _exit(EXIT_SUCCESS);
        }

        close(ready_pipes[queue][1]);
    }

    for (std::size_t queue = 0; queue < queue_count; ++queue) {
        char ready = 0;
        assert(read(ready_pipes[queue][0], &ready, sizeof(ready)) == sizeof(ready));
        assert(ready == 'r');
        close(ready_pipes[queue][0]);
    }

    for (std::size_t queue = 0; queue < queue_count; ++queue) {
        const int first_value = static_cast<int>(queue) * value_count;
        for (int value = first_value; value < first_value + value_count; ++value) {
            while (!producers[queue]->try_push(value)) {
                sched_yield();
            }
        }
    }

    for (const pid_t child : children) {
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == EXIT_SUCCESS);
    }

    for (std::size_t queue = 0; queue < queue_count; ++queue) {
        assert(ipc.close_buffer(producers[queue]));
        assert(ipc.unlink(names[queue]));
    }

    return EXIT_SUCCESS;
}

int main() {
    ipc_ringbuffer_lifecycle();
    ringbuffer_non_power_of_two_capacity();
    ipc_rejects_invalid_open_and_duplicate_creation();
    ipc_open_racing_initialization();
    ipc_parent_produces_child_consumes();
    ipc_child_produces_parent_consumes();
    ipc_large_cross_process_transfer();
    ipc_multiple_independent_queues();
    return EXIT_SUCCESS;
}
