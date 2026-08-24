// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SharedAudioMemory.hpp"

namespace lxtool::aes67 {

inline constexpr wchar_t kWindowsSharedAudioName[] = L"Local\\AESBridge.Audio.v3";
using WindowsSharedAudioMemory = SharedAudioMemory;

} // namespace lxtool::aes67
