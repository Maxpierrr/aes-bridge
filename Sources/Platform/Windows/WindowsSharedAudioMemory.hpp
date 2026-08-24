// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SharedAudioMemory.hpp"

namespace lxtool::aes67 {

inline constexpr wchar_t kWindowsSharedAudioName[] = L"Local\\AESBridge.Audio.v2";

class WindowsSharedAudioMemory final {
public:
    WindowsSharedAudioMemory() = default;
    ~WindowsSharedAudioMemory();
    WindowsSharedAudioMemory(const WindowsSharedAudioMemory&) = delete;
    WindowsSharedAudioMemory& operator=(const WindowsSharedAudioMemory&) = delete;
    bool open(bool createOwner) noexcept;
    void close() noexcept;
    [[nodiscard]] SharedAudioBlock* get() const noexcept { return block_; }
    [[nodiscard]] unsigned long lastError() const noexcept { return lastError_; }
private:
    void* mapping_{nullptr};
    SharedAudioBlock* block_{nullptr};
    unsigned long lastError_{0};
};

} // namespace lxtool::aes67
