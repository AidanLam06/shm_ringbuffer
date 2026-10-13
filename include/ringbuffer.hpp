#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace ringbuffer {

template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N > 0, "RingBuffer capacity must be greater than zero");

    public:
        static constexpr std::size_t capacity() noexcept {
            return N;
        }

        // lvalue reference overload
        bool try_push(const T& value) {
            const std::size_t current_head = head_.load(std::memory_order_relaxed);
            const std::size_t current_tail = tail_.load(std::memory_order_acquire);
            if (current_head - current_tail == N) {
                return false;
            }
            data_[index(current_head)] = value;
            head_.store(current_head + 1, std::memory_order_release);
            return true;
        }

        // rvalue reference overload
        bool try_push(T&& value) {
            const std::size_t current_head = head_.load(std::memory_order_relaxed);
            const std::size_t current_tail = tail_.load(std::memory_order_acquire);
            if (current_head - current_tail == N) {
                return false;
            }
            data_[index(current_head)] = std::move(value);
            head_.store(current_head + 1, std::memory_order_release);
            return true;
        }
        
        bool try_pop(T& value) {
            const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
            const std::size_t current_head = head_.load(std::memory_order_acquire);
            if (current_tail == current_head) {
                return false;
            }
            value = std::move(data_[index(current_tail)]);
            tail_.store(current_tail + 1, std::memory_order_release);
            return true;
        }

        // compiler attribute: warns programmer to use result of the isEmpty() member function.
        [[nodiscard]]
        bool isEmpty() const noexcept {
            const std::size_t current_tail = tail_.load(std::memory_order_relaxed);
            const std::size_t current_head = head_.load(std::memory_order_acquire);
            return current_tail == current_head;
        }

    private:
        static constexpr bool is_power_of_two = (N & (N - 1)) == 0;

        static constexpr std::size_t index(std::size_t position) noexcept {
            if constexpr (is_power_of_two) {
                return position & (N - 1);
            } else {
                return position % N;
            }
        }

        static constexpr std::size_t cache_line_size = 64;
        std::array<T, N> data_{};
        
        alignas(cache_line_size)
        std::atomic<std::size_t> head_{0};
        alignas(cache_line_size)
        std::atomic<std::size_t> tail_{0};
};
} // namespace ringbuffer
