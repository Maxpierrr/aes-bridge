// SPDX-License-Identifier: GPL-3.0-only
#include "Core/PTP.hpp"

#include <bit>
#include <climits>
#include <limits>

namespace lxtool::aes67 {
namespace {
std::uint16_t get16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
}

std::uint32_t get32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U)
        | (static_cast<std::uint32_t>(data[2]) << 8U) | data[3];
}

std::uint64_t get64(const std::uint8_t* data) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) value = (value << 8U) | data[index];
    return value;
}

void put16(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value >> 8U);
    data[1] = static_cast<std::uint8_t>(value);
}

void put32(std::uint8_t* data, std::uint32_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value >> 24U);
    data[1] = static_cast<std::uint8_t>(value >> 16U);
    data[2] = static_cast<std::uint8_t>(value >> 8U);
    data[3] = static_cast<std::uint8_t>(value);
}

void put64(std::uint8_t* data, std::uint64_t value) noexcept {
    for (int index = 7; index >= 0; --index) {
        data[index] = static_cast<std::uint8_t>(value);
        value >>= 8U;
    }
}

void putTimestamp(std::uint8_t* data, std::int64_t nanoseconds) noexcept {
    if (nanoseconds < 0) nanoseconds = 0;
    const auto seconds = static_cast<std::uint64_t>(nanoseconds / 1'000'000'000LL);
    put16(data, static_cast<std::uint16_t>(seconds >> 32U));
    put32(data + 2, static_cast<std::uint32_t>(seconds));
    put32(data + 6, static_cast<std::uint32_t>(nanoseconds % 1'000'000'000LL));
}

std::optional<std::int64_t> getTimestamp(const std::uint8_t* data) noexcept {
    const auto seconds = (static_cast<std::uint64_t>(get16(data)) << 32U) | get32(data + 2);
    const auto nanos = get32(data + 6);
    if (nanos >= 1'000'000'000U || seconds > static_cast<std::uint64_t>(INT64_MAX / 1'000'000'000LL)) return std::nullopt;
    return static_cast<std::int64_t>(seconds) * 1'000'000'000LL + nanos;
}

PTPPortIdentity getPortIdentity(const std::uint8_t* data) noexcept {
    PTPPortIdentity result;
    for (std::size_t index = 0; index < result.clock.size(); ++index) result.clock[index] = data[index];
    result.port = get16(data + 8);
    return result;
}

std::int64_t scaledToNanoseconds(std::int64_t scaled) noexcept { return scaled / 65'536LL; }

void putHeader(std::uint8_t* data, std::size_t size, PTPMessageType type, const PTPPortIdentity& source,
    std::uint16_t sequence, std::uint8_t domain, bool twoStep, std::int64_t correctionScaledNanoseconds) noexcept {
    data[0] = static_cast<std::uint8_t>(type);
    data[1] = 0x02;
    put16(data + 2, static_cast<std::uint16_t>(size));
    data[4] = domain;
    if (twoStep) data[6] = 0x02;
    put64(data + 8, std::bit_cast<std::uint64_t>(correctionScaledNanoseconds));
    for (std::size_t index = 0; index < source.clock.size(); ++index) data[20 + index] = source.clock[index];
    put16(data + 28, source.port);
    put16(data + 30, sequence);
    data[32] = type == PTPMessageType::sync ? 0 : type == PTPMessageType::delayRequest ? 1
        : type == PTPMessageType::followUp ? 2 : type == PTPMessageType::delayResponse ? 3 : 5;
    data[33] = 0x7f;
}
}

bool PTPCodec::decode(std::span<const std::uint8_t> bytes, std::uint8_t expectedDomain,
    PTPMessage& message, std::string* error) noexcept {
    if (bytes.size() < kHeaderBytes || (bytes[1] & 0x0fU) != 2U) {
        if (error) *error = "en-tête ou version PTP invalide";
        return false;
    }
    const auto length = get16(bytes.data() + 2);
    if (length < kHeaderBytes || length > bytes.size() || bytes[4] != expectedDomain) {
        if (error) *error = "taille ou domaine PTP invalide";
        return false;
    }
    const auto rawType = static_cast<std::uint8_t>(bytes[0] & 0x0fU);
    if (rawType != static_cast<std::uint8_t>(PTPMessageType::sync)
        && rawType != static_cast<std::uint8_t>(PTPMessageType::delayRequest)
        && rawType != static_cast<std::uint8_t>(PTPMessageType::followUp)
        && rawType != static_cast<std::uint8_t>(PTPMessageType::delayResponse)
        && rawType != static_cast<std::uint8_t>(PTPMessageType::announce)) return false;
    message = {};
    message.type = static_cast<PTPMessageType>(rawType);
    message.domain = bytes[4];
    message.twoStep = (bytes[6] & 0x02U) != 0U;
    message.correctionScaledNanoseconds = std::bit_cast<std::int64_t>(get64(bytes.data() + 8));
    message.source = getPortIdentity(bytes.data() + 20);
    message.sequence = get16(bytes.data() + 30);
    if (message.type == PTPMessageType::sync || message.type == PTPMessageType::delayRequest
        || message.type == PTPMessageType::followUp || message.type == PTPMessageType::delayResponse) {
        if (length < kHeaderBytes + kTimestampBytes) return false;
        message.timestampNanoseconds = getTimestamp(bytes.data() + kHeaderBytes);
        if (!message.timestampNanoseconds) return false;
    }
    if (message.type == PTPMessageType::delayResponse) {
        if (length < 54) return false;
        message.requestingPort = getPortIdentity(bytes.data() + 44);
    }
    if (message.type == PTPMessageType::announce && length < 64) return false;
    return true;
}

std::array<std::uint8_t, 44> PTPCodec::encodeDelayRequest(const PTPPortIdentity& source,
    std::uint16_t sequence, std::uint8_t domain, std::int64_t originNanoseconds) noexcept {
    return encodeTimestampMessage(PTPMessageType::delayRequest, source, sequence, domain, originNanoseconds);
}

std::array<std::uint8_t, 44> PTPCodec::encodeTimestampMessage(PTPMessageType type,
    const PTPPortIdentity& source, std::uint16_t sequence, std::uint8_t domain,
    std::int64_t timestampNanoseconds, bool twoStep, std::int64_t correctionScaledNanoseconds) noexcept {
    std::array<std::uint8_t, 44> bytes{};
    putHeader(bytes.data(), bytes.size(), type, source, sequence, domain, twoStep, correctionScaledNanoseconds);
    putTimestamp(bytes.data() + kHeaderBytes, timestampNanoseconds);
    return bytes;
}

std::array<std::uint8_t, 54> PTPCodec::encodeDelayResponse(const PTPPortIdentity& source,
    const PTPPortIdentity& requestingPort, std::uint16_t sequence, std::uint8_t domain,
    std::int64_t receiptNanoseconds, std::int64_t correctionScaledNanoseconds) noexcept {
    std::array<std::uint8_t, 54> bytes{};
    putHeader(bytes.data(), bytes.size(), PTPMessageType::delayResponse, source, sequence, domain, false, correctionScaledNanoseconds);
    putTimestamp(bytes.data() + kHeaderBytes, receiptNanoseconds);
    for (std::size_t index = 0; index < requestingPort.clock.size(); ++index) bytes[44 + index] = requestingPort.clock[index];
    put16(bytes.data() + 52, requestingPort.port);
    return bytes;
}

std::array<std::uint8_t, 64> PTPCodec::encodeAnnounce(const PTPPortIdentity& source,
    std::uint16_t sequence, std::uint8_t domain) noexcept {
    std::array<std::uint8_t, 64> bytes{};
    putHeader(bytes.data(), bytes.size(), PTPMessageType::announce, source, sequence, domain, false, 0);
    bytes[47] = 128;
    bytes[48] = 248;
    bytes[49] = 0xfe;
    bytes[52] = 128;
    for (std::size_t index = 0; index < source.clock.size(); ++index) bytes[53 + index] = source.clock[index];
    bytes[63] = 0xa0;
    return bytes;
}

std::optional<PTPMeasurement> PTPCodec::calculateE2E(std::int64_t syncOriginNanoseconds,
    std::int64_t syncIngressNanoseconds, std::int64_t delayRequestEgressNanoseconds,
    std::int64_t delayRequestReceiptNanoseconds, std::int64_t syncCorrectionScaledNanoseconds,
    std::int64_t delayCorrectionScaledNanoseconds) noexcept {
    const auto down = syncIngressNanoseconds - syncOriginNanoseconds - scaledToNanoseconds(syncCorrectionScaledNanoseconds);
    const auto up = delayRequestReceiptNanoseconds - delayRequestEgressNanoseconds - scaledToNanoseconds(delayCorrectionScaledNanoseconds);
    const auto delay = (down + up) / 2;
    const auto offset = (down - up) / 2;
    if (delay < 0) return std::nullopt;
    return PTPMeasurement{offset, delay};
}

} // namespace lxtool::aes67
