// SPDX-License-Identifier: GPL-3.0-only
#include "Core/UDPSocket.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cerrno>
#include <climits>
#include <limits>

namespace lxtool::aes67 {
namespace {
class WinsockRuntime final {
public:
    WinsockRuntime() noexcept {
        WSADATA data{};
        error_ = WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockRuntime() { if (error_ == 0) WSACleanup(); }
    [[nodiscard]] bool ready() const noexcept { return error_ == 0; }
    [[nodiscard]] int error() const noexcept { return error_; }
private:
    int error_{0};
};

WinsockRuntime& winsock() {
    static WinsockRuntime runtime;
    return runtime;
}

SOCKET nativeSocket(std::intptr_t descriptor) noexcept { return static_cast<SOCKET>(descriptor); }

bool parseAddress(const std::string& text, in_addr& address) noexcept {
    return InetPtonA(AF_INET, text.c_str(), &address) == 1;
}

bool isMulticast(in_addr address) noexcept {
    const auto host = ntohl(address.s_addr);
    return host >= 0xe0000000U && host <= 0xefffffffU;
}
}

UDPSocket::~UDPSocket() { close(); }

bool UDPSocket::openReceiver(const std::string& address, std::uint16_t port,
    const std::string& interfaceAddress, const std::string& sourceAddress) {
    close();
    if (!winsock().ready()) { lastError_ = winsock().error(); return false; }
    in_addr group{}, interface{}, source{};
    if (!parseAddress(address, group) || (!interfaceAddress.empty() && !parseAddress(interfaceAddress, interface))
        || (!sourceAddress.empty() && !parseAddress(sourceAddress, source))) { lastError_ = WSAEINVAL; return false; }
    const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) { lastError_ = WSAGetLastError(); return false; }
    descriptor_ = static_cast<std::intptr_t>(socketHandle);
    BOOL one = TRUE;
    setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), static_cast<int>(sizeof(one)));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&local), static_cast<int>(sizeof(local))) == SOCKET_ERROR) {
        lastError_ = WSAGetLastError(); close(); return false;
    }
    if (isMulticast(group)) {
        if (!sourceAddress.empty()) {
            ip_mreq_source membership{};
            membership.imr_multiaddr = group;
            membership.imr_sourceaddr = source;
            membership.imr_interface.s_addr = interfaceAddress.empty() ? htonl(INADDR_ANY) : interface.s_addr;
            if (setsockopt(socketHandle, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                reinterpret_cast<const char*>(&membership), static_cast<int>(sizeof(membership))) == SOCKET_ERROR) {
                lastError_ = WSAGetLastError(); close(); return false;
            }
        } else {
            ip_mreq membership{};
            membership.imr_multiaddr = group;
            membership.imr_interface.s_addr = interfaceAddress.empty() ? htonl(INADDR_ANY) : interface.s_addr;
            if (setsockopt(socketHandle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                reinterpret_cast<const char*>(&membership), static_cast<int>(sizeof(membership))) == SOCKET_ERROR) {
                lastError_ = WSAGetLastError(); close(); return false;
            }
        }
    }
    return true;
}

bool UDPSocket::openTransmitter(const std::string& address, std::uint16_t port,
    const std::string& interfaceAddress) {
    close();
    if (!winsock().ready()) { lastError_ = winsock().error(); return false; }
    in_addr target{}, interface{};
    if (!parseAddress(address, target) || (!interfaceAddress.empty() && !parseAddress(interfaceAddress, interface))) {
        lastError_ = WSAEINVAL; return false;
    }
    const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) { lastError_ = WSAGetLastError(); return false; }
    descriptor_ = static_cast<std::intptr_t>(socketHandle);
    if (isMulticast(target)) {
        DWORD ttl = 32;
        DWORD loop = FALSE;
        setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), static_cast<int>(sizeof(ttl)));
        setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), static_cast<int>(sizeof(loop)));
        if (!interfaceAddress.empty() && setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_IF,
            reinterpret_cast<const char*>(&interface), static_cast<int>(sizeof(interface))) == SOCKET_ERROR) {
            lastError_ = WSAGetLastError(); close(); return false;
        }
    }
    destination_ = new sockaddr_in{};
    destination_->sin_family = AF_INET;
    destination_->sin_port = htons(port);
    destination_->sin_addr = target;
    return true;
}

std::ptrdiff_t UDPSocket::send(std::span<const std::uint8_t> bytes) noexcept {
    if (descriptor_ < 0 || !destination_ || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        lastError_ = WSAEINVAL; return -1;
    }
    const int result = sendto(nativeSocket(descriptor_), reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(destination_), static_cast<int>(sizeof(*destination_)));
    if (result == SOCKET_ERROR) { lastError_ = WSAGetLastError(); return -1; }
    return result;
}

std::ptrdiff_t UDPSocket::receive(std::span<std::uint8_t> bytes, std::chrono::milliseconds timeout) noexcept {
    if (descriptor_ < 0 || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        lastError_ = WSAEINVAL; return -1;
    }
    WSAPOLLFD pollDescriptor{nativeSocket(descriptor_), POLLRDNORM, 0};
    const auto timeoutCount = timeout.count();
    const int timeoutMilliseconds = timeoutCount > INT_MAX ? INT_MAX
        : timeoutCount < 0 ? 0 : static_cast<int>(timeoutCount);
    const int ready = WSAPoll(&pollDescriptor, 1, timeoutMilliseconds);
    if (ready == 0) return 0;
    if (ready == SOCKET_ERROR) { lastError_ = WSAGetLastError(); return -1; }
    const int result = recv(nativeSocket(descriptor_), reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
    if (result == SOCKET_ERROR) { lastError_ = WSAGetLastError(); return -1; }
    return result;
}

std::uint16_t UDPSocket::localPort() const noexcept {
    if (descriptor_ < 0) return 0;
    sockaddr_in address{};
    int length = static_cast<int>(sizeof(address));
    return getsockname(nativeSocket(descriptor_), reinterpret_cast<sockaddr*>(&address), &length) == 0 ? ntohs(address.sin_port) : 0;
}

void UDPSocket::close() noexcept {
    if (descriptor_ >= 0) closesocket(nativeSocket(descriptor_));
    descriptor_ = -1;
    delete destination_;
    destination_ = nullptr;
}

} // namespace lxtool::aes67
