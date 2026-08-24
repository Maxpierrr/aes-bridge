// SPDX-License-Identifier: GPL-3.0-only
#include "Core/SharedAudioMemory.hpp"

#include <cerrno>
#include <fcntl.h>
#include <new>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lxtool::aes67 {

SharedAudioMemory::~SharedAudioMemory() { close(); }

bool SharedAudioMemory::open(bool createOwner) noexcept {
    close();
    lastError_ = 0;
    const int flags = createOwner ? O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW : O_RDWR | O_CLOEXEC | O_NOFOLLOW;
    descriptor_ = ::open(kSharedMemoryPath, flags, 0666);
    if (descriptor_ < 0) { lastError_ = errno; return false; }
    struct stat info{};
    if (fstat(descriptor_, &info) != 0) {
        lastError_ = errno;
        close();
        return false;
    }
    const bool existingLayout = info.st_size == static_cast<off_t>(sizeof(SharedAudioBlock));
    if (createOwner) {
        if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0 || fchmod(descriptor_, 0666) != 0
            || (!existingLayout && ftruncate(descriptor_, static_cast<off_t>(sizeof(SharedAudioBlock))) != 0)) {
            lastError_ = errno;
            close();
            return false;
        }
    } else if (!existingLayout) {
        lastError_ = errno != 0 ? errno : EINVAL;
        close();
        return false;
    }
    void* memory = mmap(nullptr, sizeof(SharedAudioBlock), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_, 0);
    if (memory == MAP_FAILED) { lastError_ = errno; block_ = nullptr; close(); return false; }
    block_ = static_cast<SharedAudioBlock*>(memory);
    const bool validLayout = existingLayout && block_->magic == kSharedMagic && block_->version == kSharedVersion
        && block_->channels == kVirtualChannels && block_->channelsPerStream == kAES67ChannelsPerStream
        && block_->streamBankCount == kStreamBankCount && block_->sampleRate == kSampleRate;
    if (createOwner && !validLayout) new (block_) SharedAudioBlock{};
    if (block_->magic != kSharedMagic || block_->version != kSharedVersion
        || block_->channels != kVirtualChannels || block_->channelsPerStream != kAES67ChannelsPerStream
        || block_->streamBankCount != kStreamBankCount || block_->sampleRate != kSampleRate) {
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
