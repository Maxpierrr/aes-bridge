// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/ChannelLayout.hpp"
#include "Core/IPv4Address.hpp"
#include "Core/JitterBuffer.hpp"
#include "Core/L24Codec.hpp"
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
}

int main() {
    testIPv4();
    testProtocolRoundTrip();
    testSDPAndSAP();
    testLockFreeContainers();
    test64ChannelOrder();
    if (failures == 0) std::cout << "AES Bridge portable protocol tests passed\n";
    return failures == 0 ? 0 : 1;
}
