// SPDX-License-Identifier: GPL-3.0-only
#include "Platform/Windows/WindowsSharedAudioMemory.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <new>

namespace lxtool::aes67 {

SharedAudioMemory::~SharedAudioMemory() { close(); }

bool SharedAudioMemory::open(bool createOwner) noexcept {
    close();
    lastError_ = 0;
    HANDLE mapping = nullptr;
    if (createOwner) {
        SetLastError(ERROR_SUCCESS);
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(sizeof(SharedAudioBlock)), kWindowsSharedAudioName);
        if (mapping && GetLastError() == ERROR_ALREADY_EXISTS) {
            lastError_ = static_cast<int>(ERROR_ALREADY_EXISTS);
            CloseHandle(mapping);
            return false;
        }
    } else {
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kWindowsSharedAudioName);
    }
    if (!mapping) { lastError_ = static_cast<int>(GetLastError()); return false; }
    mapping_ = mapping;
    void* memory = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedAudioBlock));
    if (!memory) { lastError_ = static_cast<int>(GetLastError()); close(); return false; }
    block_ = static_cast<SharedAudioBlock*>(memory);
    if (createOwner) new (block_) SharedAudioBlock{};
    if (block_->magic != kSharedMagic || block_->version != kSharedVersion
        || block_->channels != kVirtualChannels || block_->channelsPerStream != kAES67ChannelsPerStream
        || block_->streamBankCount != kStreamBankCount || block_->sampleRate != kSampleRate) {
        lastError_ = static_cast<int>(ERROR_REVISION_MISMATCH);
        close();
        return false;
    }
    return true;
}

void SharedAudioMemory::close() noexcept {
    if (block_) UnmapViewOfFile(block_);
    block_ = nullptr;
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
    mapping_ = nullptr;
}

bool SharedAudioMemory::remove() noexcept {
    // Named page-file mappings disappear automatically after the last handle
    // closes. There is no persistent filesystem object to unlink on Windows.
    return true;
}

} // namespace lxtool::aes67
