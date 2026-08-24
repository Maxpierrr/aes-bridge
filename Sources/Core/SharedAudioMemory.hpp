// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/Constants.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace lxtool::aes67 {

// A regular mmap-backed file is usable by both the user engine and coreaudiod.
inline constexpr char kSharedMemoryPath[] = "/private/tmp/org.maxpierr.aesbridge.audio.v3";
inline constexpr std::uint32_t kSharedMagic = 0x41455342U; // AESB
inline constexpr std::uint32_t kSharedVersion = 3;
inline constexpr std::size_t kSharedRingCapacity = 8192;
inline constexpr std::size_t kMaximumDiscoveredSessions = 16;
inline constexpr std::size_t kSessionNameCapacity = 96;
inline constexpr std::size_t kIPv4TextCapacity = 16;

struct alignas(64) SharedAudioRing final {
    std::atomic<std::uint64_t> writeIndex{0};
    std::atomic<std::uint64_t> readIndex{0};
    std::array<float, kSharedRingCapacity> samples{};

    std::size_t write(std::span<const float> source) noexcept;
    std::size_t read(std::span<float> destination) noexcept;
    [[nodiscard]] std::size_t available() const noexcept;
    void resetWhenIdle() noexcept;
};

struct alignas(64) SharedStatistics final {
    std::atomic<std::uint64_t> rxPackets{0};
    std::atomic<std::uint64_t> txPackets{0};
    std::atomic<std::uint64_t> packetsLost{0};
    std::atomic<std::uint64_t> malformedPackets{0};
    std::atomic<std::uint64_t> inputUnderruns{0};
    std::atomic<std::uint64_t> outputUnderruns{0};
    std::atomic<std::uint64_t> ringOverruns{0};
    std::atomic<std::uint64_t> reconnects{0};
    std::atomic<std::uint64_t> sapMalformedPackets{0};
    std::atomic<std::uint64_t> ptpMessages{0};
    std::atomic<std::uint64_t> ptpErrors{0};
    std::atomic<std::int64_t> ptpOffsetNanoseconds{0};
    std::atomic<std::int64_t> ptpMeanPathDelayNanoseconds{0};
};

struct alignas(64) SharedDiscoveredSession final {
    std::atomic<std::uint32_t> revision{0};
    std::atomic<bool> active{false};
    std::atomic<std::uint16_t> messageHash{0};
    std::atomic<std::uint16_t> port{0};
    std::atomic<std::uint16_t> channels{0};
    std::atomic<std::uint32_t> sampleRate{0};
    std::atomic<std::uint32_t> framesPerPacket{0};
    std::atomic<std::uint8_t> payloadType{0};
    std::atomic<std::uint8_t> ptpDomain{0};
    std::atomic<std::uint64_t> lastSeenUnixMilliseconds{0};
    std::array<std::atomic<char>, kSessionNameCapacity> name{};
    std::array<std::atomic<char>, kIPv4TextCapacity> originAddress{};
    std::array<std::atomic<char>, kIPv4TextCapacity> sourceAddress{};
    std::array<std::atomic<char>, kIPv4TextCapacity> multicastAddress{};
};

struct alignas(64) SharedAudioBlock final {
    std::uint32_t magic{kSharedMagic};
    std::uint32_t version{kSharedVersion};
    std::uint32_t channels{kVirtualChannels};
    std::uint32_t channelsPerStream{kAES67ChannelsPerStream};
    std::uint32_t streamBankCount{kStreamBankCount};
    std::uint32_t sampleRate{kSampleRate};
    std::atomic<bool> engineRunning{false};
    std::atomic<bool> ioRunning{false};
    std::atomic<bool> rxActive{false};
    std::atomic<bool> txActive{false};
    std::atomic<bool> ptpLocked{false};
    std::atomic<std::uint32_t> activeStreamCount{0};
    std::array<SharedAudioRing, kVirtualChannels> networkToCoreAudio;
    std::array<SharedAudioRing, kVirtualChannels> coreAudioToNetwork;
    std::array<SharedDiscoveredSession, kMaximumDiscoveredSessions> discoveredSessions;
    SharedStatistics statistics;
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<char>::is_always_lock_free);

class SharedAudioMemory final {
public:
    SharedAudioMemory() = default;
    ~SharedAudioMemory();
    SharedAudioMemory(const SharedAudioMemory&) = delete;
    SharedAudioMemory& operator=(const SharedAudioMemory&) = delete;
    bool open(bool createOwner) noexcept;
    void close() noexcept;
    [[nodiscard]] SharedAudioBlock* get() const noexcept { return block_; }
    [[nodiscard]] int lastError() const noexcept { return lastError_; }
    static bool remove() noexcept;
private:
#if defined(_WIN32)
    void* mapping_{nullptr};
    void* ownershipSemaphore_{nullptr};
#else
    int descriptor_{-1};
#endif
    SharedAudioBlock* block_{nullptr};
    int lastError_{0};
};

} // namespace lxtool::aes67
