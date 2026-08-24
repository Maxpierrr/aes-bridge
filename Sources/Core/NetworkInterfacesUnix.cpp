// SPDX-License-Identifier: GPL-3.0-only
#include "Core/NetworkInterfaces.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

namespace lxtool::aes67 {

std::vector<IPv4NetworkInterface> listIPv4NetworkInterfaces() {
    ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0) return {};
    std::vector<IPv4NetworkInterface> result;
    for (auto* item = head; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET) continue;
        char address[INET_ADDRSTRLEN]{};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        if (!inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) continue;
        result.push_back({item->ifa_name, address, (item->ifa_flags & IFF_LOOPBACK) != 0});
    }
    freeifaddrs(head);
    return result;
}

std::string ipv4AddressForInterface(std::string_view name) {
    for (const auto& interface : listIPv4NetworkInterfaces()) {
        if (interface.name == name) return interface.address;
    }
    return {};
}

} // namespace lxtool::aes67
