// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/ChannelLayout.hpp"
#include "Core/IPv4Address.hpp"
#include "Core/JitterBuffer.hpp"
#include "Core/L24Codec.hpp"
#include "Core/PTP.hpp"
#include "Core/RTPPacket.hpp"
#include "Core/SAP.hpp"
#include "Core/SDP.hpp"
#include "Core/SPSCRingBuffer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

void testIPv4() {
    const auto multicast = lxtool::aes67::IPv4Address::parse("239.69.83.80");
    CHECK(multicast.has_value() && multicast->isMulticast() && multicast->toString() == "239.69.83.80");
    CHECK(!lxtool::aes67::IPv4Address::parse("239.69.83.999").has_value());
    CHECK(!lxtool::aes67::IPv4Address::parse("239.69.83").has_value());
}

void testProtocolRoundTrip() {
    using namespace lxtool::aes67;
    std::array<float, kFramesPerPacket * kChannels> input{};
    for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) input[frame * kChannels + channel] = static_cast<float>(channel + 1) / 10.0F;
    }
    RTPPacket packet;
    packet.sequence = 65'535;
    packet.timestamp = 0x12345678U;
    packet.ssrc = 0x41455342U;
    packet.payload.resize(kPayloadBytes);
    CHECK(L24Codec::encode(input, packet.payload));
    std::array<std::uint8_t, RTPCodec::kFixedHeaderBytes + kPayloadBytes> wire{};
    std::size_t written = 0;
    CHECK(RTPCodec::encode(packet, wire, written));
    RTPPacket decoded;
    CHECK(RTPCodec::decode(std::span(wire).first(written), decoded));
    std::array<float, kFramesPerPacket * kChannels> output{};
    CHECK(L24Codec::decode(decoded.payload, output));
    for (std::size_t channel = 0; channel < kChannels; ++channel) CHECK(std::abs(output[channel] - input[channel]) < 0.000001F);
}

void testStereoPlanetPayloadAndSDP() {
    using namespace lxtool::aes67;
    constexpr std::size_t channels = 2;
    std::array<float, kFramesPerPacket * channels> input{};
    for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) {
        input[frame * channels] = 0.25F;
        input[frame * channels + 1] = -0.5F;
    }
    std::array<std::uint8_t, payloadBytesForChannels(channels)> payload{};
    std::array<float, kFramesPerPacket * channels> output{};
    CHECK(L24Codec::encode(input, payload));
    CHECK(payload.size() == 288);
    CHECK(L24Codec::decode(payload, output));
    CHECK(std::abs(output[0] - 0.25F) < 0.000001F);
    CHECK(std::abs(output[1] + 0.5F) < 0.000001F);

    SessionDescription session;
    session.name = "AES-Bridge-planet22c";
    session.originAddress = "192.168.50.2";
    session.multicastAddress = "239.69.83.82";
    session.channels = channels;
    const auto parsed = SDP::parse(SDP::generate(session));
    CHECK(parsed.has_value() && parsed->channels == channels);
    CHECK(parsed.has_value() && SDP::validateLXToolProfile(*parsed).empty());
}

void testSDPAndSAP() {
    using namespace lxtool::aes67;
    SessionDescription session;
    session.name = "AES-Bridge-Test";
    session.originAddress = "192.168.50.2";
    session.sourceAddress = "192.168.50.2";
    session.multicastAddress = "239.69.83.80";
    const auto sdp = SDP::generate(session);
    const auto parsed = SDP::parse(sdp);
    CHECK(parsed.has_value() && SDP::validateLXToolProfile(*parsed).empty());
    SAPMessage message{false, 0, session.originAddress, "application/sdp", sdp};
    const auto bytes = SAP::encode(message);
    SAPMessage decoded;
    CHECK(SAP::decode(bytes, decoded));
    CHECK(decoded.originAddress == session.originAddress && decoded.sdp == sdp);
}

void testLockFreeContainers() {
    lxtool::aes67::SPSCRingBuffer<int, 8> ring;
    const std::array<int, 4> input{1, 2, 3, 4};
    std::array<int, 4> output{};
    CHECK(ring.write(input) == input.size());
    CHECK(ring.read(output) == output.size() && output == input);
    lxtool::aes67::JitterBuffer<16, 16> jitter(2);
    const std::array<std::uint8_t, 1> first{1}, second{2};
    CHECK(jitter.push(100, first) && jitter.push(101, second) && jitter.ready());
}

void test64ChannelOrder() {
    using namespace lxtool::aes67;
    constexpr std::size_t frames = 4;
    std::array<float, frames * kVirtualChannels> interleaved{};
    std::array<float, frames> channel{};
    for (std::size_t index = 0; index < kVirtualChannels; ++index) {
        channel.fill(static_cast<float>(index + 1));
        CHECK(interleaveChannel<float>(channel, index, kVirtualChannels, interleaved));
    }
    for (std::size_t index = 0; index < kVirtualChannels; ++index) {
        channel.fill(0.0F);
        CHECK(deinterleaveChannel<float>(interleaved, index, kVirtualChannels, channel));
        for (const auto sample : channel) CHECK(sample == static_cast<float>(index + 1));
    }
}

void testPTPCodecAndE2ECalculation() {
    using namespace lxtool::aes67;
    PTPPortIdentity identity{{0x02, 0x11, 0x22, 0xff, 0xfe, 0x33, 0x44, 0x55}, 1};
    constexpr std::int64_t origin = 1'700'000'000'123'456'789LL;
    auto bytes = PTPCodec::encodeDelayRequest(identity, 0x1234, 0, origin);
    PTPMessage message;
    CHECK(PTPCodec::decode(bytes, 0, message));
    CHECK(message.type == PTPMessageType::delayRequest && message.source == identity);
    CHECK(message.sequence == 0x1234 && message.timestampNanoseconds == origin);
    CHECK(!PTPCodec::decode(bytes, 1, message));
    bytes[0] = static_cast<std::uint8_t>(PTPMessageType::followUp);
    bytes[6] = 0x02;
    CHECK(PTPCodec::decode(bytes, 0, message) && message.twoStep && message.timestampNanoseconds == origin);
    bytes[40] = 0x3b; bytes[41] = 0x9a; bytes[42] = 0xca; bytes[43] = 0x00;
    CHECK(!PTPCodec::decode(bytes, 0, message));

    constexpr std::int64_t t1 = 1'000'000'000LL;
    constexpr std::int64_t t2 = t1 + 2'100'000LL;
    constexpr std::int64_t t3 = 2'000'000'000LL;
    constexpr std::int64_t t4 = t3 - 1'900'000LL;
    const auto measurement = PTPCodec::calculateE2E(t1, t2, t3, t4);
    CHECK(measurement.has_value());
    CHECK(measurement->offsetNanoseconds == 2'000'000LL);
    CHECK(measurement->meanPathDelayNanoseconds == 100'000LL);
    CHECK(!PTPCodec::calculateE2E(t1, t1, t3, t3 - 2));

    const auto announce = PTPCodec::encodeAnnounce(identity, 7, 0);
    CHECK(PTPCodec::decode(announce, 0, message));
    CHECK(message.type == PTPMessageType::announce);
    CHECK(message.grandmasterPriority1 == 128 && message.grandmasterClockClass == 248);
    CHECK(message.grandmasterClockAccuracy == 0xfe && message.grandmasterPriority2 == 128);
    CHECK(message.grandmasterIdentity == identity.clock && message.stepsRemoved == 0);
}
}

int main() {
    testIPv4();
    testProtocolRoundTrip();
    testStereoPlanetPayloadAndSDP();
    testSDPAndSAP();
    testLockFreeContainers();
    test64ChannelOrder();
    testPTPCodecAndE2ECalculation();
    if (failures == 0) std::cout << "AES Bridge portable protocol tests passed\n";
    return failures == 0 ? 0 : 1;
}
