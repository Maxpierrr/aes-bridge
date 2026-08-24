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
inline constexpr char kSharedMemoryPath[] = "/private/tmp/org.maxpierr.aesbridge.audio.v1";
inline constexpr std::uint32_t kSharedMagic = 0x41455342U; // AESB
inline constexpr std::uint32_t kSharedVersion = 1;
inline constexpr std::size_t kSharedRingCapacity = 8192;

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
    std::atomic<std::int64_t> ptpOffsetNanoseconds{0};
};

struct alignas(64) SharedAudioBlock final {
    std::uint32_t magic{kSharedMagic};
    std::uint32_t version{kSharedVersion};
    std::uint32_t channels{kChannels};
    std::uint32_t sampleRate{kSampleRate};
    std::atomic<bool> engineRunning{false};
    std::atomic<bool> ioRunning{false};
    std::atomic<bool> rxActive{false};
    std::atomic<bool> txActive{false};
    std::atomic<bool> ptpLocked{false};
    std::array<SharedAudioRing, kChannels> networkToCoreAudio;
    std::array<SharedAudioRing, kChannels> coreAudioToNetwork;
    SharedStatistics statistics;
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

class SharedAudioMemory final {
public:
    SharedAudioMemory() = default;
    ~SharedAudioMemory();
    SharedAudioMemory(const SharedAudioMemory&) = delete;
    SharedAudioMemory& operator=(const SharedAudioMemory&) = delete;
    bool open(bool createAndReset) noexcept;
    void close() noexcept;
    [[nodiscard]] SharedAudioBlock* get() const noexcept { return block_; }
    [[nodiscard]] int lastError() const noexcept { return lastError_; }
    static bool remove() noexcept;
private:
    int descriptor_{-1};
    SharedAudioBlock* block_{nullptr};
    int lastError_{0};
};

} // namespace lxtool::aes67
