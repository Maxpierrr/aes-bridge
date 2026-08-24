// SPDX-License-Identifier: GPL-3.0-only
#include "Engine/PTPClient.hpp"

#include "Core/UDPSocket.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <utility>

namespace lxtool::aes67 {
namespace {
using Clock = std::chrono::system_clock;
using SteadyClock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::int64_t nowNanoseconds() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

struct SyncSample final {
    PTPPortIdentity source;
    std::uint16_t sequence{0};
    std::int64_t ingressNanoseconds{0};
    std::optional<std::int64_t> originNanoseconds;
    std::int64_t correctionScaledNanoseconds{0};
    SteadyClock::time_point receivedAt{};
};
}

PTPClient::PTPClient(PTPClientConfig config, SharedAudioBlock& sharedBlock)
    : config_(std::move(config)), sharedBlock_(sharedBlock) {}

PTPClient::~PTPClient() { stop(); }

PTPPortIdentity PTPClient::identityFromAddress(const std::string& interfaceAddress) noexcept {
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    for (const unsigned char character : interfaceAddress) hash = (hash ^ character) * 1'099'511'628'211ULL;
    PTPPortIdentity identity;
    for (int index = 7; index >= 0; --index) {
        identity.clock[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(hash);
        hash >>= 8U;
    }
    identity.clock[0] = static_cast<std::uint8_t>((identity.clock[0] | 0x02U) & 0xfeU);
    identity.port = 1;
    return identity;
}

bool PTPClient::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return false;
    if (config_.interfaceAddress.empty()) { running_.store(false); return false; }
    if (config_.localIdentity.port == 0) config_.localIdentity = identityFromAddress(config_.interfaceAddress);
    sharedBlock_.ptpLocked.store(false, std::memory_order_release);
    try {
        thread_ = std::thread(&PTPClient::loop, this);
    } catch (...) {
        running_.store(false);
        return false;
    }
    return true;
}

void PTPClient::stop() {
    const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
    const bool hadThread = thread_.joinable();
    if (thread_.joinable()) thread_.join();
    if (wasRunning || hadThread) sharedBlock_.ptpLocked.store(false, std::memory_order_release);
}

void PTPClient::loop() {
    UDPSocket eventReceiver;
    UDPSocket generalReceiver;
    UDPSocket eventSender;
    if (!eventReceiver.openReceiver(config_.multicastAddress, config_.eventReceivePort, config_.interfaceAddress)
        || !generalReceiver.openReceiver(config_.multicastAddress, config_.generalReceivePort, config_.interfaceAddress)
        || !eventSender.openTransmitter(config_.multicastAddress, config_.eventTransmitPort, config_.interfaceAddress)) {
        sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);
        running_.store(false, std::memory_order_release);
        return;
    }

    std::array<std::uint8_t, 1600> wire{};
    std::optional<PTPPortIdentity> master;
    SteadyClock::time_point lastAnnounce{};
    SteadyClock::time_point lastSync{};
    std::optional<SyncSample> sync;
    std::uint16_t delaySequence = 0;
    std::optional<std::uint16_t> pendingDelaySequence;
    std::int64_t pendingDelayEgress = 0;
    auto nextDelayRequest = SteadyClock::now();
    std::optional<std::int64_t> filteredOffset;
    unsigned stableMeasurements = 0;

    auto process = [&](std::span<const std::uint8_t> bytes, std::int64_t ingressNanoseconds) {
        PTPMessage message;
        if (!PTPCodec::decode(bytes, config_.domain, message)) {
            sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        sharedBlock_.statistics.ptpMessages.fetch_add(1, std::memory_order_relaxed);
        const auto now = SteadyClock::now();
        if (message.type == PTPMessageType::announce) {
            if (!master || *master != message.source) {
                master = message.source;
                sync.reset();
                stableMeasurements = 0;
                sharedBlock_.ptpLocked.store(false, std::memory_order_release);
            }
            lastAnnounce = now;
            return;
        }
        if (!master || message.source != *master) return;
        if (message.type == PTPMessageType::sync && message.timestampNanoseconds) {
            sync = SyncSample{message.source, message.sequence, ingressNanoseconds,
                message.twoStep ? std::nullopt : message.timestampNanoseconds,
                message.correctionScaledNanoseconds, now};
            lastSync = now;
            return;
        }
        if (message.type == PTPMessageType::followUp && message.timestampNanoseconds && sync
            && sync->source == message.source && sync->sequence == message.sequence) {
            sync->originNanoseconds = message.timestampNanoseconds;
            sync->correctionScaledNanoseconds += message.correctionScaledNanoseconds;
            return;
        }
        if (message.type != PTPMessageType::delayResponse || !message.timestampNanoseconds
            || !message.requestingPort || *message.requestingPort != config_.localIdentity
            || !pendingDelaySequence || message.sequence != *pendingDelaySequence || !sync || !sync->originNanoseconds) return;
        const auto measurement = PTPCodec::calculateE2E(*sync->originNanoseconds, sync->ingressNanoseconds,
            pendingDelayEgress, *message.timestampNanoseconds, sync->correctionScaledNanoseconds,
            message.correctionScaledNanoseconds);
        pendingDelaySequence.reset();
        if (!measurement || measurement->meanPathDelayNanoseconds > 20'000'000LL) {
            stableMeasurements = 0;
            sharedBlock_.ptpLocked.store(false, std::memory_order_release);
            return;
        }
        if (filteredOffset && std::llabs(measurement->offsetNanoseconds - *filteredOffset) <= 2'000'000LL) {
            ++stableMeasurements;
            *filteredOffset = (*filteredOffset * 7 + measurement->offsetNanoseconds) / 8;
        } else {
            filteredOffset = measurement->offsetNanoseconds;
            stableMeasurements = 1;
        }
        sharedBlock_.statistics.ptpOffsetNanoseconds.store(*filteredOffset, std::memory_order_relaxed);
        sharedBlock_.statistics.ptpMeanPathDelayNanoseconds.store(measurement->meanPathDelayNanoseconds, std::memory_order_relaxed);
        const bool fresh = now - lastAnnounce < 3s && now - lastSync < 2s;
        sharedBlock_.ptpLocked.store(fresh && stableMeasurements >= 4, std::memory_order_release);
    };

    while (running_.load(std::memory_order_acquire)) {
        auto count = eventReceiver.receive(wire, 10ms);
        if (count > 0) process(std::span(wire).first(static_cast<std::size_t>(count)), nowNanoseconds());
        else if (count < 0) sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);
        count = generalReceiver.receive(wire, 10ms);
        if (count > 0) process(std::span(wire).first(static_cast<std::size_t>(count)), nowNanoseconds());
        else if (count < 0) sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);

        const auto now = SteadyClock::now();
        if (master && sync && sync->originNanoseconds && now >= nextDelayRequest) {
            ++delaySequence;
            pendingDelayEgress = nowNanoseconds();
            const auto request = PTPCodec::encodeDelayRequest(config_.localIdentity, delaySequence, config_.domain, pendingDelayEgress);
            if (eventSender.send(request) == static_cast<std::ptrdiff_t>(request.size())) pendingDelaySequence = delaySequence;
            else sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);
            nextDelayRequest = now + 250ms;
        }
        if ((lastAnnounce != SteadyClock::time_point{} && now - lastAnnounce >= 3s)
            || (lastSync != SteadyClock::time_point{} && now - lastSync >= 2s)) {
            stableMeasurements = 0;
            sharedBlock_.ptpLocked.store(false, std::memory_order_release);
        }
    }
    sharedBlock_.ptpLocked.store(false, std::memory_order_release);
}

} // namespace lxtool::aes67
