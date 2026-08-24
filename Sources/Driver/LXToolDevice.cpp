// SPDX-License-Identifier: GPL-3.0-only
#include "Driver/LXToolDevice.hpp"
#include "Driver/LXToolIOHandler.hpp"

#include <CoreAudio/AudioServerPlugIn.h>

namespace lxtool::aes67 {
namespace {
aspl::StreamParameters streamParameters(aspl::Direction direction) {
    aspl::StreamParameters parameters;
    parameters.Direction = direction;
    parameters.StartingChannel = 1;
    parameters.Format.mSampleRate = kSampleRate;
    parameters.Format.mFormatID = kAudioFormatLinearPCM;
    parameters.Format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    parameters.Format.mBitsPerChannel = 32;
    parameters.Format.mChannelsPerFrame = kVirtualChannels;
    parameters.Format.mBytesPerFrame = kVirtualChannels * sizeof(Float32);
    parameters.Format.mFramesPerPacket = 1;
    parameters.Format.mBytesPerPacket = parameters.Format.mBytesPerFrame;
    return parameters;
}
}

LXToolDevice::LXToolDevice(std::shared_ptr<aspl::Context> context)
    : aspl::Device(context, aspl::DeviceParameters{
        .Name = "AES Bridge", .Manufacturer = "maxpierr",
        .DeviceUID = "org.maxpierr.aesbridge.device", .ModelUID = "org.maxpierr.aesbridge.model",
        .CanBeDefault = true, .CanBeDefaultForSystemSounds = false,
        .SampleRate = kSampleRate, .ChannelCount = kVirtualChannels}) {}

void LXToolDevice::initialize() {
    if (sharedMemory_.open(false)) bridge_ = sharedMemory_.get();
    const auto self = std::static_pointer_cast<aspl::Device>(shared_from_this());
    inputStream_ = std::make_shared<aspl::Stream>(GetContext(), self, streamParameters(aspl::Direction::Input));
    outputStream_ = std::make_shared<aspl::Stream>(GetContext(), self, streamParameters(aspl::Direction::Output));
    AddStreamAsync(inputStream_);
    AddStreamAsync(outputStream_);
    ioHandler_ = std::make_shared<LXToolIOHandler>(bridge_);
    SetIOHandler(ioHandler_);
}

std::vector<AudioValueRange> LXToolDevice::GetAvailableSampleRates() const { return {{kSampleRate, kSampleRate}}; }
std::string LXToolDevice::GetDeviceUID() const { return "org.maxpierr.aesbridge.device"; }

OSStatus LXToolDevice::StartIOImpl(UInt32 clientID, UInt32 startCount) {
    if (!bridge_ && sharedMemory_.open(false)) {
        bridge_ = sharedMemory_.get();
        ioHandler_->attach(bridge_);
    }
    if (startCount == 0 && bridge_) bridge_->ioRunning.store(true, std::memory_order_release);
    return aspl::Device::StartIOImpl(clientID, startCount);
}
OSStatus LXToolDevice::StopIOImpl(UInt32 clientID, UInt32 startCount) {
    if (startCount == 0 && bridge_) bridge_->ioRunning.store(false, std::memory_order_release);
    return aspl::Device::StopIOImpl(clientID, startCount);
}

} // namespace lxtool::aes67
