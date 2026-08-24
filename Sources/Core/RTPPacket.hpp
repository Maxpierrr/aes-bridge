// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lxtool::aes67 {

struct RTPPacket {
    std::uint8_t payloadType{96};
    bool marker{false};
    std::uint16_t sequence{0};
    std::uint32_t timestamp{0};
    std::uint32_t ssrc{0};
    std::vector<std::uint8_t> payload;
};

class RTPCodec final {
public:
    static constexpr std::size_t kFixedHeaderBytes = 12;
    static bool encode(const RTPPacket& packet, std::span<std::uint8_t> bytes, std::size_t& written) noexcept;
    static bool decode(std::span<const std::uint8_t> bytes, RTPPacket& packet);
};

} // namespace lxtool::aes67
