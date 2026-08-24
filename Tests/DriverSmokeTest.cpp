// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace lxtool::aes67;

OSStatus propertiesChanged(AudioServerPlugInHostRef, AudioObjectID, UInt32,
    const AudioObjectPropertyAddress*) { return kAudioHardwareNoError; }
OSStatus copyFromStorage(AudioServerPlugInHostRef, CFStringRef, CFPropertyListRef* value) {
    if (value) *value = nullptr;
    return kAudioHardwareNoError;
}
OSStatus writeToStorage(AudioServerPlugInHostRef, CFStringRef, CFPropertyListRef) { return kAudioHardwareNoError; }
OSStatus deleteFromStorage(AudioServerPlugInHostRef, CFStringRef) { return kAudioHardwareNoError; }
OSStatus requestConfigurationChange(AudioServerPlugInHostRef, AudioObjectID, UInt64, void*) {
    return kAudioHardwareNoError;
}

struct DriverAccess final {
    AudioServerPlugInDriverRef reference{nullptr};
    AudioServerPlugInDriverInterface* interface{nullptr};

    bool data(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope,
        std::vector<std::uint8_t>& result) const {
        const AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
        UInt32 size = 0;
        if (interface->GetPropertyDataSize(reference, object, 0, &address, 0, nullptr, &size) != kAudioHardwareNoError) return false;
        result.resize(size);
        UInt32 used = 0;
        return interface->GetPropertyData(reference, object, 0, &address, 0, nullptr, size, &used, result.data()) == kAudioHardwareNoError
            && used == size;
    }

    template <typename Value>
    bool pod(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope, Value& result) const {
        const AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
        UInt32 used = 0;
        return interface->GetPropertyData(reference, object, 0, &address, 0, nullptr,
            static_cast<UInt32>(sizeof(result)), &used, &result) == kAudioHardwareNoError && used == sizeof(result);
    }

    bool text(AudioObjectID object, AudioObjectPropertySelector selector, std::string& result) const {
        CFStringRef value = nullptr;
        if (!pod(object, selector, kAudioObjectPropertyScopeGlobal, value) || !value) return false;
        char buffer[256]{};
        const bool ok = CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8);
        CFRelease(value);
        if (ok) result = buffer;
        return ok;
    }
};

bool checkStream(const DriverAccess& driver, AudioObjectID stream) {
    AudioStreamBasicDescription format{};
    if (!driver.pod(stream, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, format)) return false;
    return std::abs(format.mSampleRate - static_cast<Float64>(kSampleRate)) < 0.5
        && format.mFormatID == kAudioFormatLinearPCM
        && (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0
        && (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0
        && format.mChannelsPerFrame == kVirtualChannels
        && format.mBitsPerChannel == 32
        && format.mBytesPerFrame == kVirtualChannels * sizeof(Float32)
        && format.mFramesPerPacket == 1;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: AESBridgeDriverSmoke <driver-binary>\n";
        return 2;
    }
    void* image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!image) {
        std::cerr << "dlopen: " << dlerror() << '\n';
        return 1;
    }
    using Factory = void* (*)(CFAllocatorRef, CFUUIDRef);
    Factory factory = nullptr;
    void* factorySymbol = dlsym(image, "AESBridgePlugInFactory");
    static_assert(sizeof(factory) == sizeof(factorySymbol));
    std::memcpy(&factory, &factorySymbol, sizeof(factory));
    if (!factory) {
        std::cerr << "factory absente\n";
        return 1;
    }
    DriverAccess driver;
    driver.reference = static_cast<AudioServerPlugInDriverRef>(factory(kCFAllocatorDefault, kAudioServerPlugInTypeUUID));
    if (!driver.reference || !*driver.reference) {
        std::cerr << "factory invalide\n";
        return 1;
    }
    driver.interface = *driver.reference;
    AudioServerPlugInHostInterface host{};
    host.PropertiesChanged = propertiesChanged;
    host.CopyFromStorage = copyFromStorage;
    host.WriteToStorage = writeToStorage;
    host.DeleteFromStorage = deleteFromStorage;
    host.RequestDeviceConfigurationChange = requestConfigurationChange;
    if (driver.interface->Initialize(driver.reference, &host) != kAudioHardwareNoError) {
        std::cerr << "initialisation HAL échouée\n";
        return 1;
    }

    std::vector<std::uint8_t> deviceBytes;
    if (!driver.data(kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList,
        kAudioObjectPropertyScopeGlobal, deviceBytes) || deviceBytes.size() != sizeof(AudioObjectID)) {
        std::cerr << "liste de périphériques invalide\n";
        return 1;
    }
    AudioObjectID device = kAudioObjectUnknown;
    std::memcpy(&device, deviceBytes.data(), sizeof(device));
    std::string name;
    std::string uid;
    Float64 rate = 0;
    const bool hasName = driver.text(device, kAudioObjectPropertyName, name);
    const bool hasUid = driver.text(device, kAudioDevicePropertyDeviceUID, uid);
    const bool hasRate = driver.pod(device, kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal, rate);
    if (!hasName || name != "AES Bridge" || !hasUid || uid != "org.maxpierr.aesbridge.device"
        || !hasRate || std::abs(rate - static_cast<Float64>(kSampleRate)) >= 0.5) {
        std::cerr << "identité ou fréquence du périphérique invalide: name="
                  << (hasName ? name : "<absent>") << ", uid=" << (hasUid ? uid : "<absent>")
                  << ", rate=" << (hasRate ? std::to_string(rate) : "<absente>") << '\n';
        return 1;
    }

    for (const auto scope : {kAudioObjectPropertyScopeInput, kAudioObjectPropertyScopeOutput}) {
        std::vector<std::uint8_t> streamBytes;
        if (!driver.data(device, kAudioDevicePropertyStreams, scope, streamBytes)
            || streamBytes.size() != sizeof(AudioObjectID)) {
            std::cerr << "flux HAL 64 canaux invalide\n";
            return 1;
        }
        AudioObjectID stream = kAudioObjectUnknown;
        std::memcpy(&stream, streamBytes.data(), sizeof(stream));
        if (!checkStream(driver, stream)) {
            std::cerr << "format du flux HAL invalide\n";
            return 1;
        }
    }
    std::cout << "AES Bridge HAL smoke test passed: 64 inputs, 64 outputs, 48 kHz\n";
    return 0;
}
