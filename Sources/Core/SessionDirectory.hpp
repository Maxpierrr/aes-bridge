// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SDP.hpp"
#include "Core/SharedAudioMemory.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lxtool::aes67 {

struct DiscoveredSessionSnapshot final {
    std::uint16_t messageHash{0};
    std::string name;
    std::string originAddress;
    std::string sourceAddress;
    std::string multicastAddress;
    std::uint16_t port{0};
    std::uint16_t channels{0};
    std::uint32_t sampleRate{0};
    std::uint32_t framesPerPacket{0};
    std::uint8_t payloadType{0};
    std::uint8_t ptpDomain{0};
    std::uint64_t lastSeenUnixMilliseconds{0};
};

class SessionDirectory final {
public:
    static void upsert(SharedAudioBlock& block, std::uint16_t messageHash,
        const SessionDescription& session, std::uint64_t nowUnixMilliseconds) noexcept;
    static void erase(SharedAudioBlock& block, std::uint16_t messageHash,
        const std::string& originAddress) noexcept;
    static void expire(SharedAudioBlock& block, std::uint64_t oldestAllowedUnixMilliseconds) noexcept;
    [[nodiscard]] static std::vector<DiscoveredSessionSnapshot> snapshots(const SharedAudioBlock& block);
};

} // namespace lxtool::aes67
