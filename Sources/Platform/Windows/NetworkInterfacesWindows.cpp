// SPDX-License-Identifier: GPL-3.0-only
#include "Core/NetworkInterfaces.hpp"
#include "Core/IPv4Address.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <cstddef>
#include <cwchar>
#include <string>
#include <vector>

namespace lxtool::aes67 {
namespace {
std::string utf8(const wchar_t* text) {
    if (!text || *text == L'\0') return {};
    const int length = static_cast<int>(std::wcslen(text));
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), required, nullptr, nullptr) == 0) return {};
    return result;
}
}

std::vector<IPv4NetworkInterface> listIPv4NetworkInterfaces() {
    ULONG size = 16'384;
    std::vector<std::byte> storage(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    ULONG result = GetAdaptersAddresses(AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        result = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &size);
    }
    if (result != NO_ERROR) return {};

    std::vector<IPv4NetworkInterface> interfaces;
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) continue;
        std::string name = utf8(adapter->FriendlyName);
        if (name.empty() && adapter->AdapterName) name = adapter->AdapterName;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            const auto value = ntohl(ipv4->sin_addr.s_addr);
            const IPv4Address address{{static_cast<std::uint8_t>(value >> 24U),
                static_cast<std::uint8_t>(value >> 16U), static_cast<std::uint8_t>(value >> 8U),
                static_cast<std::uint8_t>(value)}};
            interfaces.push_back({name, address.toString(), adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK});
        }
    }
    return interfaces;
}

std::string ipv4AddressForInterface(std::string_view name) {
    for (const auto& interface : listIPv4NetworkInterfaces()) {
        if (interface.name == name) return interface.address;
    }
    return {};
}

} // namespace lxtool::aes67
