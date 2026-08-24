// SPDX-License-Identifier: GPL-3.0-only
#include "Core/SharedAudioMemory.hpp"

#include <cerrno>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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

void SharedAudioRing::resetWhenIdle() noexcept { readIndex.store(0); writeIndex.store(0); }

SharedAudioMemory::~SharedAudioMemory() { close(); }

bool SharedAudioMemory::open(bool createAndReset) noexcept {
    close();
    lastError_ = 0;
    const int flags = createAndReset ? O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW : O_RDWR | O_CLOEXEC | O_NOFOLLOW;
    descriptor_ = ::open(kSharedMemoryPath, flags, 0666);
    if (descriptor_ < 0) { lastError_ = errno; return false; }
    if (createAndReset) {
        if (fchmod(descriptor_, 0666) != 0 || ftruncate(descriptor_, static_cast<off_t>(sizeof(SharedAudioBlock))) != 0) {
            lastError_ = errno;
            close();
            return false;
        }
    }
    struct stat info{};
    if (fstat(descriptor_, &info) != 0 || info.st_size != static_cast<off_t>(sizeof(SharedAudioBlock))) {
        lastError_ = errno != 0 ? errno : EINVAL;
        close();
        return false;
    }
    void* memory = mmap(nullptr, sizeof(SharedAudioBlock), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_, 0);
    if (memory == MAP_FAILED) { lastError_ = errno; block_ = nullptr; close(); return false; }
    block_ = static_cast<SharedAudioBlock*>(memory);
    if (createAndReset) new (block_) SharedAudioBlock{};
    if (block_->magic != kSharedMagic || block_->version != kSharedVersion || block_->channels != kChannels || block_->sampleRate != kSampleRate) {
        lastError_ = EPROTO;
        close(); return false;
    }
    return true;
}

void SharedAudioMemory::close() noexcept {
    if (block_) munmap(block_, sizeof(SharedAudioBlock));
    block_ = nullptr;
    if (descriptor_ >= 0) ::close(descriptor_);
    descriptor_ = -1;
}

bool SharedAudioMemory::remove() noexcept { return unlink(kSharedMemoryPath) == 0 || errno == ENOENT; }

} // namespace lxtool::aes67
