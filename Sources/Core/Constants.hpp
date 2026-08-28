// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lxtool::aes67 {

// Keep each 1 ms L24 RTP payload below the standard Ethernet MTU. The virtual
// endpoint is 64 channels, split into eight independent 8-channel AES67 banks.
inline constexpr std::size_t kAES67ChannelsPerStream = 8;
inline constexpr std::size_t kStreamBankCount = 8;
inline constexpr std::size_t kVirtualChannels = kAES67ChannelsPerStream * kStreamBankCount;
inline constexpr std::size_t kChannels = kAES67ChannelsPerStream;
inline constexpr std::uint32_t kSampleRate = 48'000;
inline constexpr std::uint32_t kPacketTimeMicroseconds = 1'000;
inline constexpr std::size_t kFramesPerPacket = 48;
inline constexpr std::size_t kBytesPerL24Sample = 3;
inline constexpr std::size_t kPayloadBytes = kChannels * kFramesPerPacket * kBytesPerL24Sample;
constexpr std::size_t payloadBytesForChannels(std::size_t channels) noexcept {
    return channels * kFramesPerPacket * kBytesPerL24Sample;
}
constexpr bool supportedAES67ChannelCount(std::size_t channels) noexcept {
    return channels == 1 || channels == 2 || channels == 4 || channels == 8;
}
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
static_assert(kVirtualChannels == 64);

} // namespace lxtool::aes67
