// SPDX-License-Identifier: GPL-3.0-only
#include "Driver/LXToolIOHandler.hpp"

#include <array>
#include <cstring>

namespace lxtool::aes67 {

void LXToolIOHandler::OnReadClientInput(const std::shared_ptr<aspl::Client>&,
    const std::shared_ptr<aspl::Stream>&, Float64, Float64, void* bytes, UInt32 bytesCount) {
    if (!bytes) return;
    constexpr UInt32 bytesPerFrame = static_cast<UInt32>(kChannels * sizeof(Float32));
    const UInt32 frameCount = bytesCount / bytesPerFrame;
    if (frameCount == 0 || frameCount > 4096 || bytesCount % bytesPerFrame != 0) {
        std::memset(bytes, 0, bytesCount);
        return;
    }
    auto* output = static_cast<Float32*>(bytes);
    auto* bridge = bridge_.load(std::memory_order_acquire);
    if (!bridge || !bridge->engineRunning.load(std::memory_order_acquire)) {
        std::memset(bytes, 0, bytesCount);
        return;
    }
    std::array<Float32, 4096> channel{};
    bool underrun = false;
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        const auto count = bridge->networkToCoreAudio[ch].read(std::span(channel).first(frameCount));
        if (count < frameCount) { std::fill(channel.begin() + static_cast<std::ptrdiff_t>(count), channel.begin() + frameCount, 0.0F); underrun = true; }
        for (UInt32 frame = 0; frame < frameCount; ++frame) output[frame * kChannels + ch] = channel[frame];
    }
    if (underrun) bridge->statistics.inputUnderruns.fetch_add(1, std::memory_order_relaxed);
}

void LXToolIOHandler::OnWriteClientOutput(const std::shared_ptr<aspl::Client>&,
    const std::shared_ptr<aspl::Stream>&, Float64, Float64, const Float32* frames,
    UInt32 frameCount, UInt32 channelCount) {
    auto* bridge = bridge_.load(std::memory_order_acquire);
    if (!frames || !bridge || !bridge->engineRunning.load(std::memory_order_acquire)
        || channelCount != kChannels || frameCount > 4096) return;
    std::array<Float32, 4096> channel{};
    bool overrun = false;
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        for (UInt32 frame = 0; frame < frameCount; ++frame) channel[frame] = frames[frame * kChannels + ch];
        if (bridge->coreAudioToNetwork[ch].write(std::span(channel).first(frameCount)) != frameCount) overrun = true;
    }
    if (overrun) bridge->statistics.ringOverruns.fetch_add(1, std::memory_order_relaxed);
}

} // namespace lxtool::aes67
