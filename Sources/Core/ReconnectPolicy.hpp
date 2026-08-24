// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace lxtool::aes67 {

class ReconnectPolicy final {
public:
    using Duration = std::chrono::milliseconds;
    [[nodiscard]] Duration nextDelay() noexcept {
        const auto shift = std::min<std::uint32_t>(attempt_++, 5);
        return std::min(Duration{250 * (1U << shift)}, Duration{8'000});
    }
    void connected() noexcept { attempt_ = 0; }
    [[nodiscard]] std::uint32_t attempt() const noexcept { return attempt_; }
private:
    std::uint32_t attempt_{0};
};

} // namespace lxtool::aes67
