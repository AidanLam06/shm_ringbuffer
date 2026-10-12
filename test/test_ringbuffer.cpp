#include <cstdlib>
#include <shared_mem_queue.hpp>
#include <unistd.h>
#include <cassert>

int ringbuffer_full_and_empty(){
    int pc[2];

    if (pipe(pc) == -1) {
        std::perror("pipe, pc : empty_queue()");
        return EXIT_FAILURE;
    }

    char name[] = "test1";
    IPC<int, 64> ipc;
    pid_t pid = fork();

    if (pid == -1) {
        close(pc[0]);
        close(pc[1]);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        int8_t buf;
        int value;

        const ssize_t received = read(pc[0], &buf, sizeof(buf));
        assert(received == 1);

        auto* consumer_queue = ipc.open(name);

        assert(!consumer_queue->try_pop(value));

        for (int i = 0; i < 8; i++) {
            consumer_queue->try_push(i);
        }
        assert(!consumer_queue->try_push(1));

        ipc.close_buffer(consumer_queue);
    } else {
        const int8_t buf = 1;

        auto* producer_queue = ipc.create(name);

        write(pc[1], &buf, sizeof(buf));
        ipc.close_buffer(producer_queue);
    }

    return EXIT_SUCCESS;
}

int ringbuffer_fifo_and_wraparound() {
    IPC<int, 8> ipc;
    auto buffer = ipc.create("test2");

    for (int first = 0; first < 1024; first += 8) {
        for (int value = first; value < first + 8; ++value) {
            assert(buffer->try_push(value));
        }

        for (int expected = first; expected < first + 8; ++expected) {
            int value;
            assert(buffer->try_pop(value));
            assert(value == expected);
        }
    }

    assert(buffer->isEmpty());
    ipc.close_buffer(buffer);
    
    return EXIT_SUCCESS;
}

int main() {
    ringbuffer_full_and_empty();
    ringbuffer_fifo_and_wraparound();
}