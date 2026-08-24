// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SharedAudioMemory.hpp"
#include "Core/JitterBuffer.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace lxtool::aes67 {

struct LiveEngineConfig final {
    std::string interfaceName;
    std::string interfaceAddress;
    std::string rxAddress{"239.69.83.80"};
    std::string txAddress{"239.69.83.81"};
    std::uint16_t rxPort{5004};
    std::uint16_t txPort{5004};
    std::uint8_t payloadType{kPayloadType};
    std::size_t jitterPackets{kDefaultJitterPackets};
    bool enableSAP{true};
};

class LiveEngine final {
public:
    explicit LiveEngine(LiveEngineConfig config);
    ~LiveEngine();
    LiveEngine(const LiveEngine&) = delete;
    LiveEngine& operator=(const LiveEngine&) = delete;
    bool start();
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] SharedAudioBlock* sharedBlock() const noexcept { return sharedMemory_.get(); }
    [[nodiscard]] const std::string& interfaceAddress() const noexcept { return config_.interfaceAddress; }
    static std::string interfaceIPv4(const std::string& interfaceName);
private:
    void receiveLoop();
    void consumeLoop();
    void transmitLoop();
    void sapLoop();
    LiveEngineConfig config_;
    JitterBuffer<64, 1200> jitter_;
    SharedAudioMemory sharedMemory_;
    std::atomic<bool> running_{false};
    std::thread receiveThread_;
    std::thread consumeThread_;
    std::thread transmitThread_;
    std::thread sapThread_;
};

} // namespace lxtool::aes67
