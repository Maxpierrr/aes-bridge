// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/Constants.hpp"
#include "Core/SPSCRingBuffer.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace lxtool::aes67 {

struct alignas(64) AudioBridge final {
    static constexpr std::size_t kRingSamples = 4096;
    using ChannelRing = SPSCRingBuffer<float, kRingSamples>;
    std::array<ChannelRing, kChannels> networkToCoreAudio;
    std::array<ChannelRing, kChannels> coreAudioToNetwork;
    std::atomic<std::uint64_t> inputUnderruns{0};
    std::atomic<std::uint64_t> outputOverruns{0};
    std::atomic<bool> ioRunning{false};
};

} // namespace lxtool::aes67
