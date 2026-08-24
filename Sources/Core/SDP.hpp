// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/Constants.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lxtool::aes67 {

struct SessionDescription {
    std::string name;
    std::string originAddress;
    std::string multicastAddress;
    std::uint16_t port{kDefaultRTPPort};
    std::uint8_t payloadType{kPayloadType};
    std::string encoding{"L24"};
    std::uint32_t sampleRate{kSampleRate};
    std::uint16_t channels{kChannels};
    std::uint32_t packetTimeMilliseconds{1};
    std::uint32_t framesPerPacket{kFramesPerPacket};
    std::uint8_t ptpDomain{kPTPDomain};
    std::string sourceAddress;
};

class SDP final {
public:
    static std::optional<SessionDescription> parse(std::string_view text, std::string* error = nullptr);
    static std::string generate(const SessionDescription& session);
    static std::vector<std::string> validateLXToolProfile(const SessionDescription& session);
};

} // namespace lxtool::aes67
