// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SharedAudioMemory.hpp"

#include <aspl/Context.hpp>
#include <aspl/Device.hpp>
#include <aspl/Stream.hpp>
#include <memory>

namespace lxtool::aes67 {

class LXToolIOHandler;

class LXToolDevice final : public aspl::Device {
public:
    explicit LXToolDevice(std::shared_ptr<aspl::Context> context);
    void initialize();
    std::vector<AudioValueRange> GetAvailableSampleRates() const override;
    std::string GetDeviceUID() const override;
    OSStatus StartIOImpl(UInt32 clientID, UInt32 startCount) override;
    OSStatus StopIOImpl(UInt32 clientID, UInt32 startCount) override;
private:
    SharedAudioMemory sharedMemory_;
    SharedAudioBlock* bridge_{nullptr};
    std::shared_ptr<aspl::Stream> inputStream_;
    std::shared_ptr<aspl::Stream> outputStream_;
    std::shared_ptr<LXToolIOHandler> ioHandler_;
};

} // namespace lxtool::aes67
