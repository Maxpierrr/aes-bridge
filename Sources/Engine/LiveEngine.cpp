// SPDX-License-Identifier: GPL-3.0-only
#include "Engine/LiveEngine.hpp"

#include "Core/JitterBuffer.hpp"
#include "Core/L24Codec.hpp"
#include "Core/RTPPacket.hpp"
#include "Core/ReconnectPolicy.hpp"
#include "Core/SAP.hpp"
#include "Core/SDP.hpp"
#include "Core/SessionDirectory.hpp"
#include "Core/UDPSocket.hpp"

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
}

LiveEngine::LiveEngine(LiveEngineConfig config) : config_(std::move(config)), jitter_(config_.jitterPackets) {}
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
    if (config_.interfaceAddress.empty() || !sharedMemory_.open(true)) { running_.store(false); return false; }
    auto* block = sharedMemory_.get();
    block->engineRunning.store(true, std::memory_order_release);
    block->ptpLocked.store(false, std::memory_order_release);
    receiveThread_ = std::thread(&LiveEngine::receiveLoop, this);
    consumeThread_ = std::thread(&LiveEngine::consumeLoop, this);
    transmitThread_ = std::thread(&LiveEngine::transmitLoop, this);
    if (config_.enableSAPPublication) sapPublishThread_ = std::thread(&LiveEngine::sapPublishLoop, this);
    if (config_.enableSAPDiscovery) sapDiscoveryThread_ = std::thread(&LiveEngine::sapDiscoveryLoop, this);
    return true;
}

void LiveEngine::stop() {
    if (!running_.exchange(false)) return;
    if (receiveThread_.joinable()) receiveThread_.join();
    if (consumeThread_.joinable()) consumeThread_.join();
    if (transmitThread_.joinable()) transmitThread_.join();
    if (sapPublishThread_.joinable()) sapPublishThread_.join();
    if (sapDiscoveryThread_.joinable()) sapDiscoveryThread_.join();
    if (auto* block = sharedMemory_.get()) {
        block->rxActive.store(false); block->txActive.store(false); block->engineRunning.store(false, std::memory_order_release);
    }
}

void LiveEngine::receiveLoop() {
    auto* block = sharedMemory_.get();
    ReconnectPolicy reconnect;
    std::array<std::uint8_t, 1600> wire{};
    while (running_) {
        UDPSocket socket;
        if (!socket.openReceiver(config_.rxAddress, config_.rxPort, config_.interfaceAddress, config_.rxSourceAddress)) {
            block->rxActive.store(false); block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); continue;
        }
        reconnect.connected();
        block->rxActive.store(false);
        auto lastPacket = std::chrono::steady_clock::time_point::min();
        std::uint32_t currentSSRC = 0;
        bool haveSSRC = false;
        while (running_) {
            const auto count = socket.receive(wire, 100ms);
            if (count == 0) {
                if (lastPacket != std::chrono::steady_clock::time_point::min()
                    && std::chrono::steady_clock::now() - lastPacket > 2s) block->rxActive.store(false);
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
                jitterResetRequested_.store(true, std::memory_order_release);
                block->rxActive.store(false, std::memory_order_release);
                block->statistics.reconnects.fetch_add(1, std::memory_order_relaxed);
                currentSSRC = packet.ssrc;
                haveSSRC = true;
                lastPacket = arrival;
                continue;
            }
            if (jitterResetRequested_.load(std::memory_order_acquire)) continue;
            if (jitter_.push(packet.sequence, packet.payload)) {
                block->statistics.rxPackets.fetch_add(1);
                block->rxActive.store(true);
                lastPacket = arrival;
                currentSSRC = packet.ssrc;
                haveSSRC = true;
            }
            else block->statistics.ringOverruns.fetch_add(1);
        }
        block->rxActive.store(false);
        if (running_) { block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); }
    }
}

void LiveEngine::consumeLoop() {
    auto* block = sharedMemory_.get();
    std::array<std::uint8_t, 1200> payload{};
    std::array<float, kFramesPerPacket * kChannels> interleaved{};
    std::array<float, kFramesPerPacket> channel{};
    auto next = std::chrono::steady_clock::now();
    while (running_) {
        if (jitterResetRequested_.exchange(false, std::memory_order_acq_rel)) jitter_.reset();
        next += 1ms;
        std::this_thread::sleep_until(next);
        if (std::chrono::steady_clock::now() - next > 10ms) next = std::chrono::steady_clock::now();
        std::size_t length = 0; std::uint16_t sequence = 0;
        if (!jitter_.pop(payload, length, sequence)) {
            if (jitter_.ready() && jitter_.buffered() > 0) { jitter_.skipMissing(); block->statistics.packetsLost.fetch_add(1); }
            continue;
        }
        if (length != kPayloadBytes || !L24Codec::decode(std::span(payload).first(length), interleaved)) { block->statistics.malformedPackets.fetch_add(1); continue; }
        for (std::size_t ch = 0; ch < kChannels; ++ch) {
            for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) channel[frame] = interleaved[frame * kChannels + ch];
            if (block->networkToCoreAudio[ch].write(channel) != channel.size()) block->statistics.ringOverruns.fetch_add(1);
        }
    }
}

void LiveEngine::transmitLoop() {
    auto* block = sharedMemory_.get();
    ReconnectPolicy reconnect;
    std::array<float, kFramesPerPacket * kChannels> interleaved{};
    std::array<float, kFramesPerPacket> channel{};
    std::array<std::uint8_t, RTPCodec::kFixedHeaderBytes + kPayloadBytes> wire{};
    RTPPacket packet; packet.payloadType = config_.txPayloadType; packet.ssrc = 0x41455342U; packet.payload.resize(kPayloadBytes);
    while (running_) {
        UDPSocket socket;
        if (!socket.openTransmitter(config_.txAddress, config_.txPort, config_.interfaceAddress)) {
            block->txActive.store(false); block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); continue;
        }
        reconnect.connected(); block->txActive.store(true);
        auto next = std::chrono::steady_clock::now();
        while (running_) {
            next += 1ms; std::this_thread::sleep_until(next);
            if (std::chrono::steady_clock::now() - next > 10ms) next = std::chrono::steady_clock::now();
            bool underrun = false;
            for (std::size_t ch = 0; ch < kChannels; ++ch) {
                const auto count = block->coreAudioToNetwork[ch].read(channel);
                if (count < channel.size()) { std::fill(channel.begin() + static_cast<std::ptrdiff_t>(count), channel.end(), 0.0F); underrun = true; }
                for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) interleaved[frame * kChannels + ch] = channel[frame];
            }
            if (underrun && block->ioRunning.load()) block->statistics.outputUnderruns.fetch_add(1);
            L24Codec::encode(interleaved, packet.payload);
            packet.sequence = static_cast<std::uint16_t>(packet.sequence + 1U); packet.timestamp += kFramesPerPacket;
            std::size_t written = 0;
            if (!RTPCodec::encode(packet, wire, written) || socket.send(std::span(wire).first(written)) != static_cast<std::ptrdiff_t>(written)) break;
            block->statistics.txPackets.fetch_add(1);
        }
        block->txActive.store(false);
        if (running_) { block->statistics.reconnects.fetch_add(1); std::this_thread::sleep_for(reconnect.nextDelay()); }
    }
}

void LiveEngine::sapPublishLoop() {
    SessionDescription session;
    session.name = std::string(kMacSessionName); session.originAddress = config_.interfaceAddress;
    session.sourceAddress = config_.interfaceAddress; session.multicastAddress = config_.txAddress; session.port = config_.txPort;
    session.payloadType = config_.txPayloadType;
    SAPMessage announcement{false, 0, config_.interfaceAddress, "application/sdp", SDP::generate(session)};
    const auto bytes = SAP::encode(announcement);
    UDPSocket publisher;
    if (!publisher.openTransmitter(config_.sapAddress, config_.sapPort, config_.interfaceAddress)) return;
    while (running_) {
        publisher.send(bytes);
        for (int i = 0; i < 50 && running_; ++i) std::this_thread::sleep_for(100ms);
    }
    announcement.deletion = true;
    const auto deletion = SAP::encode(announcement);
    publisher.send(deletion);
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
            if (session->originAddress == config_.interfaceAddress && session->name == kMacSessionName) continue;
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
