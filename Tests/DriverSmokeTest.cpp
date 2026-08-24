// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/SharedAudioMemory.hpp"
#include "Engine/LiveEngine.hpp"

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
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

struct SharedMemoryFixture final {
    SharedAudioMemory memory;
    ~SharedMemoryFixture() {
        memory.close();
        (void)SharedAudioMemory::remove();
    }
};

bool runOperation(const DriverAccess& driver, AudioObjectID device, AudioObjectID stream,
    UInt32 clientID, UInt32 operation, UInt32 frameCount, AudioServerPlugInIOCycleInfo& cycle,
    void* buffer) {
    Boolean willDo = false;
    Boolean inPlace = false;
    if (driver.interface->WillDoIOOperation(driver.reference, device, clientID, operation,
            &willDo, &inPlace) != kAudioHardwareNoError || !willDo || !inPlace) return false;
    if (driver.interface->BeginIOOperation(driver.reference, device, clientID, operation,
            frameCount, &cycle) != kAudioHardwareNoError) return false;
    const auto status = driver.interface->DoIOOperation(driver.reference, device, stream,
        clientID, operation, frameCount, &cycle, buffer, nullptr);
    const auto endStatus = driver.interface->EndIOOperation(driver.reference, device, clientID,
        operation, frameCount, &cycle);
    return status == kAudioHardwareNoError && endStatus == kAudioHardwareNoError;
}

bool approximatelyEqual(Float32 left, Float32 right) {
    return std::abs(left - right) < 0.000001F;
}

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: AESBridgeDriverSmoke <driver-binary>\n";
        return 2;
    }
    SharedMemoryFixture shared;
    if (!SharedAudioMemory::remove() || !shared.memory.open(true)) {
        std::cerr << "mémoire audio partagée indisponible\n";
        return 1;
    }
    auto* block = shared.memory.get();
    block->engineRunning.store(true, std::memory_order_release);

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

    std::array<AudioObjectID, 2> streams{};
    const std::array<AudioObjectPropertyScope, 2> scopes{
        kAudioObjectPropertyScopeInput, kAudioObjectPropertyScopeOutput};
    for (std::size_t index = 0; index < scopes.size(); ++index) {
        std::vector<std::uint8_t> streamBytes;
        if (!driver.data(device, kAudioDevicePropertyStreams, scopes[index], streamBytes)
            || streamBytes.size() != sizeof(AudioObjectID)) {
            std::cerr << "flux HAL 64 canaux invalide\n";
            return 1;
        }
        std::memcpy(&streams[index], streamBytes.data(), sizeof(streams[index]));
        if (!checkStream(driver, streams[index])) {
            std::cerr << "format du flux HAL invalide\n";
            return 1;
        }
    }

    constexpr UInt32 clientID = 42;
    AudioServerPlugInClientInfo client{};
    client.mClientID = clientID;
    client.mProcessID = 1;
    client.mIsNativeEndian = true;
    client.mBundleID = CFSTR("org.maxpierr.aesbridge.smoke-test");
    if (driver.interface->AddDeviceClient(driver.reference, device, &client) != kAudioHardwareNoError
        || driver.interface->StartIO(driver.reference, device, clientID) != kAudioHardwareNoError
        || !block->ioRunning.load(std::memory_order_acquire)) {
        std::cerr << "démarrage des E/S HAL échoué\n";
        return 1;
    }

    Float64 sampleTime = 0;
    UInt64 hostTime = 0;
    UInt64 seed = 0;
    if (driver.interface->GetZeroTimeStamp(driver.reference, device, clientID,
            &sampleTime, &hostTime, &seed) != kAudioHardwareNoError || hostTime == 0 || seed == 0) {
        std::cerr << "horloge HAL invalide\n";
        return 1;
    }

    constexpr UInt32 frameCount = static_cast<UInt32>(kFramesPerPacket);
    constexpr std::size_t sampleCount = kFramesPerPacket * kVirtualChannels;
    std::array<Float32, sampleCount> expectedInput{};
    std::array<Float32, sampleCount> inputBuffer{};
    std::array<Float32, kFramesPerPacket> channel{};
    for (std::size_t channelIndex = 0; channelIndex < kVirtualChannels; ++channelIndex) {
        for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) {
            channel[frame] = static_cast<Float32>(channelIndex + 1) / 100.0F
                + static_cast<Float32>(frame) / 100'000.0F;
            expectedInput[frame * kVirtualChannels + channelIndex] = channel[frame];
        }
        if (block->networkToCoreAudio[channelIndex].write(channel) != channel.size()) {
            std::cerr << "préparation des entrées partagées échouée\n";
            return 1;
        }
    }
    AudioServerPlugInIOCycleInfo cycle{};
    cycle.mInputTime.mSampleTime = sampleTime;
    cycle.mInputTime.mFlags = kAudioTimeStampSampleTimeValid;
    cycle.mOutputTime = cycle.mInputTime;
    if (!runOperation(driver, device, streams[0], clientID,
            kAudioServerPlugInIOOperationReadInput, frameCount, cycle, inputBuffer.data())
        || !std::equal(inputBuffer.begin(), inputBuffer.end(), expectedInput.begin(), approximatelyEqual)) {
        std::cerr << "callback d’entrée HAL 64 canaux invalide\n";
        return 1;
    }

    std::array<Float32, sampleCount> outputBuffer{};
    for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) {
        for (std::size_t channelIndex = 0; channelIndex < kVirtualChannels; ++channelIndex) {
            outputBuffer[frame * kVirtualChannels + channelIndex]
                = -static_cast<Float32>(channelIndex + 1) / 100.0F
                - static_cast<Float32>(frame) / 100'000.0F;
        }
    }
    if (!runOperation(driver, device, streams[1], clientID,
            kAudioServerPlugInIOOperationProcessMix, frameCount, cycle, outputBuffer.data())
        || !runOperation(driver, device, streams[1], clientID,
            kAudioServerPlugInIOOperationWriteMix, frameCount, cycle, outputBuffer.data())) {
        std::cerr << "callback de sortie HAL mixée invalide\n";
        return 1;
    }
    for (std::size_t channelIndex = 0; channelIndex < kVirtualChannels; ++channelIndex) {
        std::array<Float32, kFramesPerPacket> received{};
        if (block->coreAudioToNetwork[channelIndex].read(received) != received.size()) {
            std::cerr << "sortie HAL absente sur le canal " << channelIndex + 1 << '\n';
            return 1;
        }
        for (std::size_t frame = 0; frame < kFramesPerPacket; ++frame) {
            const auto expected = outputBuffer[frame * kVirtualChannels + channelIndex];
            if (!approximatelyEqual(received[frame], expected)) {
                std::cerr << "ordre de sortie HAL invalide sur le canal " << channelIndex + 1 << '\n';
                return 1;
            }
        }
    }

    block->engineRunning.store(false, std::memory_order_release);
    inputBuffer.fill(1.0F);
    if (!runOperation(driver, device, streams[0], clientID,
            kAudioServerPlugInIOOperationReadInput, frameCount, cycle, inputBuffer.data())
        || !std::all_of(inputBuffer.begin(), inputBuffer.end(), [](Float32 sample) { return sample == 0.0F; })) {
        std::cerr << "silence de sécurité HAL invalide\n";
        return 1;
    }
    if (driver.interface->StopIO(driver.reference, device, clientID) != kAudioHardwareNoError
        || block->ioRunning.load(std::memory_order_acquire)) {
        std::cerr << "arrêt des E/S HAL échoué\n";
        return 1;
    }

    shared.memory.close();
    LiveEngineConfig engineConfig;
    engineConfig.interfaceAddress = "127.0.0.1";
    engineConfig.rxAddress = "127.0.0.1";
    engineConfig.txAddress = "127.0.0.1";
    engineConfig.rxPort = 55020;
    engineConfig.txPort = 55020;
    engineConfig.streamCount = kStreamBankCount;
    engineConfig.portStride = 1;
    engineConfig.jitterPackets = 3;
    engineConfig.enableSAPPublication = false;
    engineConfig.enableSAPDiscovery = false;
    engineConfig.enablePTP = false;
    LiveEngine engine(engineConfig);
    if (!engine.start()) {
        std::cerr << "démarrage du moteur loopback échoué\n";
        return 1;
    }
    block = engine.sharedBlock();
    if (!block || driver.interface->StartIO(driver.reference, device, clientID) != kAudioHardwareNoError
        || !block->ioRunning.load(std::memory_order_acquire)) {
        std::cerr << "redémarrage des E/S HAL avec le moteur échoué\n";
        return 1;
    }

    constexpr UInt32 injectedFrameCount = static_cast<UInt32>(kFramesPerPacket * 32);
    std::vector<Float32> roundtripOutput(
        static_cast<std::size_t>(injectedFrameCount) * kVirtualChannels);
    for (std::size_t frame = 0; frame < injectedFrameCount; ++frame) {
        for (std::size_t channelIndex = 0; channelIndex < kVirtualChannels; ++channelIndex) {
            roundtripOutput[frame * kVirtualChannels + channelIndex]
                = static_cast<Float32>(channelIndex + 1) / 100.0F;
        }
    }
    if (!runOperation(driver, device, streams[1], clientID,
            kAudioServerPlugInIOOperationProcessMix, injectedFrameCount, cycle, roundtripOutput.data())
        || !runOperation(driver, device, streams[1], clientID,
            kAudioServerPlugInIOOperationWriteMix, injectedFrameCount, cycle, roundtripOutput.data())) {
        std::cerr << "injection HAL vers RTP échouée\n";
        return 1;
    }

    constexpr std::size_t minimumReturnedFrames = kFramesPerPacket * 8;
    if (!waitUntil([&] {
            if (block->statistics.txPackets.load(std::memory_order_acquire) < kStreamBankCount * 8
                || block->statistics.rxPackets.load(std::memory_order_acquire) < kStreamBankCount * 8) return false;
            return std::all_of(block->networkToCoreAudio.begin(), block->networkToCoreAudio.end(),
                [](const auto& ring) { return ring.available() >= minimumReturnedFrames; });
        }, std::chrono::milliseconds(1500))) {
        std::cerr << "retour RTP 64 canaux incomplet\n";
        return 1;
    }

    constexpr UInt32 returnedFrameCount = 4096;
    std::vector<Float32> roundtripInput(
        static_cast<std::size_t>(returnedFrameCount) * kVirtualChannels, 0.0F);
    if (!runOperation(driver, device, streams[0], clientID,
            kAudioServerPlugInIOOperationReadInput, returnedFrameCount, cycle, roundtripInput.data())) {
        std::cerr << "lecture du retour RTP par HAL échouée\n";
        return 1;
    }
    for (std::size_t channelIndex = 0; channelIndex < kVirtualChannels; ++channelIndex) {
        const auto expected = static_cast<Float32>(channelIndex + 1) / 100.0F;
        std::size_t matchingFrames = 0;
        bool found = false;
        for (std::size_t frame = 0; frame < returnedFrameCount; ++frame) {
            if (approximatelyEqual(roundtripInput[frame * kVirtualChannels + channelIndex], expected)) {
                if (++matchingFrames == kFramesPerPacket) { found = true; break; }
            } else {
                matchingFrames = 0;
            }
        }
        if (!found) {
            std::cerr << "retour Core Audio/RTP invalide sur le canal " << channelIndex + 1 << '\n';
            return 1;
        }
    }
    if (driver.interface->StopIO(driver.reference, device, clientID) != kAudioHardwareNoError
        || block->ioRunning.load(std::memory_order_acquire)
        || driver.interface->RemoveDeviceClient(driver.reference, device, &client) != kAudioHardwareNoError) {
        std::cerr << "arrêt du test bout en bout échoué\n";
        return 1;
    }
    engine.stop();

    std::cout << "AES Bridge HAL smoke test passed: Core Audio 64x64 -> eight RTP banks -> Core Audio at 48 kHz\n";
    return 0;
}
