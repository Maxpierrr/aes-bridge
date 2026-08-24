// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SharedAudioMemory.hpp"
#include "Core/JitterBuffer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lxtool::aes67 {

class PTPClient;

struct LiveEngineConfig final {
    std::string interfaceName;
    std::string interfaceAddress;
    std::string rxAddress{"239.69.83.80"};
    std::string rxSourceAddress;
    std::string txAddress{"239.69.83.81"};
    std::uint16_t rxPort{5004};
    std::uint16_t txPort{5004};
    std::uint8_t rxPayloadType{kPayloadType};
    std::uint8_t txPayloadType{kPayloadType};
    std::size_t streamCount{1};
    std::uint16_t portStride{0};
    std::size_t jitterPackets{kDefaultJitterPackets};
    std::string sapAddress{std::string(kSAPMulticast)};
    std::uint16_t sapPort{kSAPPort};
    std::uint32_t sapSessionTimeoutSeconds{15};
    bool enableSAPPublication{true};
    bool enableSAPDiscovery{true};
    bool enablePTP{true};
    std::string ptpAddress{"224.0.1.129"};
    std::uint16_t ptpEventPort{319};
    std::uint16_t ptpGeneralPort{320};
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
    struct StreamRuntime final {
        explicit StreamRuntime(std::size_t jitterPackets) : jitter(jitterPackets) {}
        JitterBuffer<64, 1200> jitter;
        std::atomic<bool> jitterResetRequested{false};
        bool rxActive{false};
        bool txActive{false};
        std::thread receiveThread;
        std::thread consumeThread;
        std::thread transmitThread;
    };
    void receiveLoop(std::size_t bank, StreamRuntime& runtime);
    void consumeLoop(std::size_t bank, StreamRuntime& runtime);
    void transmitLoop(std::size_t bank, StreamRuntime& runtime);
    void sapPublishLoop();
    void sapDiscoveryLoop();
    void setRxActive(StreamRuntime& runtime, bool active) noexcept;
    void setTxActive(StreamRuntime& runtime, bool active) noexcept;
    LiveEngineConfig config_;
    SharedAudioMemory sharedMemory_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> activeReceivers_{0};
    std::atomic<std::size_t> activeTransmitters_{0};
    std::chrono::steady_clock::time_point transmitEpoch_{};
    std::int64_t transmitSystemEpochNanoseconds_{0};
    std::vector<std::unique_ptr<StreamRuntime>> streams_;
    std::thread sapPublishThread_;
    std::thread sapDiscoveryThread_;
    std::unique_ptr<PTPClient> ptpClient_;
};

} // namespace lxtool::aes67
