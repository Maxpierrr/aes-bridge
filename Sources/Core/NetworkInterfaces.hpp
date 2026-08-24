// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lxtool::aes67 {

struct IPv4NetworkInterface final {
    std::string name;
    std::string address;
    bool loopback{false};
};

[[nodiscard]] std::vector<IPv4NetworkInterface> listIPv4NetworkInterfaces();
[[nodiscard]] std::string ipv4AddressForInterface(std::string_view name);

} // namespace lxtool::aes67
