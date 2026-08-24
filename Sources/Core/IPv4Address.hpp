// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lxtool::aes67 {

struct IPv4Address final {
    std::array<std::uint8_t, 4> octets{};

    [[nodiscard]] static std::optional<IPv4Address> parse(std::string_view text) noexcept {
        IPv4Address result;
        for (std::size_t index = 0; index < result.octets.size(); ++index) {
            const auto separator = text.find('.');
            const auto component = text.substr(0, separator);
            unsigned value = 0;
            const auto parsed = std::from_chars(component.data(), component.data() + component.size(), value);
            if (component.empty() || parsed.ec != std::errc{} || parsed.ptr != component.data() + component.size() || value > 255) return std::nullopt;
            result.octets[index] = static_cast<std::uint8_t>(value);
            if (index + 1 < result.octets.size()) {
                if (separator == std::string_view::npos) return std::nullopt;
                text.remove_prefix(separator + 1);
            } else if (separator != std::string_view::npos) {
                return std::nullopt;
            }
        }
        return result;
    }

    [[nodiscard]] bool isMulticast() const noexcept { return octets[0] >= 224 && octets[0] <= 239; }

    [[nodiscard]] std::string toString() const {
        return std::to_string(octets[0]) + '.' + std::to_string(octets[1]) + '.'
            + std::to_string(octets[2]) + '.' + std::to_string(octets[3]);
    }
};

} // namespace lxtool::aes67
