// SPDX-License-Identifier: GPL-3.0-only
#include "Core/SAP.hpp"
#include "Core/IPv4Address.hpp"

#include <algorithm>

namespace lxtool::aes67 {

std::uint16_t SAP::hash(std::string_view sdp) noexcept {
    std::uint32_t value = 2166136261U;
    for (const unsigned char byte : sdp) value = (value ^ byte) * 16777619U;
    return static_cast<std::uint16_t>((value >> 16U) ^ value);
}

std::vector<std::uint8_t> SAP::encode(const SAPMessage& message) {
    const auto origin = IPv4Address::parse(message.originAddress);
    if (!origin) return {};
    const std::string mime = message.payloadType.empty() ? "application/sdp" : message.payloadType;
    std::vector<std::uint8_t> out(8 + mime.size() + 1 + message.sdp.size());
    out[0] = static_cast<std::uint8_t>(0x20U | (message.deletion ? 0x04U : 0U));
    out[1] = 0;
    const auto messageHash = message.messageHash != 0 ? message.messageHash : hash(message.sdp);
    out[2] = static_cast<std::uint8_t>(messageHash >> 8U);
    out[3] = static_cast<std::uint8_t>(messageHash);
    std::copy(origin->octets.begin(), origin->octets.end(), out.begin() + 4);
    std::copy(mime.begin(), mime.end(), out.begin() + 8);
    std::copy(message.sdp.begin(), message.sdp.end(), out.begin() + static_cast<std::ptrdiff_t>(9 + mime.size()));
    return out;
}

bool SAP::decode(std::span<const std::uint8_t> bytes, SAPMessage& message, std::string* error) {
    if (bytes.size() < 9 || ((bytes[0] >> 5U) & 0x07U) != 1U || (bytes[0] & 0x02U) != 0U || (bytes[0] & 0x01U) != 0U) {
        if (error) *error = "en-tête SAP IPv4 non pris en charge";
        return false;
    }
    const std::size_t payloadStart = 8 + static_cast<std::size_t>(bytes[1]) * 4;
    if (payloadStart >= bytes.size()) return false;
    const auto zero = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(payloadStart), bytes.end(), 0);
    if (zero == bytes.end()) return false;
    IPv4Address origin{{bytes[4], bytes[5], bytes[6], bytes[7]}};
    message.deletion = (bytes[0] & 0x04U) != 0;
    message.messageHash = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[2]) << 8U) | bytes[3]);
    message.originAddress = origin.toString();
    message.payloadType.assign(reinterpret_cast<const char*>(bytes.data() + payloadStart), static_cast<std::size_t>(zero - bytes.begin()) - payloadStart);
    const auto sdpStart = std::next(zero);
    if (sdpStart == bytes.end()) return false;
    message.sdp.assign(reinterpret_cast<const char*>(bytes.data() + (sdpStart - bytes.begin())), static_cast<std::size_t>(bytes.end() - sdpStart));
    if (message.payloadType != "application/sdp" || !message.sdp.starts_with("v=0")) return false;
    return true;
}

} // namespace lxtool::aes67
