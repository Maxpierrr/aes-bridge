// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace lxtool::aes67;

template <typename Value>
bool property(AudioObjectID object, AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope, Value& value) {
    const AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    UInt32 size = static_cast<UInt32>(sizeof(value));
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr
        && size == sizeof(value);
}

std::optional<std::string> textProperty(AudioObjectID object,
    AudioObjectPropertySelector selector) {
    CFStringRef value = nullptr;
    if (!property(object, selector, kAudioObjectPropertyScopeGlobal, value) || !value) return std::nullopt;
    char text[256]{};
    const bool converted = CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8);
    CFRelease(value);
    if (!converted) return std::nullopt;
    return std::string(text);
}

template <typename Value>
std::vector<Value> propertyList(AudioObjectID object,
    AudioObjectPropertySelector selector, AudioObjectPropertyScope scope) {
    const AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) != noErr
        || size % sizeof(Value) != 0) return {};
    std::vector<Value> values(size / sizeof(Value));
    if (size != 0 && AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, values.data()) != noErr) return {};
    return values;
}

std::vector<AudioObjectID> objectList(AudioObjectID object,
    AudioObjectPropertySelector selector, AudioObjectPropertyScope scope) {
    return propertyList<AudioObjectID>(object, selector, scope);
}

struct DeviceSnapshot final {
    bool visible{false};
    bool valid{false};
    std::string name;
    std::string uid;
    double sampleRate{0};
    std::uint32_t inputChannels{0};
    std::uint32_t outputChannels{0};
    std::string error;
};

std::uint32_t channelCount(AudioDeviceID device, AudioObjectPropertyScope scope,
    std::string& error) {
    const auto streams = objectList(device, kAudioDevicePropertyStreams, scope);
    if (streams.size() != 1) {
        error = scope == kAudioObjectPropertyScopeInput
            ? "nombre de flux d'entrée inattendu" : "nombre de flux de sortie inattendu";
        return 0;
    }
    AudioStreamBasicDescription format{};
    if (!property(streams.front(), kAudioStreamPropertyVirtualFormat,
            kAudioObjectPropertyScopeGlobal, format)
        || format.mFormatID != kAudioFormatLinearPCM
        || (format.mFormatFlags & kAudioFormatFlagIsFloat) == 0
        || (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0
        || format.mBitsPerChannel != 32
        || std::abs(format.mSampleRate - static_cast<double>(kSampleRate)) >= 0.5
        || format.mBytesPerFrame != format.mChannelsPerFrame * sizeof(Float32)
        || format.mFramesPerPacket != 1
        || format.mBytesPerPacket != format.mBytesPerFrame) {
        error = scope == kAudioObjectPropertyScopeInput
            ? "format du flux d'entrée invalide" : "format du flux de sortie invalide";
        return 0;
    }
    return format.mChannelsPerFrame;
}

DeviceSnapshot inspectDevice() {
    DeviceSnapshot result;
    const auto devices = objectList(kAudioObjectSystemObject,
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal);
    for (const auto device : devices) {
        const auto uid = textProperty(device, kAudioDevicePropertyDeviceUID);
        if (!uid || *uid != "org.maxpierr.aesbridge.device") continue;
        result.visible = true;
        result.uid = *uid;
        result.name = textProperty(device, kAudioObjectPropertyName).value_or("");
        Float64 sampleRate = 0;
        if (!property(device, kAudioDevicePropertyNominalSampleRate,
                kAudioObjectPropertyScopeGlobal, sampleRate)) {
            result.error = "fréquence nominale illisible";
            return result;
        }
        result.sampleRate = sampleRate;
        const auto availableRates = propertyList<AudioValueRange>(device,
            kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal);
        if (availableRates.size() != 1
            || std::abs(availableRates.front().mMinimum - static_cast<double>(kSampleRate)) >= 0.5
            || std::abs(availableRates.front().mMaximum - static_cast<double>(kSampleRate)) >= 0.5) {
            result.error = "plage de fréquences disponible invalide";
            return result;
        }
        result.inputChannels = channelCount(device, kAudioObjectPropertyScopeInput, result.error);
        if (!result.error.empty()) return result;
        result.outputChannels = channelCount(device, kAudioObjectPropertyScopeOutput, result.error);
        if (!result.error.empty()) return result;
        result.valid = result.name == "AES Bridge"
            && std::abs(result.sampleRate - static_cast<double>(kSampleRate)) < 0.5
            && result.inputChannels == kVirtualChannels
            && result.outputChannels == kVirtualChannels;
        if (!result.valid) result.error = "identité, fréquence ou nombre de canaux inattendu";
        return result;
    }
    result.error = "périphérique AES Bridge absent";
    return result;
}

unsigned waitSeconds(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) != "--wait") continue;
        std::size_t consumed = 0;
        const auto value = std::stoul(argv[index + 1], &consumed);
        if (consumed != std::string(argv[index + 1]).size() || value > 120) throw std::out_of_range("wait");
        return static_cast<unsigned>(value);
    }
    return 0;
}

bool hasOption(int argc, char** argv, const std::string& option) {
    for (int index = 1; index < argc; ++index) if (argv[index] == option) return true;
    return false;
}
}

int main(int argc, char** argv) {
    unsigned wait = 0;
    try {
        wait = waitSeconds(argc, argv);
    } catch (const std::exception&) {
        std::cerr << "Usage: AESBridgeCoreAudioCheck [--wait 0..120] [--json]\n";
        return 2;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(wait);
    DeviceSnapshot snapshot;
    do {
        snapshot = inspectDevice();
        if (snapshot.visible || std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (true);

    if (hasOption(argc, argv, "--json")) {
        std::cout << "{\"visible\":" << (snapshot.visible ? "true" : "false")
                  << ",\"valid\":" << (snapshot.valid ? "true" : "false")
                  << ",\"name\":\"" << snapshot.name << "\""
                  << ",\"uid\":\"" << snapshot.uid << "\""
                  << ",\"sampleRate\":" << snapshot.sampleRate
                  << ",\"inputChannels\":" << snapshot.inputChannels
                  << ",\"outputChannels\":" << snapshot.outputChannels << "}\n";
    } else if (snapshot.valid) {
        std::cout << "AES Bridge visible dans Core Audio: 64 entrées, 64 sorties, 48 kHz.\n";
    } else {
        std::cerr << "Vérification Core Audio échouée: " << snapshot.error << "\n";
    }
    return snapshot.valid ? 0 : snapshot.visible ? 1 : 2;
}
