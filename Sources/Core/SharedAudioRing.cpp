// SPDX-License-Identifier: GPL-3.0-only
#include "Core/SharedAudioMemory.hpp"

namespace lxtool::aes67 {

std::size_t SharedAudioRing::write(std::span<const float> source) noexcept {
    const auto write = writeIndex.load(std::memory_order_relaxed);
    const auto read = readIndex.load(std::memory_order_acquire);
    const auto used = static_cast<std::size_t>(write - read);
    const auto count = std::min(source.size(), kSharedRingCapacity - std::min(used, kSharedRingCapacity));
    for (std::size_t i = 0; i < count; ++i) samples[(write + i) % kSharedRingCapacity] = source[i];
    writeIndex.store(write + count, std::memory_order_release);
    return count;
}

std::size_t SharedAudioRing::read(std::span<float> destination) noexcept {
    const auto read = readIndex.load(std::memory_order_relaxed);
    const auto write = writeIndex.load(std::memory_order_acquire);
    const auto count = std::min(destination.size(), static_cast<std::size_t>(write - read));
    for (std::size_t i = 0; i < count; ++i) destination[i] = samples[(read + i) % kSharedRingCapacity];
    readIndex.store(read + count, std::memory_order_release);
    return count;
}

std::size_t SharedAudioRing::available() const noexcept {
    return static_cast<std::size_t>(writeIndex.load(std::memory_order_acquire) - readIndex.load(std::memory_order_relaxed));
}

void SharedAudioRing::resetWhenIdle() noexcept {
    readIndex.store(0, std::memory_order_relaxed);
    writeIndex.store(0, std::memory_order_relaxed);
}

} // namespace lxtool::aes67
