// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lxtool::aes67 {

struct SAPMessage {
    bool deletion{false};
    std::uint16_t messageHash{0};
    std::string originAddress;
    std::string payloadType;
    std::string sdp;
};

class SAP final {
public:
    static std::vector<std::uint8_t> encode(const SAPMessage& message);
    static bool decode(std::span<const std::uint8_t> bytes, SAPMessage& message, std::string* error = nullptr);
    static std::uint16_t hash(std::string_view sdp) noexcept;
};

} // namespace lxtool::aes67
