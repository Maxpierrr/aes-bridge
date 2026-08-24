// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lxtool::aes67 {

inline constexpr std::size_t kChannels = 8;
inline constexpr std::uint32_t kSampleRate = 48'000;
inline constexpr std::uint32_t kPacketTimeMicroseconds = 1'000;
inline constexpr std::size_t kFramesPerPacket = 48;
inline constexpr std::size_t kBytesPerL24Sample = 3;
inline constexpr std::size_t kPayloadBytes = kChannels * kFramesPerPacket * kBytesPerL24Sample;
inline constexpr std::uint8_t kPayloadType = 96;
inline constexpr std::uint8_t kPTPDomain = 0;
inline constexpr std::string_view kDefaultMulticast = "239.69.83.80";
inline constexpr std::uint16_t kDefaultRTPPort = 5004;
inline constexpr std::string_view kSAPMulticast = "239.255.255.255";
inline constexpr std::uint16_t kSAPPort = 9875;
inline constexpr std::string_view kPiSessionName = "LXToolPi-Inputs-1-8";
inline constexpr std::string_view kMacSessionName = "AES-Bridge-Outputs-1-8";
inline constexpr std::uint32_t kSafeLatencyMilliseconds = 6;
inline constexpr std::size_t kDefaultJitterPackets = 6;

static_assert(kPayloadBytes == 1152);

} // namespace lxtool::aes67
