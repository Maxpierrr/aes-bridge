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

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

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
    lxtool::aes67::SharedAudioMemory owner;
    lxtool::aes67::SharedAudioMemory peer;
    const bool ownerOpened = owner.open(true);
    if (!ownerOpened) std::cerr << "shared-memory owner errno=" << owner.lastError() << '\n';
    CHECK(ownerOpened);
    const bool peerOpened = peer.open(false);
    if (!peerOpened) std::cerr << "shared-memory peer errno=" << peer.lastError() << '\n';
    CHECK(peerOpened);
    if (!owner.get() || !peer.get()) return;

    const std::array<float, 4> sent{0.1F, 0.2F, 0.3F, 0.4F};
    std::array<float, 4> received{};
    CHECK(owner.get()->coreAudioToNetwork[3].write(sent) == sent.size());
    CHECK(peer.get()->coreAudioToNetwork[3].read(received) == received.size());
    CHECK(received == sent);
    owner.close();
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
    config.jitterPackets = 3;
    config.enableSAPPublication = false;
    config.enableSAPDiscovery = false;

    lxtool::aes67::LiveEngine engine(config);
    CHECK(engine.start());
    lxtool::aes67::LiveEngine duplicate(config);
    CHECK(!duplicate.start());
    auto* block = engine.sharedBlock();
    if (!block) return;
    block->ioRunning.store(true);

    constexpr std::size_t injectedFrames = lxtool::aes67::kFramesPerPacket * 32;
    std::array<float, injectedFrames> channel{};
    for (std::size_t ch = 0; ch < lxtool::aes67::kChannels; ++ch) {
        channel.fill(static_cast<float>(ch + 1) / 10.0F);
        CHECK(block->coreAudioToNetwork[ch].write(channel) == channel.size());
    }

    std::this_thread::sleep_for(120ms);
    CHECK(block->statistics.txPackets.load() >= 80);
    CHECK(block->statistics.rxPackets.load() >= 80);

    std::array<float, 7000> returned{};
    for (std::size_t ch = 0; ch < lxtool::aes67::kChannels; ++ch) {
        const auto count = block->networkToCoreAudio[ch].read(returned);
        CHECK(count > injectedFrames);
        const float expected = static_cast<float>(ch + 1) / 10.0F;
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
}

int main() {
    testL24(); testRTP(); testSDP(); testSAP(); testChannelOrder(); testJitterAndLoss(); testRingAndReconnect(); testUDPLoopback();
    testSharedAudioMemory(); testLiveEngineLoopbackAndChannelOrder(); testLiveSAPDiscoveryAndDeletion(); testRTPSourceRestartRecovery();
    if (failures == 0) std::cout << "All AES Bridge core and live-loopback tests passed\n";
    return failures == 0 ? 0 : 1;
}
