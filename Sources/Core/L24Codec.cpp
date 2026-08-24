// SPDX-License-Identifier: GPL-3.0-only
#include "Core/L24Codec.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lxtool::aes67 {

bool L24Codec::encode(std::span<const float> samples, std::span<std::uint8_t> bytes) noexcept {
    if (bytes.size() != samples.size() * 3) return false;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float value = std::clamp(samples[i], -1.0F, 1.0F);
        const std::int32_t pcm = value <= -1.0F
            ? -8'388'608
            : static_cast<std::int32_t>(std::lrint(static_cast<double>(value) * 8'388'607.0));
        const auto bits = static_cast<std::uint32_t>(pcm);
        bytes[i * 3] = static_cast<std::uint8_t>((bits >> 16U) & 0xffU);
        bytes[i * 3 + 1] = static_cast<std::uint8_t>((bits >> 8U) & 0xffU);
        bytes[i * 3 + 2] = static_cast<std::uint8_t>(bits & 0xffU);
    }
    return true;
}

bool L24Codec::decode(std::span<const std::uint8_t> bytes, std::span<float> samples) noexcept {
    if (bytes.size() != samples.size() * 3) return false;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::uint32_t raw = (static_cast<std::uint32_t>(bytes[i * 3]) << 16U)
            | (static_cast<std::uint32_t>(bytes[i * 3 + 1]) << 8U)
            | static_cast<std::uint32_t>(bytes[i * 3 + 2]);
        if ((raw & 0x0080'0000U) != 0U) raw |= 0xff00'0000U;
        const auto pcm = static_cast<std::int32_t>(raw);
        samples[i] = pcm < 0
            ? static_cast<float>(static_cast<double>(pcm) / 8'388'608.0)
            : static_cast<float>(static_cast<double>(pcm) / 8'388'607.0);
    }
    return true;
}

} // namespace lxtool::aes67
