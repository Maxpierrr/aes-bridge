// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <type_traits>

namespace lxtool::aes67 {

template <typename T, std::size_t Capacity>
class SPSCRingBuffer final {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(Capacity >= 2);
public:
    std::size_t write(std::span<const T> source) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        const auto count = std::min(source.size(), writable(write, read));
        for (std::size_t i = 0; i < count; ++i) storage_[(write + i) % Capacity] = source[i];
        writeIndex_.store((write + count) % Capacity, std::memory_order_release);
        return count;
    }

    std::size_t read(std::span<T> destination) noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        const auto count = std::min(destination.size(), readable(read, write));
        for (std::size_t i = 0; i < count; ++i) destination[i] = storage_[(read + i) % Capacity];
        readIndex_.store((read + count) % Capacity, std::memory_order_release);
        return count;
    }

    [[nodiscard]] std::size_t available() const noexcept {
        return readable(readIndex_.load(std::memory_order_relaxed), writeIndex_.load(std::memory_order_acquire));
    }
    void resetWhenIdle() noexcept { readIndex_.store(0); writeIndex_.store(0); }

private:
    static std::size_t readable(std::size_t read, std::size_t write) noexcept {
        return write >= read ? write - read : Capacity - read + write;
    }
    static std::size_t writable(std::size_t write, std::size_t read) noexcept {
        return Capacity - readable(read, write) - 1;
    }

    std::array<T, Capacity> storage_{};
    alignas(64) std::atomic<std::size_t> writeIndex_{0};
    alignas(64) std::atomic<std::size_t> readIndex_{0};
};

} // namespace lxtool::aes67
