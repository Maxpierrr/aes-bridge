// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/L24Codec.hpp"
#include "Core/RTPPacket.hpp"
#include "Core/UDPSocket.hpp"
#include "Platform/Windows/WindowsSharedAudioMemory.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
bool selfTest() {
    using namespace lxtool::aes67;
    UDPSocket receiver;
    if (!receiver.openReceiver("127.0.0.1", 0, "127.0.0.1")) return false;
    UDPSocket sender;
    if (!sender.openTransmitter("127.0.0.1", receiver.localPort(), "127.0.0.1")) return false;

    std::array<float, kFramesPerPacket * kChannels> samples{};
    for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            samples[frame * kChannels + channel] = static_cast<float>(channel + 1) / 10.0F;
        }
    }
    RTPPacket packet;
    packet.sequence = 42;
    packet.timestamp = 48;
    packet.ssrc = 0x41455342U;
    packet.payload.resize(kPayloadBytes);
    if (!L24Codec::encode(samples, packet.payload)) return false;
    std::array<std::uint8_t, RTPCodec::kFixedHeaderBytes + kPayloadBytes> wire{};
    std::size_t written = 0;
    if (!RTPCodec::encode(packet, wire, written) || sender.send(std::span(wire).first(written)) < 0) return false;
    const auto received = receiver.receive(wire, std::chrono::milliseconds(500));
    if (received != static_cast<std::ptrdiff_t>(written)) return false;
    RTPPacket decoded;
    if (!RTPCodec::decode(std::span(wire).first(written), decoded)) return false;
    std::array<float, kFramesPerPacket * kChannels> output{};
    if (!L24Codec::decode(decoded.payload, output)) return false;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (std::abs(output[channel] - static_cast<float>(channel + 1) / 10.0F) > 0.000001F) return false;
    }
    WindowsSharedAudioMemory owner;
    WindowsSharedAudioMemory peer;
    if (!owner.open(true) || !peer.open(false) || !owner.get() || !peer.get()) return false;
    if (owner.get()->channels != kVirtualChannels || owner.get()->channelsPerStream != kAES67ChannelsPerStream
        || owner.get()->streamBankCount != kStreamBankCount) return false;
    const std::array<float, 4> sharedInput{0.1F, 0.2F, 0.3F, 0.4F};
    std::array<float, 4> sharedOutput{};
    if (owner.get()->coreAudioToNetwork[63].write(sharedInput) != sharedInput.size()
        || peer.get()->coreAudioToNetwork[63].read(sharedOutput) != sharedOutput.size()
        || sharedInput != sharedOutput) return false;
    return true;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        if (!selfTest()) {
            std::cerr << "AES Bridge Windows backend self-test failed\n";
            return 1;
        }
        std::cout << "AES Bridge Windows protocol/Winsock self-test passed\n";
        return 0;
    }
    std::cout << "AES Bridge Windows backend scaffold\n"
              << "  --self-test  Validate L24/RTP over Winsock loopback\n"
              << "A future virtual WASAPI endpoint will connect to this backend.\n";
    return 0;
}
