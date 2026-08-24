// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

struct sockaddr_in;

namespace lxtool::aes67 {

class UDPSocket final {
public:
    UDPSocket() = default;
    ~UDPSocket();
    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;
    bool openReceiver(const std::string& address, std::uint16_t port, const std::string& interfaceAddress);
    bool openTransmitter(const std::string& address, std::uint16_t port, const std::string& interfaceAddress);
    std::ptrdiff_t send(std::span<const std::uint8_t> bytes) noexcept;
    std::ptrdiff_t receive(std::span<std::uint8_t> bytes, std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] std::uint16_t localPort() const noexcept;
    [[nodiscard]] int lastError() const noexcept { return lastError_; }
    void close() noexcept;
private:
    int descriptor_{-1};
    sockaddr_in* destination_{nullptr};
    int lastError_{0};
};

} // namespace lxtool::aes67
