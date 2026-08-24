// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace lxtool::aes67 {

enum class PTPMessageType : std::uint8_t {
    sync = 0x0,
    delayRequest = 0x1,
    followUp = 0x8,
    delayResponse = 0x9,
    announce = 0xb,
};

struct PTPPortIdentity final {
    std::array<std::uint8_t, 8> clock{};
    std::uint16_t port{0};
    bool operator==(const PTPPortIdentity&) const = default;
};

struct PTPMessage final {
    PTPMessageType type{PTPMessageType::sync};
    std::uint8_t domain{0};
    bool twoStep{false};
    std::int64_t correctionScaledNanoseconds{0};
    PTPPortIdentity source;
    std::uint16_t sequence{0};
    std::optional<std::int64_t> timestampNanoseconds;
    std::optional<PTPPortIdentity> requestingPort;
};

struct PTPMeasurement final {
    std::int64_t offsetNanoseconds{0};
    std::int64_t meanPathDelayNanoseconds{0};
};

class PTPCodec final {
public:
    static constexpr std::size_t kHeaderBytes = 34;
    static constexpr std::size_t kTimestampBytes = 10;
    static bool decode(std::span<const std::uint8_t> bytes, std::uint8_t expectedDomain,
        PTPMessage& message, std::string* error = nullptr) noexcept;
    static std::array<std::uint8_t, 44> encodeDelayRequest(const PTPPortIdentity& source,
        std::uint16_t sequence, std::uint8_t domain, std::int64_t originNanoseconds) noexcept;
    static std::array<std::uint8_t, 44> encodeTimestampMessage(PTPMessageType type,
        const PTPPortIdentity& source, std::uint16_t sequence, std::uint8_t domain,
        std::int64_t timestampNanoseconds, bool twoStep = false,
        std::int64_t correctionScaledNanoseconds = 0) noexcept;
    static std::array<std::uint8_t, 54> encodeDelayResponse(const PTPPortIdentity& source,
        const PTPPortIdentity& requestingPort, std::uint16_t sequence, std::uint8_t domain,
        std::int64_t receiptNanoseconds, std::int64_t correctionScaledNanoseconds = 0) noexcept;
    static std::array<std::uint8_t, 64> encodeAnnounce(const PTPPortIdentity& source,
        std::uint16_t sequence, std::uint8_t domain) noexcept;
    static std::optional<PTPMeasurement> calculateE2E(std::int64_t syncOriginNanoseconds,
        std::int64_t syncIngressNanoseconds, std::int64_t delayRequestEgressNanoseconds,
        std::int64_t delayRequestReceiptNanoseconds, std::int64_t syncCorrectionScaledNanoseconds = 0,
        std::int64_t delayCorrectionScaledNanoseconds = 0) noexcept;
};

} // namespace lxtool::aes67
