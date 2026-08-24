// SPDX-License-Identifier: GPL-3.0-only
#include "Core/RTPPacket.hpp"

#include <algorithm>

namespace lxtool::aes67 {
namespace {
void put16(std::uint8_t* out, std::uint16_t value) noexcept {
    out[0] = static_cast<std::uint8_t>(value >> 8U); out[1] = static_cast<std::uint8_t>(value);
}
void put32(std::uint8_t* out, std::uint32_t value) noexcept {
    out[0] = static_cast<std::uint8_t>(value >> 24U); out[1] = static_cast<std::uint8_t>(value >> 16U);
    out[2] = static_cast<std::uint8_t>(value >> 8U); out[3] = static_cast<std::uint8_t>(value);
}
std::uint16_t get16(const std::uint8_t* in) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[0]) << 8U) | in[1]);
}
std::uint32_t get32(const std::uint8_t* in) noexcept {
    return (static_cast<std::uint32_t>(in[0]) << 24U) | (static_cast<std::uint32_t>(in[1]) << 16U)
        | (static_cast<std::uint32_t>(in[2]) << 8U) | in[3];
}
}

bool RTPCodec::encode(const RTPPacket& packet, std::span<std::uint8_t> bytes, std::size_t& written) noexcept {
    written = 0;
    if (packet.payloadType > 127 || bytes.size() < kFixedHeaderBytes + packet.payload.size()) return false;
    bytes[0] = 0x80U;
    bytes[1] = static_cast<std::uint8_t>((packet.marker ? 0x80U : 0U) | packet.payloadType);
    put16(bytes.data() + 2, packet.sequence);
    put32(bytes.data() + 4, packet.timestamp);
    put32(bytes.data() + 8, packet.ssrc);
    std::copy(packet.payload.begin(), packet.payload.end(), bytes.begin() + kFixedHeaderBytes);
    written = kFixedHeaderBytes + packet.payload.size();
    return true;
}

bool RTPCodec::decode(std::span<const std::uint8_t> bytes, RTPPacket& packet) {
    if (bytes.size() < kFixedHeaderBytes || (bytes[0] >> 6U) != 2U) return false;
    const bool padding = (bytes[0] & 0x20U) != 0;
    const bool extension = (bytes[0] & 0x10U) != 0;
    const std::size_t csrcCount = bytes[0] & 0x0fU;
    std::size_t offset = kFixedHeaderBytes + csrcCount * 4;
    if (offset > bytes.size()) return false;
    if (extension) {
        if (offset + 4 > bytes.size()) return false;
        const std::size_t words = get16(bytes.data() + offset + 2);
        offset += 4 + words * 4;
        if (offset > bytes.size()) return false;
    }
    std::size_t end = bytes.size();
    if (padding) {
        const std::size_t count = bytes.back();
        if (count == 0 || count > end - offset) return false;
        end -= count;
    }
    packet.payloadType = bytes[1] & 0x7fU;
    packet.marker = (bytes[1] & 0x80U) != 0;
    packet.sequence = get16(bytes.data() + 2);
    packet.timestamp = get32(bytes.data() + 4);
    packet.ssrc = get32(bytes.data() + 8);
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

} // namespace lxtool::aes67
