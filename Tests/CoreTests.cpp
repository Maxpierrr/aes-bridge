// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/JitterBuffer.hpp"
#include "Core/L24Codec.hpp"
#include "Core/RTPPacket.hpp"
#include "Core/ReconnectPolicy.hpp"
#include "Core/SAP.hpp"
#include "Core/SDP.hpp"
#include "Core/SessionDirectory.hpp"
#include "Core/SharedAudioMemory.hpp"
#include "Core/SPSCRingBuffer.hpp"
#include "Core/UDPSocket.hpp"
#include "Engine/LiveEngine.hpp"
#include "Engine/PTPClient.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <thread>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

void testL24() {
    const std::array<float, 9> source{-1.0F, -0.75F, -0.1F, 0.0F, 0.1F, 0.5F, 0.999F, 1.0F, 1.5F};
    std::array<std::uint8_t, source.size() * 3> bytes{};
    std::array<float, source.size()> decoded{};
    CHECK(lxtool::aes67::L24Codec::encode(source, bytes));
    CHECK(bytes[0] == 0x80 && bytes[1] == 0x00 && bytes[2] == 0x00);
    CHECK(bytes[21] == 0x7f && bytes[22] == 0xff && bytes[23] == 0xff);
    CHECK(lxtool::aes67::L24Codec::decode(bytes, decoded));
    for (std::size_t i = 0; i < source.size(); ++i) {
        CHECK(std::abs(decoded[i] - std::clamp(source[i], -1.0F, 1.0F)) < 0.000001F);
    }
}

void testRTP() {
    lxtool::aes67::RTPPacket packet;
    packet.sequence = 65'535; packet.timestamp = 0x12345678; packet.ssrc = 0xabcdef01;
    packet.payload = {0, 1, 2, 3, 4};
    std::array<std::uint8_t, 64> wire{};
    std::size_t written = 0;
    CHECK(lxtool::aes67::RTPCodec::encode(packet, wire, written));
    CHECK(written == 17 && wire[0] == 0x80 && wire[1] == 96);
    lxtool::aes67::RTPPacket decoded;
    CHECK(lxtool::aes67::RTPCodec::decode(std::span(wire).first(written), decoded));
    CHECK(decoded.sequence == packet.sequence && decoded.timestamp == packet.timestamp && decoded.ssrc == packet.ssrc);
    CHECK(decoded.payload == packet.payload);
    wire[0] = 0x40;
    CHECK(!lxtool::aes67::RTPCodec::decode(std::span(wire).first(written), decoded));
}

void testSDP() {
    lxtool::aes67::SessionDescription session;
    session.name = std::string(lxtool::aes67::kPiSessionName);
    session.originAddress = "192.168.50.2";
    session.sourceAddress = "192.168.50.2";
    session.multicastAddress = std::string(lxtool::aes67::kDefaultMulticast);
    const auto text = lxtool::aes67::SDP::generate(session);
    std::string error;
    const auto parsed = lxtool::aes67::SDP::parse(text, &error);
    CHECK(parsed.has_value());
    CHECK(parsed->name == lxtool::aes67::kPiSessionName);
    CHECK(parsed->multicastAddress == lxtool::aes67::kDefaultMulticast);
    CHECK(parsed->sourceAddress == "192.168.50.2");
    CHECK(lxtool::aes67::SDP::validateLXToolProfile(*parsed).empty());
    auto wrong = *parsed;
    wrong.channels = 7;
    CHECK(!lxtool::aes67::SDP::validateLXToolProfile(wrong).empty());
    CHECK(!lxtool::aes67::SDP::parse("v=0\r\ns=x\r\nc=IN IP4 192.168.1.1\r\n").has_value());
}

void testSAP() {
    lxtool::aes67::SessionDescription session;
    session.name = std::string(lxtool::aes67::kPiSessionName);
    session.originAddress = "10.10.10.2";
    session.multicastAddress = std::string(lxtool::aes67::kDefaultMulticast);
    lxtool::aes67::SAPMessage original{false, 0, "10.10.10.2", "application/sdp", lxtool::aes67::SDP::generate(session)};
    const auto bytes = lxtool::aes67::SAP::encode(original);
    CHECK(!bytes.empty());
    lxtool::aes67::SAPMessage decoded;
    CHECK(lxtool::aes67::SAP::decode(bytes, decoded));
    CHECK(decoded.originAddress == original.originAddress && decoded.sdp == original.sdp);
    CHECK(decoded.messageHash == lxtool::aes67::SAP::hash(original.sdp));
}

void testChannelOrder() {
    std::array<float, lxtool::aes67::kFramesPerPacket * lxtool::aes67::kChannels> interleaved{};
    for (std::size_t frame = 0; frame < lxtool::aes67::kFramesPerPacket; ++frame) {
        for (std::size_t channel = 0; channel < lxtool::aes67::kChannels; ++channel) {
            interleaved[frame * lxtool::aes67::kChannels + channel] = static_cast<float>(channel + 1) / 10.0F;
        }
    }
    std::array<std::uint8_t, lxtool::aes67::kPayloadBytes> payload{};
    std::array<float, interleaved.size()> decoded{};
    CHECK(lxtool::aes67::L24Codec::encode(interleaved, payload));
    CHECK(lxtool::aes67::L24Codec::decode(payload, decoded));
    for (std::size_t frame = 0; frame < lxtool::aes67::kFramesPerPacket; ++frame) {
        for (std::size_t channel = 0; channel < lxtool::aes67::kChannels; ++channel) {
            CHECK(std::abs(decoded[frame * lxtool::aes67::kChannels + channel] - static_cast<float>(channel + 1) / 10.0F) < 0.000001F);
        }
    }
}

void testJitterAndLoss() {
    lxtool::aes67::JitterBuffer<16, 16> jitter(3);
    const std::array<std::uint8_t, 2> a{100, 1}, b{101, 2}, c{102, 3}, d{104, 4};
    CHECK(jitter.push(100, a)); CHECK(jitter.push(102, c)); CHECK(!jitter.ready()); CHECK(jitter.push(101, b)); CHECK(jitter.ready());
    std::array<std::uint8_t, 16> out{}; std::size_t length = 0; std::uint16_t sequence = 0;
    CHECK(jitter.pop(out, length, sequence) && sequence == 100 && out[0] == 100);
    CHECK(jitter.pop(out, length, sequence) && sequence == 101 && out[0] == 101);
    CHECK(jitter.pop(out, length, sequence) && sequence == 102 && out[0] == 102);
    CHECK(jitter.push(104, d));
    CHECK(!jitter.pop(out, length, sequence));
    jitter.skipMissing();
    CHECK(jitter.lost() == 1);
    CHECK(jitter.pop(out, length, sequence) && sequence == 104 && out[0] == 104);
}

void testRingAndReconnect() {
    lxtool::aes67::SPSCRingBuffer<int, 8> ring;
    const std::array<int, 6> first{1,2,3,4,5,6};
    CHECK(ring.write(first) == 6);
    std::array<int, 4> read{};
    CHECK(ring.read(read) == 4 && read[0] == 1 && read[3] == 4);
    const std::array<int, 5> second{7,8,9,10,11};
    CHECK(ring.write(second) == 5);
    std::array<int, 7> all{};
    CHECK(ring.read(all) == 7);
    CHECK((all == std::array<int, 7>{5,6,7,8,9,10,11}));

    lxtool::aes67::ReconnectPolicy reconnect;
    CHECK(reconnect.nextDelay().count() == 250);
    CHECK(reconnect.nextDelay().count() == 500);
    for (int i = 0; i < 10; ++i) (void)reconnect.nextDelay();
    CHECK(reconnect.nextDelay().count() == 8000);
    reconnect.connected();
    CHECK(reconnect.nextDelay().count() == 250);
}

void testUDPLoopback() {
    lxtool::aes67::UDPSocket receiver;
    CHECK(receiver.openReceiver("127.0.0.1", 0, "127.0.0.1"));
    CHECK(receiver.localPort() != 0);
    lxtool::aes67::UDPSocket sender;
    CHECK(sender.openTransmitter("127.0.0.1", receiver.localPort(), "127.0.0.1"));
    const std::array<std::uint8_t, 6> sent{1,2,3,4,5,6};
    CHECK(sender.send(sent) == static_cast<std::ptrdiff_t>(sent.size()));
    std::array<std::uint8_t, 16> received{};
    const auto count = receiver.receive(received, std::chrono::milliseconds(500));
    CHECK(count == static_cast<std::ptrdiff_t>(sent.size()));
    CHECK(std::equal(sent.begin(), sent.end(), received.begin()));
}

void testSharedAudioMemory() {
    CHECK(lxtool::aes67::SharedAudioMemory::remove());
    CHECK(!lxtool::aes67::SharedAudioMemory::ownerActive());
    lxtool::aes67::SharedAudioMemory owner;
    lxtool::aes67::SharedAudioMemory peer;
    const bool ownerOpened = owner.open(true);
    if (!ownerOpened) std::cerr << "shared-memory owner errno=" << owner.lastError() << '\n';
    CHECK(ownerOpened);
    CHECK(lxtool::aes67::SharedAudioMemory::ownerActive());
    const bool peerOpened = peer.open(false);
    if (!peerOpened) std::cerr << "shared-memory peer errno=" << peer.lastError() << '\n';
    CHECK(peerOpened);
    if (!owner.get() || !peer.get()) return;

    CHECK(owner.get()->channels == lxtool::aes67::kVirtualChannels);
    CHECK(owner.get()->channelsPerStream == lxtool::aes67::kAES67ChannelsPerStream);
    CHECK(owner.get()->streamBankCount == lxtool::aes67::kStreamBankCount);

    const std::array<float, 4> sent{0.1F, 0.2F, 0.3F, 0.4F};
    std::array<float, 4> received{};
    CHECK(owner.get()->coreAudioToNetwork[63].write(sent) == sent.size());
    CHECK(peer.get()->coreAudioToNetwork[63].read(received) == received.size());
    CHECK(received == sent);
    const std::array<float, 4> queuedAcrossRestart{0.5F, 0.6F, 0.7F, 0.8F};
    std::array<float, 4> receivedAcrossRestart{};
    peer.get()->ioRunning.store(true, std::memory_order_release);
    CHECK(owner.get()->coreAudioToNetwork[62].write(queuedAcrossRestart) == queuedAcrossRestart.size());
    owner.close();
    lxtool::aes67::SharedAudioMemory restartedOwner;
    CHECK(restartedOwner.open(true));
    CHECK(restartedOwner.get() && restartedOwner.get()->ioRunning.load(std::memory_order_acquire));
    CHECK(peer.get()->coreAudioToNetwork[62].read(receivedAcrossRestart) == receivedAcrossRestart.size());
    CHECK(receivedAcrossRestart == queuedAcrossRestart);
    lxtool::aes67::SharedAudioMemory duplicateOwner;
    CHECK(!duplicateOwner.open(true));
    restartedOwner.close();
    CHECK(!lxtool::aes67::SharedAudioMemory::ownerActive());
    peer.close();
    CHECK(lxtool::aes67::SharedAudioMemory::remove());
}

void testLiveEngineLoopbackAndChannelOrder() {
    using namespace std::chrono_literals;
    CHECK(lxtool::aes67::SharedAudioMemory::remove());
    lxtool::aes67::LiveEngineConfig config;
    config.interfaceAddress = "127.0.0.1";
    config.rxAddress = "127.0.0.1";
    config.txAddress = "127.0.0.1";
    config.rxPort = 54678;
    config.txPort = 54678;
    config.streamCount = lxtool::aes67::kStreamBankCount;
    config.portStride = 1;
    config.jitterPackets = 3;
    config.enableSAPPublication = false;
    config.enableSAPDiscovery = false;
    config.enablePTP = false;

    lxtool::aes67::LiveEngine engine(config);
    CHECK(engine.start());
    lxtool::aes67::LiveEngine duplicate(config);
    CHECK(!duplicate.start());
    auto* block = engine.sharedBlock();
    if (!block) return;
    block->ioRunning.store(true);

    constexpr std::size_t injectedFrames = lxtool::aes67::kFramesPerPacket * 32;
    std::array<float, injectedFrames> channel{};
    for (std::size_t ch = 0; ch < lxtool::aes67::kVirtualChannels; ++ch) {
        channel.fill(static_cast<float>(ch + 1) / 100.0F);
        CHECK(block->coreAudioToNetwork[ch].write(channel) == channel.size());
    }

    const auto expectedPackets = config.streamCount * 32;
    CHECK(waitUntil([&] {
        if (block->statistics.txPackets.load() < expectedPackets
            || block->statistics.rxPackets.load() < expectedPackets) return false;
        return std::all_of(block->networkToCoreAudio.begin(), block->networkToCoreAudio.end(),
            [](const auto& ring) { return ring.available() >= injectedFrames; });
    }, 1s));

    std::array<float, 7000> returned{};
    for (std::size_t ch = 0; ch < lxtool::aes67::kVirtualChannels; ++ch) {
        const auto count = block->networkToCoreAudio[ch].read(returned);
        CHECK(count >= injectedFrames);
        const float expected = static_cast<float>(ch + 1) / 100.0F;
        std::size_t matchingRun = 0;
        bool found = false;
        for (std::size_t frame = 0; frame < count; ++frame) {
            if (std::abs(returned[frame] - expected) < 0.000001F) {
                if (++matchingRun >= lxtool::aes67::kFramesPerPacket) { found = true; break; }
            } else {
                matchingRun = 0;
            }
        }
        CHECK(found);
    }
    block->ioRunning.store(false);
    engine.stop();
    CHECK(lxtool::aes67::SharedAudioMemory::remove());
}

void testStereoPlanetLoopbackAndCoreAudioMapping() {
    using namespace std::chrono_literals;
    using namespace lxtool::aes67;
    CHECK(SharedAudioMemory::remove());
    LiveEngineConfig config;
    config.interfaceAddress = "127.0.0.1";
    config.rxAddress = "127.0.0.1";
    config.txAddress = "127.0.0.1";
    config.rxPort = 54679;
    config.txPort = 54679;
    config.channelsPerStream = 2;
    config.coreAudioStartChannel = 9;
    config.jitterPackets = 3;
    config.enableSAPPublication = false;
    config.enableSAPDiscovery = false;
    config.enablePTP = false;

    LiveEngine engine(config);
    CHECK(engine.start());
    auto* block = engine.sharedBlock();
    if (!block) return;
    block->ioRunning.store(true);
    constexpr std::size_t injectedFrames = kFramesPerPacket * 24;
    std::array<float, injectedFrames> left{};
    std::array<float, injectedFrames> right{};
    left.fill(0.22F);
    right.fill(-0.44F);
    CHECK(block->coreAudioToNetwork[8].write(left) == left.size());
    CHECK(block->coreAudioToNetwork[9].write(right) == right.size());
    CHECK(waitUntil([&] {
        return block->statistics.txPackets.load() >= 24
            && block->statistics.rxPackets.load() >= 24
            && block->networkToCoreAudio[8].available() >= injectedFrames
            && block->networkToCoreAudio[9].available() >= injectedFrames;
    }, 1s));

    std::array<float, 4096> returnedLeft{};
    std::array<float, 4096> returnedRight{};
    const auto leftCount = block->networkToCoreAudio[8].read(returnedLeft);
    const auto rightCount = block->networkToCoreAudio[9].read(returnedRight);
    CHECK(leftCount >= injectedFrames && rightCount >= injectedFrames);
    CHECK(std::find_if(returnedLeft.begin(), returnedLeft.begin() + static_cast<std::ptrdiff_t>(leftCount),
        [](float value) { return std::abs(value - 0.22F) < 0.000001F; }) != returnedLeft.begin() + static_cast<std::ptrdiff_t>(leftCount));
    CHECK(std::find_if(returnedRight.begin(), returnedRight.begin() + static_cast<std::ptrdiff_t>(rightCount),
        [](float value) { return std::abs(value + 0.44F) < 0.000001F; }) != returnedRight.begin() + static_cast<std::ptrdiff_t>(rightCount));
    CHECK(block->networkToCoreAudio[0].available() == 0);
    block->ioRunning.store(false);
    engine.stop();
    CHECK(SharedAudioMemory::remove());
}

void testLiveSAPDiscoveryAndDeletion() {
    using namespace std::chrono_literals;
    CHECK(lxtool::aes67::SharedAudioMemory::remove());
    lxtool::aes67::LiveEngineConfig config;
    config.interfaceAddress = "127.0.0.1";
    config.rxAddress = "127.0.0.1";
    config.txAddress = "127.0.0.1";
    config.rxPort = 54683;
    config.txPort = 54683;
    config.sapAddress = "127.0.0.1";
    config.sapPort = 54684;
    config.enableSAPPublication = false;
    config.enableSAPDiscovery = true;
    config.enablePTP = false;

    lxtool::aes67::LiveEngine engine(config);
    CHECK(engine.start());
    auto* block = engine.sharedBlock();
    if (!block) return;

    lxtool::aes67::SessionDescription session;
    session.name = "Test-Pi-Inputs-1-8";
    session.originAddress = "10.20.30.40";
    session.sourceAddress = "10.20.30.40";
    session.multicastAddress = "239.69.83.80";
    session.port = 5004;
    const auto sdp = lxtool::aes67::SDP::generate(session);
    const auto hash = lxtool::aes67::SAP::hash(sdp);
    lxtool::aes67::SAPMessage announcement{false, hash, session.originAddress, "application/sdp", sdp};
    lxtool::aes67::UDPSocket publisher;
    CHECK(publisher.openTransmitter(config.sapAddress, config.sapPort, "127.0.0.1"));
    const auto announcementBytes = lxtool::aes67::SAP::encode(announcement);
    bool discovered = false;
    for (int attempt = 0; attempt < 20 && !discovered; ++attempt) {
        CHECK(publisher.send(announcementBytes) == static_cast<std::ptrdiff_t>(announcementBytes.size()));
        std::this_thread::sleep_for(25ms);
        const auto sessions = lxtool::aes67::SessionDirectory::snapshots(*block);
        discovered = sessions.size() == 1 && sessions.front().name == session.name
            && sessions.front().multicastAddress == session.multicastAddress
            && sessions.front().port == session.port && sessions.front().channels == 8;
    }
    CHECK(discovered);

    announcement.deletion = true;
    const auto deletionBytes = lxtool::aes67::SAP::encode(announcement);
    CHECK(publisher.send(deletionBytes) == static_cast<std::ptrdiff_t>(deletionBytes.size()));
    bool deleted = false;
    for (int attempt = 0; attempt < 20 && !deleted; ++attempt) {
        std::this_thread::sleep_for(25ms);
        deleted = lxtool::aes67::SessionDirectory::snapshots(*block).empty();
    }
    CHECK(deleted);
    engine.stop();
    CHECK(lxtool::aes67::SharedAudioMemory::remove());
}

void testRTPSourceRestartRecovery() {
    using namespace std::chrono_literals;
    using namespace lxtool::aes67;
    CHECK(SharedAudioMemory::remove());
    LiveEngineConfig config;
    config.interfaceAddress = "127.0.0.1";
    config.rxAddress = "127.0.0.1";
    config.txAddress = "127.0.0.1";
    config.rxPort = 54685;
    config.txPort = 54686;
    config.jitterPackets = 3;
    config.enableSAPPublication = false;
    config.enableSAPDiscovery = false;
    config.enablePTP = false;
    LiveEngine engine(config);
    CHECK(engine.start());
    auto* block = engine.sharedBlock();
    if (!block) return;

    UDPSocket sender;
    CHECK(sender.openTransmitter("127.0.0.1", config.rxPort, "127.0.0.1"));
    auto sendPackets = [&](std::uint32_t ssrc, std::uint16_t firstSequence, float value) {
        std::array<float, kFramesPerPacket * kChannels> samples{};
        samples.fill(value);
        RTPPacket packet;
        packet.ssrc = ssrc;
        packet.payload.resize(kPayloadBytes);
        CHECK(L24Codec::encode(samples, packet.payload));
        std::array<std::uint8_t, RTPCodec::kFixedHeaderBytes + kPayloadBytes> wire{};
        for (std::uint16_t index = 0; index < 12; ++index) {
            packet.sequence = static_cast<std::uint16_t>(firstSequence + index);
            packet.timestamp = static_cast<std::uint32_t>(index) * kFramesPerPacket;
            std::size_t written = 0;
            CHECK(RTPCodec::encode(packet, wire, written));
            CHECK(sender.send(std::span(wire).first(written)) == static_cast<std::ptrdiff_t>(written));
            std::this_thread::sleep_for(2ms);
        }
    };
    std::this_thread::sleep_for(30ms);
    sendPackets(0x11111111U, 100, 0.1F);
    sendPackets(0x22222222U, 0, 0.7F);
    std::this_thread::sleep_for(80ms);
    CHECK(block->statistics.reconnects.load() >= 1);
    std::array<float, 4096> returned{};
    const auto count = block->networkToCoreAudio[0].read(returned);
    bool foundRestartedSource = false;
    for (std::size_t index = 0; index < count; ++index) {
        if (std::abs(returned[index] - 0.7F) < 0.000001F) { foundRestartedSource = true; break; }
    }
    CHECK(foundRestartedSource);
    engine.stop();
    CHECK(SharedAudioMemory::remove());
}

void testEightBankSAPPublication() {
    using namespace lxtool::aes67;
    CHECK(SharedAudioMemory::remove());
    UDPSocket listener;
    CHECK(listener.openReceiver("127.0.0.1", 54690, "127.0.0.1"));
    LiveEngineConfig config;
    config.interfaceAddress = "127.0.0.1";
    config.rxAddress = "127.0.0.1";
    config.txAddress = "239.69.83.96";
    config.rxPort = 54700;
    config.txPort = 54720;
    config.streamCount = kStreamBankCount;
    config.portStride = 1;
    config.sapAddress = "127.0.0.1";
    config.sapPort = 54690;
    config.enableSAPPublication = true;
    config.enableSAPDiscovery = false;
    config.enablePTP = false;
    LiveEngine engine(config);
    CHECK(engine.start());
    std::array<bool, kStreamBankCount> seen{};
    std::array<std::uint8_t, 4096> wire{};
    for (int attempt = 0; attempt < 24; ++attempt) {
        const auto count = listener.receive(wire, std::chrono::milliseconds(100));
        if (count <= 0) continue;
        SAPMessage message;
        if (!SAP::decode(std::span(wire).first(static_cast<std::size_t>(count)), message) || message.deletion) continue;
        const auto session = SDP::parse(message.sdp);
        if (!session) continue;
        for (std::size_t bank = 0; bank < kStreamBankCount; ++bank) {
            const auto first = bank * kAES67ChannelsPerStream + 1U;
            const auto expectedName = "AES-Bridge-Outputs-" + std::to_string(first) + '-' + std::to_string(first + 7U);
            if (session->name == expectedName) {
                seen[bank] = session->multicastAddress == "239.69.83." + std::to_string(96U + bank)
                    && session->port == static_cast<std::uint16_t>(config.txPort + bank);
            }
        }
        if (std::all_of(seen.begin(), seen.end(), [](bool value) { return value; })) break;
    }
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
    engine.stop();
    CHECK(SharedAudioMemory::remove());
}

void testPTPE2ELockAndTimeout() {
    using namespace std::chrono_literals;
    using namespace lxtool::aes67;
    CHECK(SharedAudioMemory::remove());
    SharedAudioMemory memory;
    CHECK(memory.open(true));
    if (!memory.get()) return;

    PTPPortIdentity clientIdentity{{0x02, 1, 2, 3, 4, 5, 6, 7}, 1};
    PTPPortIdentity masterIdentity{{0x02, 9, 8, 7, 6, 5, 4, 3}, 1};
    PTPClientConfig config;
    config.multicastAddress = "127.0.0.1";
    config.interfaceAddress = "127.0.0.1";
    config.eventReceivePort = 54800;
    config.eventTransmitPort = 54801;
    config.generalReceivePort = 54802;
    config.localIdentity = clientIdentity;

    UDPSocket syncSender;
    UDPSocket generalSender;
    UDPSocket delayRequestListener;
    CHECK(syncSender.openTransmitter("127.0.0.1", config.eventReceivePort, "127.0.0.1"));
    CHECK(generalSender.openTransmitter("127.0.0.1", config.generalReceivePort, "127.0.0.1"));
    CHECK(delayRequestListener.openReceiver("127.0.0.1", config.eventTransmitPort, "127.0.0.1"));

    {
    PTPClient client(config, *memory.get());
    CHECK(client.start());
    auto ptpV1Traffic = PTPCodec::encodeAnnounce(masterIdentity, 0, 0);
    ptpV1Traffic[1] = 0x01;
    CHECK(generalSender.send(ptpV1Traffic) == static_cast<std::ptrdiff_t>(ptpV1Traffic.size()));
    std::this_thread::sleep_for(30ms);
    CHECK(memory.get()->statistics.ptpErrors.load(std::memory_order_relaxed) == 0);
    constexpr std::int64_t simulatedOffset = 2'000'000LL;
    auto systemNow = [] { return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count(); };
    std::array<std::uint8_t, 256> wire{};
    unsigned responses = 0;
    for (std::uint16_t cycle = 1; cycle <= 40 && responses < 6; ++cycle) {
        const auto announce = PTPCodec::encodeAnnounce(masterIdentity, cycle, 0);
        const auto t1 = systemNow() - simulatedOffset;
        const auto sync = PTPCodec::encodeTimestampMessage(PTPMessageType::sync, masterIdentity, cycle, 0, 0, true);
        const auto followUp = PTPCodec::encodeTimestampMessage(PTPMessageType::followUp, masterIdentity, cycle, 0, t1);
        CHECK(generalSender.send(announce) == static_cast<std::ptrdiff_t>(announce.size()));
        CHECK(syncSender.send(sync) == static_cast<std::ptrdiff_t>(sync.size()));
        CHECK(generalSender.send(followUp) == static_cast<std::ptrdiff_t>(followUp.size()));
        const auto count = delayRequestListener.receive(wire, 120ms);
        if (count <= 0) continue;
        PTPMessage request;
        CHECK(PTPCodec::decode(std::span(wire).first(static_cast<std::size_t>(count)), 0, request));
        const auto response = PTPCodec::encodeDelayResponse(masterIdentity, clientIdentity, request.sequence, 0,
            systemNow() - simulatedOffset);
        CHECK(generalSender.send(response) == static_cast<std::ptrdiff_t>(response.size()));
        ++responses;
        std::this_thread::sleep_for(40ms);
    }
    CHECK(responses >= 6);
    bool locked = false;
    for (int attempt = 0; attempt < 20 && !locked; ++attempt) {
        locked = memory.get()->ptpLocked.load(std::memory_order_acquire);
        std::this_thread::sleep_for(25ms);
    }
    CHECK(locked);
    CHECK(std::llabs(memory.get()->statistics.ptpOffsetNanoseconds.load() - simulatedOffset) < 3'000'000LL);
    CHECK(memory.get()->statistics.ptpMeanPathDelayNanoseconds.load() >= 0);
    CHECK(memory.get()->statistics.ptpMessages.load() >= 19);
    client.stop();
    CHECK(!memory.get()->ptpLocked.load());
    CHECK(memory.get()->statistics.ptpOffsetNanoseconds.load() == 0);
    CHECK(memory.get()->statistics.ptpMeanPathDelayNanoseconds.load() == 0);
    }
    memory.close();
    CHECK(SharedAudioMemory::remove());
}

void testBankTimestampAlignment() {
    using namespace lxtool::aes67;
    CHECK(SharedAudioMemory::remove());
    UDPSocket firstListener;
    UDPSocket secondListener;
    CHECK(firstListener.openReceiver("127.0.0.1", 54820, "127.0.0.1"));
    CHECK(secondListener.openReceiver("127.0.0.1", 54821, "127.0.0.1"));
    LiveEngineConfig config;
    config.interfaceAddress = "127.0.0.1";
    config.rxAddress = "127.0.0.1";
    config.txAddress = "127.0.0.1";
    config.rxPort = 54830;
    config.txPort = 54820;
    config.streamCount = 2;
    config.portStride = 1;
    config.enableSAPPublication = false;
    config.enableSAPDiscovery = false;
    config.enablePTP = false;
    LiveEngine engine(config);
    CHECK(engine.start());
    std::vector<RTPPacket> firstPackets;
    std::vector<RTPPacket> secondPackets;
    for (std::size_t attempt = 0; attempt < 32 && (firstPackets.size() < 8 || secondPackets.size() < 8); ++attempt) {
        for (auto [listener, packets] : {
                 std::pair{&firstListener, &firstPackets}, std::pair{&secondListener, &secondPackets}}) {
            std::array<std::uint8_t, 1400> wire{};
            const auto count = listener->receive(wire, std::chrono::milliseconds(100));
            RTPPacket packet;
            if (count > 0 && RTPCodec::decode(
                    std::span(wire).first(static_cast<std::size_t>(count)), packet)) {
                packets->push_back(std::move(packet));
            }
        }
    }
    CHECK(!firstPackets.empty());
    CHECK(!secondPackets.empty());
    if (!firstPackets.empty() && !secondPackets.empty()) {
        const auto phase = firstPackets.front().timestamp % kFramesPerPacket;
        const auto hasSharedPhase = [phase](const RTPPacket& packet) {
            return packet.timestamp % kFramesPerPacket == phase;
        };
        CHECK(std::all_of(firstPackets.begin(), firstPackets.end(), hasSharedPhase));
        CHECK(std::all_of(secondPackets.begin(), secondPackets.end(), hasSharedPhase));
        CHECK(firstPackets.front().ssrc != secondPackets.front().ssrc);
    }
    engine.stop();
    CHECK(SharedAudioMemory::remove());
}
}

int main() {
    testL24(); testRTP(); testSDP(); testSAP(); testChannelOrder(); testJitterAndLoss(); testRingAndReconnect(); testUDPLoopback();
    testSharedAudioMemory(); testLiveEngineLoopbackAndChannelOrder(); testStereoPlanetLoopbackAndCoreAudioMapping();
    testLiveSAPDiscoveryAndDeletion(); testRTPSourceRestartRecovery();
    testEightBankSAPPublication();
    testPTPE2ELockAndTimeout();
    testBankTimestampAlignment();
    if (failures == 0) std::cout << "All AES Bridge core and live-loopback tests passed\n";
    return failures == 0 ? 0 : 1;
}
