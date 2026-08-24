// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace lxtool::aes67 {

class L24Codec final {
public:
    static bool encode(std::span<const float> samples, std::span<std::uint8_t> bytes) noexcept;
    static bool decode(std::span<const std::uint8_t> bytes, std::span<float> samples) noexcept;
};

} // namespace lxtool::aes67
