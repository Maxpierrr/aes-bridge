// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/PTP.hpp"
#include "Core/SharedAudioMemory.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace lxtool::aes67 {

struct PTPClientConfig final {
    std::string multicastAddress{"224.0.1.129"};
    std::string interfaceAddress;
    std::uint16_t eventReceivePort{319};
    std::uint16_t eventTransmitPort{319};
    std::uint16_t generalReceivePort{320};
    std::uint8_t domain{kPTPDomain};
    PTPPortIdentity localIdentity;
};

class PTPClient final {
public:
    PTPClient(PTPClientConfig config, SharedAudioBlock& sharedBlock);
    ~PTPClient();
    PTPClient(const PTPClient&) = delete;
    PTPClient& operator=(const PTPClient&) = delete;
    bool start();
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    static PTPPortIdentity identityFromAddress(const std::string& interfaceAddress) noexcept;
private:
    void loop();
    PTPClientConfig config_;
    SharedAudioBlock& sharedBlock_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace lxtool::aes67
