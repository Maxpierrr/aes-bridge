// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/Constants.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace lxtool::aes67 {

template <std::size_t SlotCount = 64, std::size_t MaxPayload = 1200>
class JitterBuffer final {
    static_assert(SlotCount >= 8);
public:
    explicit JitterBuffer(std::size_t targetPackets = kDefaultJitterPackets) noexcept
        : targetPackets_(targetPackets < 2 ? 2 : (targetPackets >= SlotCount ? SlotCount - 1 : targetPackets)) {}

    bool push(std::uint16_t sequence, std::span<const std::uint8_t> payload) noexcept {
        if (payload.size() > MaxPayload) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
        auto& slot = slots_[sequence % SlotCount];
        State expected = State::empty;
        if (!slot.state.compare_exchange_strong(expected, State::writing, std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed); return false;
        }
        slot.sequence = sequence;
        slot.length = payload.size();
        std::copy(payload.begin(), payload.end(), slot.payload.begin());
        slot.state.store(State::ready, std::memory_order_release);
        buffered_.fetch_add(1, std::memory_order_relaxed);
        if (!started_.load(std::memory_order_relaxed)) {
            bool unset = false;
            if (hasFirst_.compare_exchange_strong(unset, true)) firstSequence_.store(sequence);
            if (buffered_.load(std::memory_order_acquire) >= targetPackets_) {
                expectedSequence_.store(firstSequence_.load());
                started_.store(true, std::memory_order_release);
            }
        }
        return true;
    }

    bool pop(std::span<std::uint8_t> destination, std::size_t& length, std::uint16_t& sequence) noexcept {
        length = 0;
        if (!started_.load(std::memory_order_acquire)) return false;
        sequence = expectedSequence_.load(std::memory_order_relaxed);
        auto& slot = slots_[sequence % SlotCount];
        State expected = State::ready;
        if (!slot.state.compare_exchange_strong(expected, State::reading, std::memory_order_acquire)) return false;
        if (slot.sequence != sequence || slot.length > destination.size()) {
            slot.state.store(State::empty, std::memory_order_release);
            buffered_.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        std::copy_n(slot.payload.begin(), slot.length, destination.begin());
        length = slot.length;
        slot.state.store(State::empty, std::memory_order_release);
        buffered_.fetch_sub(1, std::memory_order_relaxed);
        expectedSequence_.store(static_cast<std::uint16_t>(sequence + 1U), std::memory_order_relaxed);
        return true;
    }

    void skipMissing() noexcept {
        if (started_.load(std::memory_order_acquire)) {
            expectedSequence_.store(static_cast<std::uint16_t>(expectedSequence_.load() + 1U));
            lost_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    [[nodiscard]] std::size_t buffered() const noexcept { return buffered_.load(); }
    [[nodiscard]] std::uint64_t lost() const noexcept { return lost_.load(); }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_.load(); }
    [[nodiscard]] bool ready() const noexcept { return started_.load(); }

    void reset() noexcept {
        for (auto& slot : slots_) slot.state.store(State::empty, std::memory_order_relaxed);
        buffered_.store(0); lost_.store(0); dropped_.store(0); hasFirst_.store(false); started_.store(false);
    }

private:
    enum class State : std::uint8_t { empty, writing, ready, reading };
    struct Slot {
        std::atomic<State> state{State::empty};
        std::uint16_t sequence{0};
        std::size_t length{0};
        std::array<std::uint8_t, MaxPayload> payload{};
    };
    std::array<Slot, SlotCount> slots_{};
    const std::size_t targetPackets_;
    std::atomic<std::size_t> buffered_{0};
    std::atomic<std::uint16_t> firstSequence_{0};
    std::atomic<std::uint16_t> expectedSequence_{0};
    std::atomic<bool> hasFirst_{false};
    std::atomic<bool> started_{false};
    std::atomic<std::uint64_t> lost_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace lxtool::aes67
