// SPDX-License-Identifier: GPL-3.0-only
#include "Engine/LiveEngine.hpp"

#include "Core/JitterBuffer.hpp"
#include "Core/IPv4Address.hpp"
#include "Core/L24Codec.hpp"
#include "Core/RTPPacket.hpp"
#include "Core/ReconnectPolicy.hpp"
#include "Core/SAP.hpp"
#include "Core/SDP.hpp"
#include "Core/SessionDirectory.hpp"
#include "Core/UDPSocket.hpp"
#include "Engine/PTPClient.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <utility>

namespace lxtool::aes67 {
using namespace std::chrono_literals;

namespace {
std::uint64_t unixMillisecondsNow() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string bankAddress(const std::string& base, std::size_t bank) {
    const auto parsed = IPv4Address::parse(base);
    if (!parsed) return {};
    if (!parsed->isMulticast()) return base;
    std::uint32_t value = (static_cast<std::uint32_t>(parsed->octets[0]) << 24U)
        | (static_cast<std::uint32_t>(parsed->octets[1]) << 16U)
        | (static_cast<std::uint32_t>(parsed->octets[2]) << 8U)
        | static_cast<std::uint32_t>(parsed->octets[3]);
    if (bank > static_cast<std::size_t>(UINT32_MAX - value)) return {};
    value += static_cast<std::uint32_t>(bank);
    IPv4Address shifted{{static_cast<std::uint8_t>(value >> 24U), static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value)}};
    return shifted.toString();
}

std::uint16_t bankPort(std::uint16_t base, std::uint16_t stride, std::size_t bank) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::size_t>(base) + static_cast<std::size_t>(stride) * bank);
}

std::uint32_t rtpTimestampForNanoseconds(std::int64_t nanoseconds) noexcept {
    if (nanoseconds < 0) nanoseconds = 0;
    const auto seconds = static_cast<std::uint64_t>(nanoseconds / 1'000'000'000LL);
    const auto remainder = static_cast<std::uint64_t>(nanoseconds % 1'000'000'000LL);
    const auto samples = seconds * kSampleRate + remainder * kSampleRate / 1'000'000'000ULL;
    return static_cast<std::uint32_t>(samples);
}
}

LiveEngine::LiveEngine(LiveEngineConfig config) : config_(std::move(config)) {}
LiveEngine::~LiveEngine() { stop(); }

std::string LiveEngine::interfaceIPv4(const std::string& name) {
    ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0) return {};
    std::string result;
    for (auto* item = head; item && result.empty(); item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || name != item->ifa_name) continue;
        char address[INET_ADDRSTRLEN]{};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) result = address;
    }
    freeifaddrs(head);
    return result;
}

bool LiveEngine::start() {
    if (running_.exchange(true)) return false;
    if (config_.interfaceAddress.empty()) config_.interfaceAddress = interfaceIPv4(config_.interfaceName);
    if (config_.streamCount == 0 || config_.streamCount > kStreamBankCount) { running_.store(false); return false; }
    const auto lastPortOffset = static_cast<std::size_t>(config_.portStride) * (config_.streamCount - 1U);
    if (config_.interfaceAddress.empty()
        || lastPortOffset > UINT16_MAX - config_.rxPort || lastPortOffset > UINT16_MAX - config_.txPort
        || bankAddress(config_.rxAddress, config_.streamCount - 1U).empty()
        || bankAddress(config_.txAddress, config_.streamCount - 1U).empty()
        || !sharedMemory_.open(true)) { running_.store(false); return false; }
    auto* block = sharedMemory_.get();
    block->engineRunning.store(true, std::memory_order_release);
    block->ptpLocked.store(false, std::memory_order_release);
    block->activeStreamCount.store(static_cast<std::uint32_t>(config_.streamCount), std::memory_order_release);
    activeReceivers_.store(0);
    activeTransmitters_.store(0);
    const auto steadyNow = std::chrono::steady_clock::now();
    transmitEpoch_ = steadyNow + 20ms;
    transmitSystemEpochNanoseconds_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 20'000'000LL;
    streams_.reserve(config_.streamCount);
    try {
        for (std::size_t bank = 0; bank < config_.streamCount; ++bank) {
            streams_.push_back(std::make_unique<StreamRuntime>(config_.jitterPackets));
            auto& runtime = *streams_.back();
            runtime.receiveThread = std::thread(&LiveEngine::receiveLoop, this, bank, std::ref(runtime));
            runtime.consumeThread = std::thread(&LiveEngine::consumeLoop, this, bank, std::ref(runtime));
            runtime.transmitThread = std::thread(&LiveEngine::transmitLoop, this, bank, std::ref(runtime));
        }
    } catch (...) {
        stop();
        return false;
    }
    if (config_.enableSAPPublication) sapPublishThread_ = std::thread(&LiveEngine::sapPublishLoop, this);
    if (config_.enableSAPDiscovery) sapDiscoveryThread_ = std::thread(&LiveEngine::sapDiscoveryLoop, this);
    if (config_.enablePTP) {
        PTPClientConfig ptpConfig;
        ptpConfig.multicastAddress = config_.ptpAddress;
        ptpConfig.interfaceAddress = config_.interfaceAddress;
        ptpConfig.eventReceivePort = config_.ptpEventPort;
        ptpConfig.eventTransmitPort = config_.ptpEventPort;
        ptpConfig.generalReceivePort = config_.ptpGeneralPort;
        ptpClient_ = std::make_unique<PTPClient>(std::move(ptpConfig), *block);
        if (!ptpClient_->start()) ptpClient_.reset();
    }
    return true;
}

void LiveEngine::stop() {
    if (!running_.exchange(false)) return;
    for (auto& stream : streams_) {
        if (stream->receiveThread.joinable()) stream->receiveThread.join();
        if (stream->consumeThread.joinable()) stream->consumeThread.join();
        if (stream->transmitThread.joinable()) stream->transmitThread.join();
    }
    if (sapPublishThread_.joinable()) sapPublishThread_.join();
    if (sapDiscoveryThread_.joinable()) sapDiscoveryThread_.join();
    if (ptpClient_) ptpClient_->stop();
    ptpClient_.reset();
    if (auto* block = sharedMemory_.get()) {
        block->rxActive.store(false); block->txActive.store(false); block->activeStreamCount.store(0);
        block->engineRunning.store(false, std::memory_order_release);
    }
    streams_.clear();
}

void LiveEngine::setRxActive(StreamRuntime& runtime, bool active) noexcept {
    if (runtime.rxActive == active) return;
    runtime.rxActive = active;
    auto* block = sharedMemory_.get();
    if (active) {
        activeReceivers_.fetch_add(1, std::memory_order_acq_rel);
        block->rxActive.store(true, std::memory_order_release);
    } else if (activeReceivers_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        block->rxActive.store(false, std::memory_order_release);
    }
}

void LiveEngine::setTxActive(StreamRuntime& runtime, bool active) noexcept {
    if (runtime.txActive == active) return;
    runtime.txActive = active;
    auto* block = sharedMemory_.get();
    if (active) {
        activeTransmitters_.fetch_add(1, std::memory_order_acq_rel);
        block->txActive.store(true, std::memory_order_release);
    } else if (activeTransmitters_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        block->txActive.store(false, std::memory_order_release);
    }
}

void LiveEngine::receiveLoop(std::size_t bank, StreamRuntime& runtime) {
    auto* block = sharedMemory_.get();
    ReconnectPolicy reconnect;
    std::array<std::uint8_t, 1600> wire{};
    const auto address = bankAddress(config_.rxAddress, bank);
    const auto port = bankPort(config_.rxPort, config_.portStride, bank);
    while (running_) {
        UDPSocket socket;
        if (!socket.openReceiver(address, port, config_.interfaceAddress, config_.rxSourceAddress)) {
            setRxActive(runtime, false); block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); continue;
        }
        reconnect.connected();
        setRxActive(runtime, false);
        auto lastPacket = std::chrono::steady_clock::time_point::min();
        std::uint32_t currentSSRC = 0;
        bool haveSSRC = false;
        while (running_) {
            const auto count = socket.receive(wire, 100ms);
            if (count == 0) {
                if (lastPacket != std::chrono::steady_clock::time_point::min()
                    && std::chrono::steady_clock::now() - lastPacket > 2s) setRxActive(runtime, false);
                continue;
            }
            if (count < 0) break;
            RTPPacket packet;
            if (!RTPCodec::decode(std::span(wire).first(static_cast<std::size_t>(count)), packet)
                || packet.payloadType != config_.rxPayloadType || packet.payload.size() != kPayloadBytes) {
                block->statistics.malformedPackets.fetch_add(1); continue;
            }
            const auto arrival = std::chrono::steady_clock::now();
            const bool resumedAfterGap = lastPacket != std::chrono::steady_clock::time_point::min() && arrival - lastPacket > 2s;
            const bool sourceChanged = haveSSRC && packet.ssrc != currentSSRC;
            if (resumedAfterGap || sourceChanged) {
                runtime.jitterResetRequested.store(true, std::memory_order_release);
                setRxActive(runtime, false);
                block->statistics.reconnects.fetch_add(1, std::memory_order_relaxed);
                currentSSRC = packet.ssrc;
                haveSSRC = true;
                lastPacket = arrival;
                continue;
            }
            if (runtime.jitterResetRequested.load(std::memory_order_acquire)) continue;
            if (runtime.jitter.push(packet.sequence, packet.payload)) {
                block->statistics.rxPackets.fetch_add(1);
                setRxActive(runtime, true);
                lastPacket = arrival;
                currentSSRC = packet.ssrc;
                haveSSRC = true;
            }
            else block->statistics.ringOverruns.fetch_add(1);
        }
        setRxActive(runtime, false);
        if (running_) { block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); }
    }
}

void LiveEngine::consumeLoop(std::size_t bank, StreamRuntime& runtime) {
    auto* block = sharedMemory_.get();
    std::array<std::uint8_t, 1200> payload{};
    std::array<float, kFramesPerPacket * kChannels> interleaved{};
    std::array<float, kFramesPerPacket> channel{};
    auto next = std::chrono::steady_clock::now();
    while (running_) {
        if (runtime.jitterResetRequested.exchange(false, std::memory_order_acq_rel)) runtime.jitter.reset();
        next += 1ms;
        std::this_thread::sleep_until(next);
        if (std::chrono::steady_clock::now() - next > 10ms) next = std::chrono::steady_clock::now();
        std::size_t length = 0; std::uint16_t sequence = 0;
        if (!runtime.jitter.pop(payload, length, sequence)) {
            if (runtime.jitter.ready() && runtime.jitter.buffered() > 0) { runtime.jitter.skipMissing(); block->statistics.packetsLost.fetch_add(1); }
            continue;
        }
        if (length != kPayloadBytes || !L24Codec::decode(std::span(payload).first(length), interleaved)) { block->statistics.malformedPackets.fetch_add(1); continue; }
        for (std::size_t ch = 0; ch < kChannels; ++ch) {
            for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) channel[frame] = interleaved[frame * kChannels + ch];
            const auto virtualChannel = bank * kAES67ChannelsPerStream + ch;
            if (block->networkToCoreAudio[virtualChannel].write(channel) != channel.size()) block->statistics.ringOverruns.fetch_add(1);
        }
    }
}

void LiveEngine::transmitLoop(std::size_t bank, StreamRuntime& runtime) {
    auto* block = sharedMemory_.get();
    ReconnectPolicy reconnect;
    std::array<float, kFramesPerPacket * kChannels> interleaved{};
    std::array<float, kFramesPerPacket> channel{};
    std::array<std::uint8_t, RTPCodec::kFixedHeaderBytes + kPayloadBytes> wire{};
    RTPPacket packet; packet.payloadType = config_.txPayloadType;
    packet.ssrc = 0x41455342U + static_cast<std::uint32_t>(bank); packet.payload.resize(kPayloadBytes);
    const auto address = bankAddress(config_.txAddress, bank);
    const auto port = bankPort(config_.txPort, config_.portStride, bank);
    auto nextDeadline = [this] {
        const auto now = std::chrono::steady_clock::now();
        if (now < transmitEpoch_) return transmitEpoch_;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - transmitEpoch_).count();
        return transmitEpoch_ + std::chrono::milliseconds(elapsed + 1);
    };
    while (running_) {
        UDPSocket socket;
        if (!socket.openTransmitter(address, port, config_.interfaceAddress)) {
            setTxActive(runtime, false); block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); continue;
        }
        reconnect.connected(); setTxActive(runtime, true);
        auto next = nextDeadline();
        while (running_) {
            std::this_thread::sleep_until(next);
            if (std::chrono::steady_clock::now() - next > 10ms) next = nextDeadline();
            bool underrun = false;
            for (std::size_t ch = 0; ch < kChannels; ++ch) {
                const auto virtualChannel = bank * kAES67ChannelsPerStream + ch;
                const auto count = block->coreAudioToNetwork[virtualChannel].read(channel);
                if (count < channel.size()) { std::fill(channel.begin() + static_cast<std::ptrdiff_t>(count), channel.end(), 0.0F); underrun = true; }
                for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) interleaved[frame * kChannels + ch] = channel[frame];
            }
            if (underrun && block->ioRunning.load()) block->statistics.outputUnderruns.fetch_add(1);
            L24Codec::encode(interleaved, packet.payload);
            packet.sequence = static_cast<std::uint16_t>(packet.sequence + 1U);
            const auto scheduledNanoseconds = transmitSystemEpochNanoseconds_
                + std::chrono::duration_cast<std::chrono::nanoseconds>(next - transmitEpoch_).count();
            const auto offset = block->ptpLocked.load(std::memory_order_acquire)
                ? block->statistics.ptpOffsetNanoseconds.load(std::memory_order_relaxed) : 0;
            packet.timestamp = rtpTimestampForNanoseconds(scheduledNanoseconds - offset);
            std::size_t written = 0;
            if (!RTPCodec::encode(packet, wire, written) || socket.send(std::span(wire).first(written)) != static_cast<std::ptrdiff_t>(written)) break;
            block->statistics.txPackets.fetch_add(1);
            next += 1ms;
        }
        setTxActive(runtime, false);
        if (running_) { block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); }
    }
}

void LiveEngine::sapPublishLoop() {
    std::vector<SAPMessage> announcements;
    std::vector<std::vector<std::uint8_t>> encodedAnnouncements;
    announcements.reserve(config_.streamCount);
    encodedAnnouncements.reserve(config_.streamCount);
    for (std::size_t bank = 0; bank < config_.streamCount; ++bank) {
        const auto firstChannel = bank * kAES67ChannelsPerStream + 1U;
        const auto lastChannel = firstChannel + kAES67ChannelsPerStream - 1U;
        SessionDescription session;
        session.name = "AES-Bridge-Outputs-" + std::to_string(firstChannel) + '-' + std::to_string(lastChannel);
        session.originAddress = config_.interfaceAddress;
        session.sourceAddress = config_.interfaceAddress;
        session.multicastAddress = bankAddress(config_.txAddress, bank);
        session.port = bankPort(config_.txPort, config_.portStride, bank);
        session.payloadType = config_.txPayloadType;
        announcements.push_back(SAPMessage{false, 0, config_.interfaceAddress, "application/sdp", SDP::generate(session)});
        encodedAnnouncements.push_back(SAP::encode(announcements.back()));
    }
    UDPSocket publisher;
    if (!publisher.openTransmitter(config_.sapAddress, config_.sapPort, config_.interfaceAddress)) return;
    while (running_) {
        for (const auto& bytes : encodedAnnouncements) publisher.send(bytes);
        for (int i = 0; i < 50 && running_; ++i) std::this_thread::sleep_for(100ms);
    }
    for (auto& announcement : announcements) {
        announcement.deletion = true;
        publisher.send(SAP::encode(announcement));
    }
}

void LiveEngine::sapDiscoveryLoop() {
    auto* block = sharedMemory_.get();
    ReconnectPolicy reconnect;
    std::array<std::uint8_t, 65'536> wire{};
    while (running_) {
        UDPSocket listener;
        if (!listener.openReceiver(config_.sapAddress, config_.sapPort, config_.interfaceAddress)) {
            block->statistics.reconnects.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(reconnect.nextDelay());
            continue;
        }
        reconnect.connected();
        while (running_) {
            const auto count = listener.receive(wire, 250ms);
            const auto now = unixMillisecondsNow();
            const auto timeoutMilliseconds = static_cast<std::uint64_t>(config_.sapSessionTimeoutSeconds) * 1'000U;
            SessionDirectory::expire(*block, now > timeoutMilliseconds ? now - timeoutMilliseconds : 0);
            if (count == 0) continue;
            if (count < 0) break;
            SAPMessage message;
            std::string error;
            if (!SAP::decode(std::span(wire).first(static_cast<std::size_t>(count)), message, &error)
                || message.payloadType != "application/sdp") {
                block->statistics.sapMalformedPackets.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            const auto session = SDP::parse(message.sdp, &error);
            if (!session || !SDP::validateLXToolProfile(*session).empty()) {
                block->statistics.sapMalformedPackets.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (session->originAddress == config_.interfaceAddress && session->name.starts_with("AES-Bridge-Outputs-")) continue;
            if (message.deletion) SessionDirectory::erase(*block, message.messageHash, session->originAddress);
            else SessionDirectory::upsert(*block, message.messageHash, *session, now);
        }
        if (running_) {
            block->statistics.reconnects.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(reconnect.nextDelay());
        }
    }
}

} // namespace lxtool::aes67
