// SPDX-License-Identifier: GPL-3.0-only
#include "Core/SDP.hpp"
#include "Core/IPv4Address.hpp"

#include <charconv>
#include <sstream>

namespace lxtool::aes67 {
namespace {
std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> parts;
    while (true) {
        const auto pos = value.find(delimiter);
        parts.push_back(value.substr(0, pos));
        if (pos == std::string_view::npos) break;
        value.remove_prefix(pos + 1);
    }
    return parts;
}
template <typename T> bool parseNumber(std::string_view text, T& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool validIPv4(std::string_view value, bool requireMulticast = false) {
    const auto address = IPv4Address::parse(value);
    return address.has_value() && (!requireMulticast || address->isMulticast());
}
}

std::optional<SessionDescription> SDP::parse(std::string_view text, std::string* error) {
    SessionDescription session;
    bool sawMedia = false;
    bool sawRtpMap = false;
    for (const auto rawLine : split(text, '\n')) {
        auto line = rawLine;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 2 || line[1] != '=') continue;
        const auto value = line.substr(2);
        if (line[0] == 's') session.name = value;
        else if (line[0] == 'o') {
            const auto parts = split(value, ' ');
            if (parts.size() >= 6) session.originAddress = parts[5];
        } else if (line[0] == 'c') {
            const auto parts = split(value, ' ');
            if (parts.size() >= 3) session.multicastAddress = parts[2].substr(0, parts[2].find('/'));
        } else if (line[0] == 'm') {
            const auto parts = split(value, ' ');
            unsigned port = 0, payload = 0;
            if (parts.size() < 4 || parts[0] != "audio" || parts[2] != "RTP/AVP"
                || !parseNumber(parts[1], port) || port > 65535 || !parseNumber(parts[3], payload) || payload > 127) {
                if (error) *error = "ligne m= audio/RTP invalide";
                return std::nullopt;
            }
            session.port = static_cast<std::uint16_t>(port);
            session.payloadType = static_cast<std::uint8_t>(payload);
            sawMedia = true;
        } else if (line.starts_with("a=rtpmap:")) {
            const auto body = line.substr(9);
            const auto fields = split(body, ' ');
            if (fields.size() != 2) return std::nullopt;
            unsigned payload = 0, rate = 0, channels = 1;
            const auto format = split(fields[1], '/');
            if (format.size() < 2 || !parseNumber(fields[0], payload) || !parseNumber(format[1], rate)
                || (format.size() >= 3 && !parseNumber(format[2], channels))) return std::nullopt;
            if (payload != session.payloadType || payload > 127 || channels > UINT16_MAX) return std::nullopt;
            session.encoding = format[0];
            session.sampleRate = static_cast<std::uint32_t>(rate);
            session.channels = static_cast<std::uint16_t>(channels);
            sawRtpMap = true;
        } else if (line.starts_with("a=ptime:")) {
            unsigned ptime = 0;
            if (!parseNumber(line.substr(8), ptime)) return std::nullopt;
            session.packetTimeMilliseconds = ptime;
        } else if (line.starts_with("a=framecount:")) {
            unsigned count = 0;
            if (!parseNumber(line.substr(13), count)) return std::nullopt;
            session.framesPerPacket = count;
        } else if (line.starts_with("a=source-filter:")) {
            auto body = line.substr(16);
            while (!body.empty() && body.front() == ' ') body.remove_prefix(1);
            const auto parts = split(body, ' ');
            if (parts.size() >= 5) session.sourceAddress = parts[4];
        } else if (line.starts_with("a=ts-refclk:")) {
            const auto marker = line.find("domain-nmbr=");
            if (marker != std::string_view::npos) {
                unsigned domain = 0;
                if (!parseNumber(line.substr(marker + 12), domain) || domain > 127) return std::nullopt;
                session.ptpDomain = static_cast<std::uint8_t>(domain);
            }
        }
    }
    if (!sawMedia || !sawRtpMap || session.name.empty() || !validIPv4(session.multicastAddress, true)) {
        if (error) *error = "SDP incomplet ou adresse multicast invalide";
        return std::nullopt;
    }
    return session;
}

std::string SDP::generate(const SessionDescription& s) {
    std::ostringstream out;
    out << "v=0\r\n"
        << "o=- 1 1 IN IP4 " << s.originAddress << "\r\n"
        << "s=" << s.name << "\r\n"
        << "c=IN IP4 " << s.multicastAddress << "/32\r\n"
        << "t=0 0\r\n"
        << "m=audio " << s.port << " RTP/AVP " << static_cast<unsigned>(s.payloadType) << "\r\n"
        << "a=rtpmap:" << static_cast<unsigned>(s.payloadType) << " " << s.encoding << "/" << s.sampleRate << "/" << s.channels << "\r\n"
        << "a=ptime:" << s.packetTimeMilliseconds << "\r\n"
        << "a=framecount:" << s.framesPerPacket << "\r\n"
        << "a=ts-refclk:ptp=IEEE1588-2008:traceable:domain-nmbr=" << static_cast<unsigned>(s.ptpDomain) << "\r\n"
        << "a=mediaclk:direct=0\r\n";
    if (!s.sourceAddress.empty()) {
        out << "a=source-filter: incl IN IP4 " << s.multicastAddress << " " << s.sourceAddress << "\r\n";
    }
    out << "a=sendonly\r\n";
    return out.str();
}

std::vector<std::string> SDP::validateLXToolProfile(const SessionDescription& s) {
    std::vector<std::string> errors;
    if (s.encoding != "L24") errors.emplace_back("encodage différent de L24");
    if (s.sampleRate != kSampleRate) errors.emplace_back("fréquence différente de 48000 Hz");
    if (s.channels != kChannels) errors.emplace_back("nombre de canaux différent de 8");
    if (s.packetTimeMilliseconds != 1 || s.framesPerPacket != kFramesPerPacket) errors.emplace_back("paquet différent de 1 ms / 48 trames");
    if (s.ptpDomain != kPTPDomain) errors.emplace_back("domaine PTP différent de 0");
    if (!validIPv4(s.multicastAddress, true)) errors.emplace_back("adresse RTP non multicast");
    return errors;
}

} // namespace lxtool::aes67
