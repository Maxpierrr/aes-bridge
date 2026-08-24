// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <span>

namespace lxtool::aes67 {

template <typename Sample>
bool interleaveChannel(std::span<const Sample> source, std::size_t channelIndex,
    std::size_t channelCount, std::span<Sample> destination) noexcept {
    if (channelCount == 0 || channelIndex >= channelCount || source.size() > destination.size() / channelCount) return false;
    for (std::size_t frame = 0; frame < source.size(); ++frame) destination[frame * channelCount + channelIndex] = source[frame];
    return true;
}

template <typename Sample>
bool deinterleaveChannel(std::span<const Sample> source, std::size_t channelIndex,
    std::size_t channelCount, std::span<Sample> destination) noexcept {
    if (channelCount == 0 || channelIndex >= channelCount || destination.size() > source.size() / channelCount) return false;
    for (std::size_t frame = 0; frame < destination.size(); ++frame) destination[frame] = source[frame * channelCount + channelIndex];
    return true;
}

} // namespace lxtool::aes67
