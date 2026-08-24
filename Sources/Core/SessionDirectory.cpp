// SPDX-License-Identifier: GPL-3.0-only
#include "Core/SessionDirectory.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace lxtool::aes67 {
namespace {
template <std::size_t Capacity>
void storeText(std::array<std::atomic<char>, Capacity>& destination, std::string_view source) noexcept {
    const auto count = std::min(source.size(), Capacity - 1);
    for (std::size_t i = 0; i < count; ++i) destination[i].store(source[i], std::memory_order_relaxed);
    for (std::size_t i = count; i < Capacity; ++i) destination[i].store('\0', std::memory_order_relaxed);
}

template <std::size_t Capacity>
std::string loadText(const std::array<std::atomic<char>, Capacity>& source) {
    std::string result;
    result.reserve(Capacity - 1);
    for (const auto& character : source) {
        const char value = character.load(std::memory_order_relaxed);
        if (value == '\0') break;
        result.push_back(value);
    }
    return result;
}

bool snapshotSlot(const SharedDiscoveredSession& slot, DiscoveredSessionSnapshot& result) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto before = slot.revision.load(std::memory_order_acquire);
        if ((before & 1U) != 0U || !slot.active.load(std::memory_order_acquire)) continue;
        DiscoveredSessionSnapshot candidate;
        candidate.messageHash = slot.messageHash.load(std::memory_order_relaxed);
        candidate.name = loadText(slot.name);
        candidate.originAddress = loadText(slot.originAddress);
        candidate.sourceAddress = loadText(slot.sourceAddress);
        candidate.multicastAddress = loadText(slot.multicastAddress);
        candidate.port = slot.port.load(std::memory_order_relaxed);
        candidate.channels = slot.channels.load(std::memory_order_relaxed);
        candidate.sampleRate = slot.sampleRate.load(std::memory_order_relaxed);
        candidate.framesPerPacket = slot.framesPerPacket.load(std::memory_order_relaxed);
        candidate.payloadType = slot.payloadType.load(std::memory_order_relaxed);
        candidate.ptpDomain = slot.ptpDomain.load(std::memory_order_relaxed);
        candidate.lastSeenUnixMilliseconds = slot.lastSeenUnixMilliseconds.load(std::memory_order_relaxed);
        const auto after = slot.revision.load(std::memory_order_acquire);
        if (before == after && (after & 1U) == 0U && slot.active.load(std::memory_order_acquire)) {
            result = std::move(candidate);
            return true;
        }
    }
    return false;
}

void beginWrite(SharedDiscoveredSession& slot) noexcept {
    slot.revision.fetch_add(1, std::memory_order_acq_rel);
    slot.active.store(false, std::memory_order_release);
}

void endWrite(SharedDiscoveredSession& slot, bool active) noexcept {
    slot.active.store(active, std::memory_order_release);
    slot.revision.fetch_add(1, std::memory_order_release);
}
}

void SessionDirectory::upsert(SharedAudioBlock& block, std::uint16_t messageHash,
    const SessionDescription& session, std::uint64_t nowUnixMilliseconds) noexcept {
    SharedDiscoveredSession* target = nullptr;
    SharedDiscoveredSession* oldest = &block.discoveredSessions.front();
    auto oldestTime = std::numeric_limits<std::uint64_t>::max();
    for (auto& slot : block.discoveredSessions) {
        DiscoveredSessionSnapshot current;
        if (!snapshotSlot(slot, current)) {
            if (!target) target = &slot;
            continue;
        }
        if (current.messageHash == messageHash && current.originAddress == session.originAddress) {
            target = &slot;
            break;
        }
        if (current.lastSeenUnixMilliseconds < oldestTime) {
            oldest = &slot;
            oldestTime = current.lastSeenUnixMilliseconds;
        }
    }
    if (!target) target = oldest;
    beginWrite(*target);
    target->messageHash.store(messageHash, std::memory_order_relaxed);
    target->port.store(session.port, std::memory_order_relaxed);
    target->channels.store(session.channels, std::memory_order_relaxed);
    target->sampleRate.store(session.sampleRate, std::memory_order_relaxed);
    target->framesPerPacket.store(session.framesPerPacket, std::memory_order_relaxed);
    target->payloadType.store(session.payloadType, std::memory_order_relaxed);
    target->ptpDomain.store(session.ptpDomain, std::memory_order_relaxed);
    target->lastSeenUnixMilliseconds.store(nowUnixMilliseconds, std::memory_order_relaxed);
    storeText(target->name, session.name);
    storeText(target->originAddress, session.originAddress);
    storeText(target->sourceAddress, session.sourceAddress);
    storeText(target->multicastAddress, session.multicastAddress);
    endWrite(*target, true);
}

void SessionDirectory::erase(SharedAudioBlock& block, std::uint16_t messageHash,
    const std::string& originAddress) noexcept {
    for (auto& slot : block.discoveredSessions) {
        DiscoveredSessionSnapshot current;
        if (!snapshotSlot(slot, current) || current.messageHash != messageHash || current.originAddress != originAddress) continue;
        beginWrite(slot);
        endWrite(slot, false);
    }
}

void SessionDirectory::expire(SharedAudioBlock& block, std::uint64_t oldestAllowedUnixMilliseconds) noexcept {
    for (auto& slot : block.discoveredSessions) {
        DiscoveredSessionSnapshot current;
        if (!snapshotSlot(slot, current) || current.lastSeenUnixMilliseconds >= oldestAllowedUnixMilliseconds) continue;
        beginWrite(slot);
        endWrite(slot, false);
    }
}

std::vector<DiscoveredSessionSnapshot> SessionDirectory::snapshots(const SharedAudioBlock& block) {
    std::vector<DiscoveredSessionSnapshot> result;
    result.reserve(kMaximumDiscoveredSessions);
    for (const auto& slot : block.discoveredSessions) {
        DiscoveredSessionSnapshot session;
        if (snapshotSlot(slot, session)) result.push_back(std::move(session));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.name != right.name) return left.name < right.name;
        return left.originAddress < right.originAddress;
    });
    return result;
}

} // namespace lxtool::aes67
