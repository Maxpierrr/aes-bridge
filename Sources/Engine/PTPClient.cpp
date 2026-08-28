// SPDX-License-Identifier: GPL-3.0-only
#include "Engine/PTPClient.hpp"

#include "Core/UDPSocket.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <tuple>
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

using AnnounceRank = std::tuple<std::uint8_t, std::uint8_t, std::uint8_t, std::uint16_t,
    std::uint8_t, std::array<std::uint8_t, 8>, std::uint16_t,
    std::array<std::uint8_t, 8>, std::uint16_t>;

AnnounceRank announceRank(const PTPMessage& message) noexcept {
    return {message.grandmasterPriority1, message.grandmasterClockClass,
        message.grandmasterClockAccuracy, message.grandmasterOffsetScaledLogVariance,
        message.grandmasterPriority2, message.grandmasterIdentity, message.stepsRemoved,
        message.source.clock, message.source.port};
}
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
    sharedBlock_.statistics.ptpOffsetNanoseconds.store(0, std::memory_order_relaxed);
    sharedBlock_.statistics.ptpMeanPathDelayNanoseconds.store(0, std::memory_order_relaxed);
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
    std::optional<AnnounceRank> masterRank;
    SteadyClock::time_point lastAnnounce{};
    SteadyClock::time_point lastSync{};
    std::optional<SyncSample> sync;
    std::uint16_t delaySequence = 0;
    std::optional<std::uint16_t> pendingDelaySequence;
    std::int64_t pendingDelayEgress = 0;
    auto nextDelayRequest = SteadyClock::now();
    std::optional<std::int64_t> filteredOffset;
    unsigned stableMeasurements = 0;
    SteadyClock::time_point lastValidMeasurement{};
#if defined(_WIN32)
    // The hardware trace is a macOS development aid. Avoid MSVC's deprecated
    // getenv API in the portable backend, which is built with warnings fatal.
    constexpr bool traceMeasurements = false;
#else
    const bool traceMeasurements = std::getenv("AES_BRIDGE_PTP_TRACE") != nullptr;
#endif

    auto process = [&](std::span<const std::uint8_t> bytes, std::int64_t ingressNanoseconds) {
        // Dante can emit PTPv1 and PTPv2 concurrently on these ports. Traffic
        // from another version/domain is expected and must not be reported as
        // malformed PTPv2 for this AES67 clock domain.
        if (bytes.size() < PTPCodec::kHeaderBytes) {
            sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if ((bytes[1] & 0x0fU) != 2U || bytes[4] != config_.domain) return;
        PTPMessage message;
        if (!PTPCodec::decode(bytes, config_.domain, message)) {
            sharedBlock_.statistics.ptpErrors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        sharedBlock_.statistics.ptpMessages.fetch_add(1, std::memory_order_relaxed);
        const auto now = SteadyClock::now();
        if (message.type == PTPMessageType::announce) {
            const auto rank = announceRank(message);
            const bool masterExpired = lastAnnounce != SteadyClock::time_point{} && now - lastAnnounce >= 3s;
            const bool select = !master || masterExpired || message.source == *master
                || !masterRank || rank < *masterRank;
            if (!select) return;
            if (!master || *master != message.source) {
                if (traceMeasurements) {
                    std::cerr << "PTPv2 maitre selectionne: ";
                    for (const auto byte : message.source.clock) std::cerr << std::hex << static_cast<unsigned>(byte);
                    std::cerr << std::dec << ':' << message.source.port << '\n';
                }
                master = message.source;
                sync.reset();
                stableMeasurements = 0;
                sharedBlock_.ptpLocked.store(false, std::memory_order_release);
            }
            masterRank = rank;
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
        if (traceMeasurements && measurement) {
            std::cerr << "PTPv2 mesure: offset=" << measurement->offsetNanoseconds
                      << " ns delai=" << measurement->meanPathDelayNanoseconds
                      << " ns stables=" << stableMeasurements << '\n';
        }
        if (!measurement || measurement->meanPathDelayNanoseconds > 20'000'000LL) {
            if (traceMeasurements) std::cerr << "PTPv2 mesure logicielle ignoree\n";
            if (lastValidMeasurement != SteadyClock::time_point{} && now - lastValidMeasurement >= 2s) {
                stableMeasurements = 0;
                sharedBlock_.ptpLocked.store(false, std::memory_order_release);
                sharedBlock_.statistics.ptpOffsetNanoseconds.store(0, std::memory_order_relaxed);
                sharedBlock_.statistics.ptpMeanPathDelayNanoseconds.store(0, std::memory_order_relaxed);
            }
            return;
        }
        if (filteredOffset && std::llabs(measurement->offsetNanoseconds - *filteredOffset) <= 2'000'000LL) {
            ++stableMeasurements;
            // Equivalent to (7 * old + sample) / 8 without overflowing when
            // a grandmaster uses an epoch far from the host system clock.
            *filteredOffset += (measurement->offsetNanoseconds - *filteredOffset) / 8;
        } else {
            filteredOffset = measurement->offsetNanoseconds;
            stableMeasurements = 1;
        }
        sharedBlock_.statistics.ptpOffsetNanoseconds.store(*filteredOffset, std::memory_order_relaxed);
        sharedBlock_.statistics.ptpMeanPathDelayNanoseconds.store(measurement->meanPathDelayNanoseconds, std::memory_order_relaxed);
        lastValidMeasurement = now;
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
            if (traceMeasurements && stableMeasurements != 0) {
                std::cerr << "PTPv2 stabilite reinitialisee: annonce="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAnnounce).count()
                          << " ms sync="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSync).count()
                          << " ms\n";
            }
            stableMeasurements = 0;
            sharedBlock_.ptpLocked.store(false, std::memory_order_release);
            sharedBlock_.statistics.ptpOffsetNanoseconds.store(0, std::memory_order_relaxed);
            sharedBlock_.statistics.ptpMeanPathDelayNanoseconds.store(0, std::memory_order_relaxed);
        }
    }
    sharedBlock_.ptpLocked.store(false, std::memory_order_release);
    sharedBlock_.statistics.ptpOffsetNanoseconds.store(0, std::memory_order_relaxed);
    sharedBlock_.statistics.ptpMeanPathDelayNanoseconds.store(0, std::memory_order_relaxed);
}

} // namespace lxtool::aes67
