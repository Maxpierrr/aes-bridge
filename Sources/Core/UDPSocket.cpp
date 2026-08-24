// SPDX-License-Identifier: GPL-3.0-only
#include "Core/UDPSocket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace lxtool::aes67 {
namespace {
bool parseAddress(const std::string& text, in_addr& address) noexcept {
    return inet_pton(AF_INET, text.c_str(), &address) == 1;
}
bool isMulticast(in_addr address) noexcept {
    const auto host = ntohl(address.s_addr);
    return host >= 0xe0000000U && host <= 0xefffffffU;
}
}

UDPSocket::~UDPSocket() { close(); }

bool UDPSocket::openReceiver(const std::string& address, std::uint16_t port, const std::string& interfaceAddress) {
    close();
    in_addr group{}, interface{};
    if (!parseAddress(address, group) || (!interfaceAddress.empty() && !parseAddress(interfaceAddress, interface))) { lastError_ = EINVAL; return false; }
    descriptor_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (descriptor_ < 0) { lastError_ = errno; return false; }
    int one = 1;
    setsockopt(descriptor_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(descriptor_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(port); local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(descriptor_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) { lastError_ = errno; close(); return false; }
    if (isMulticast(group)) {
        ip_mreq membership{}; membership.imr_multiaddr = group; membership.imr_interface.s_addr = interfaceAddress.empty() ? htonl(INADDR_ANY) : interface.s_addr;
        if (setsockopt(descriptor_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) { lastError_ = errno; close(); return false; }
    }
    return true;
}

bool UDPSocket::openTransmitter(const std::string& address, std::uint16_t port, const std::string& interfaceAddress) {
    close();
    in_addr target{}, interface{};
    if (!parseAddress(address, target) || (!interfaceAddress.empty() && !parseAddress(interfaceAddress, interface))) { lastError_ = EINVAL; return false; }
    descriptor_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (descriptor_ < 0) { lastError_ = errno; return false; }
    if (isMulticast(target)) {
        const unsigned char ttl = 32, loop = 0;
        setsockopt(descriptor_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        setsockopt(descriptor_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
        if (!interfaceAddress.empty() && setsockopt(descriptor_, IPPROTO_IP, IP_MULTICAST_IF, &interface, sizeof(interface)) != 0) {
            lastError_ = errno; close(); return false;
        }
    }
    destination_ = new sockaddr_in{};
    destination_->sin_family = AF_INET; destination_->sin_port = htons(port); destination_->sin_addr = target;
    return true;
}

std::ptrdiff_t UDPSocket::send(std::span<const std::uint8_t> bytes) noexcept {
    if (descriptor_ < 0 || !destination_) { lastError_ = EBADF; return -1; }
    const auto result = sendto(descriptor_, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(destination_), sizeof(*destination_));
    if (result < 0) lastError_ = errno;
    return result;
}

std::ptrdiff_t UDPSocket::receive(std::span<std::uint8_t> bytes, std::chrono::milliseconds timeout) noexcept {
    if (descriptor_ < 0) { lastError_ = EBADF; return -1; }
    pollfd descriptor{descriptor_, POLLIN, 0};
    const int ready = poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready == 0) return 0;
    if (ready < 0) { lastError_ = errno; return -1; }
    const auto result = recvfrom(descriptor_, bytes.data(), bytes.size(), 0, nullptr, nullptr);
    if (result < 0) lastError_ = errno;
    return result;
}

std::uint16_t UDPSocket::localPort() const noexcept {
    if (descriptor_ < 0) return 0;
    sockaddr_in address{}; socklen_t length = sizeof(address);
    return getsockname(descriptor_, reinterpret_cast<sockaddr*>(&address), &length) == 0 ? ntohs(address.sin_port) : 0;
}

void UDPSocket::close() noexcept {
    if (descriptor_ >= 0) ::close(descriptor_);
    descriptor_ = -1;
    delete destination_;
    destination_ = nullptr;
}

} // namespace lxtool::aes67
