// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/NetworkInterfaces.hpp"
#include "Core/SDP.hpp"
#include "Core/SessionDirectory.hpp"
#include "Core/SharedAudioMemory.hpp"
#include "Engine/LiveEngine.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef interface
#undef interface
#endif
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace {
std::atomic<bool> gStopRequested{false};

void requestStop(int) {
    gStopRequested.store(true, std::memory_order_relaxed);
}

bool processIsAlive(std::uint32_t processId) noexcept {
    if (processId == 0) return true;
#if defined(_WIN32)
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
#else
    return ::kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#endif
}

void listInterfaces() {
    for (const auto& interface : lxtool::aes67::listIPv4NetworkInterfaces()) {
        if (!interface.loopback) std::cout << interface.name << '\t' << interface.address << '\n';
    }
}

std::optional<std::string> valueAfter(int argc, char** argv, const std::string& option) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == option) return argv[i + 1];
    }
    return std::nullopt;
}

bool hasOption(int argc, char** argv, const std::string& option) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == option) return true;
    }
    return false;
}

template <typename Integer>
Integer parseUnsigned(const std::string& value, Integer maximum) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != value.size() || parsed > static_cast<unsigned long long>(maximum)) throw std::out_of_range("hors plage");
    return static_cast<Integer>(parsed);
}

std::string jsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += static_cast<unsigned char>(character) < 0x20U ? '?' : character; break;
        }
    }
    return result;
}

void printStatus(const lxtool::aes67::SharedAudioBlock& block) {
    std::cout << "RX " << block.statistics.rxPackets.load(std::memory_order_relaxed)
              << "  TX " << block.statistics.txPackets.load(std::memory_order_relaxed)
              << "  pertes " << block.statistics.packetsLost.load(std::memory_order_relaxed)
              << "  erreurs " << block.statistics.malformedPackets.load(std::memory_order_relaxed)
                    + block.statistics.sapMalformedPackets.load(std::memory_order_relaxed)
              << "  reconnexions " << block.statistics.reconnects.load(std::memory_order_relaxed)
              << "  PTP " << (block.ptpLocked.load(std::memory_order_relaxed) ? "VERROUILLÉ" : "NON VERROUILLÉ")
              << '\n';
}

void printStatusJson(const lxtool::aes67::SharedAudioBlock& block) {
    std::cout << "{\"engineRunning\":" << (block.engineRunning.load(std::memory_order_relaxed) ? "true" : "false")
              << ",\"virtualChannels\":" << block.channels
              << ",\"activeStreamCount\":" << block.activeStreamCount.load(std::memory_order_relaxed)
              << ",\"rxPackets\":" << block.statistics.rxPackets.load(std::memory_order_relaxed)
              << ",\"txPackets\":" << block.statistics.txPackets.load(std::memory_order_relaxed)
              << ",\"lostPackets\":" << block.statistics.packetsLost.load(std::memory_order_relaxed)
              << ",\"malformedPackets\":" << block.statistics.malformedPackets.load(std::memory_order_relaxed)
              << ",\"sapMalformedPackets\":" << block.statistics.sapMalformedPackets.load(std::memory_order_relaxed)
              << ",\"reconnects\":" << block.statistics.reconnects.load(std::memory_order_relaxed)
              << ",\"inputUnderruns\":" << block.statistics.inputUnderruns.load(std::memory_order_relaxed)
              << ",\"outputUnderruns\":" << block.statistics.outputUnderruns.load(std::memory_order_relaxed)
              << ",\"ringOverruns\":" << block.statistics.ringOverruns.load(std::memory_order_relaxed)
              << ",\"ptpMessages\":" << block.statistics.ptpMessages.load(std::memory_order_relaxed)
              << ",\"ptpErrors\":" << block.statistics.ptpErrors.load(std::memory_order_relaxed)
              << ",\"ptpOffsetNanoseconds\":" << block.statistics.ptpOffsetNanoseconds.load(std::memory_order_relaxed)
              << ",\"ptpMeanPathDelayNanoseconds\":" << block.statistics.ptpMeanPathDelayNanoseconds.load(std::memory_order_relaxed)
              << ",\"rxActive\":" << (block.rxActive.load(std::memory_order_relaxed) ? "true" : "false")
              << ",\"txActive\":" << (block.txActive.load(std::memory_order_relaxed) ? "true" : "false")
              << ",\"ptpLocked\":" << (block.ptpLocked.load(std::memory_order_relaxed) ? "true" : "false")
              << ",\"sessions\":[";
    const auto sessions = lxtool::aes67::SessionDirectory::snapshots(block);
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        const auto& session = sessions[i];
        if (i != 0) std::cout << ',';
        std::cout << "{\"id\":\"" << session.messageHash << '-' << jsonEscape(session.originAddress)
                  << "\",\"name\":\"" << jsonEscape(session.name)
                  << "\",\"originAddress\":\"" << jsonEscape(session.originAddress)
                  << "\",\"sourceAddress\":\"" << jsonEscape(session.sourceAddress)
                  << "\",\"multicastAddress\":\"" << jsonEscape(session.multicastAddress)
                  << "\",\"port\":" << session.port
                  << ",\"channels\":" << session.channels
                  << ",\"sampleRate\":" << session.sampleRate
                  << ",\"framesPerPacket\":" << session.framesPerPacket
                  << ",\"payloadType\":" << static_cast<unsigned>(session.payloadType)
                  << ",\"ptpDomain\":" << static_cast<unsigned>(session.ptpDomain)
                  << ",\"lastSeenUnixMilliseconds\":" << session.lastSeenUnixMilliseconds << '}';
    }
    std::cout << "]}\n";
}

void usage() {
    std::cout << "AES Bridge engine 0.2 (prototype à valider sur matériel)\n"
              << "  --list-interfaces\n"
              << "  --print-tx-sdp <adresse-ip-interface>\n"
              << "  --status\n"
              << "  --run --interface <nom> [--interface-address <IPv4>]\n"
              << "        [--profile raspberry|computer-a|computer-b]\n"
              << "        [--rx-group <IPv4>] [--rx-source <IPv4>] [--tx-group <IPv4>]\n"
              << "        [--rx-port <port>] [--tx-port <port>]\n"
              << "        [--stream-count <1..8>] [--port-stride <0..65535>]\n"
              << "        [--rx-payload-type <0..127>] [--tx-payload-type <0..127>]\n"
              << "        [--jitter-packets <2..63>] [--duration <secondes>] [--no-sap] [--no-ptp]\n"
              << "        [--parent-pid <pid-app-controle>]\n";
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--list-interfaces") {
        listInterfaces();
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--print-tx-sdp") {
        lxtool::aes67::SessionDescription session;
        session.name = std::string(lxtool::aes67::kMacSessionName);
        session.originAddress = argv[2];
        session.sourceAddress = argv[2];
        session.multicastAddress = "239.69.83.81";
        std::cout << lxtool::aes67::SDP::generate(session);
        return 0;
    }
    if (hasOption(argc, argv, "--status")) {
        lxtool::aes67::SharedAudioMemory shared;
        if (!shared.open(false) || !lxtool::aes67::SharedAudioMemory::ownerActive()
            || !shared.get()->engineRunning.load(std::memory_order_acquire)) {
            std::cerr << "AES Bridge n'est pas démarré.\n";
            return 2;
        }
        printStatusJson(*shared.get());
        return 0;
    }
    if (!hasOption(argc, argv, "--run")) {
        usage();
        return argc == 1 ? 0 : 2;
    }

    lxtool::aes67::LiveEngineConfig config;
    config.interfaceName = valueAfter(argc, argv, "--interface").value_or("");
    config.interfaceAddress = valueAfter(argc, argv, "--interface-address").value_or("");
    const auto profile = valueAfter(argc, argv, "--profile").value_or("raspberry");
    if (profile == "computer-a") {
        config.streamCount = lxtool::aes67::kStreamBankCount;
        config.rxAddress = "239.69.83.80";
        config.txAddress = "239.69.83.96";
    } else if (profile == "computer-b") {
        config.streamCount = lxtool::aes67::kStreamBankCount;
        config.rxAddress = "239.69.83.96";
        config.txAddress = "239.69.83.80";
    } else if (profile != "raspberry") {
        std::cerr << "Profil inconnu: " << profile << "\n";
        return 2;
    }
    config.rxAddress = valueAfter(argc, argv, "--rx-group").value_or(config.rxAddress);
    config.rxSourceAddress = valueAfter(argc, argv, "--rx-source").value_or("");
    config.txAddress = valueAfter(argc, argv, "--tx-group").value_or(config.txAddress);
    config.enableSAPPublication = !hasOption(argc, argv, "--no-sap") && !hasOption(argc, argv, "--no-sap-publish");
    config.enableSAPDiscovery = !hasOption(argc, argv, "--no-sap") && !hasOption(argc, argv, "--no-sap-discovery");
    config.enablePTP = !hasOption(argc, argv, "--no-ptp");

    std::uint32_t parentProcessId = 0;
    try {
        if (const auto value = valueAfter(argc, argv, "--rx-port")) config.rxPort = parseUnsigned<std::uint16_t>(*value, 65'535);
        if (const auto value = valueAfter(argc, argv, "--tx-port")) config.txPort = parseUnsigned<std::uint16_t>(*value, 65'535);
        if (const auto value = valueAfter(argc, argv, "--stream-count")) config.streamCount = parseUnsigned<std::size_t>(*value, lxtool::aes67::kStreamBankCount);
        if (const auto value = valueAfter(argc, argv, "--port-stride")) config.portStride = parseUnsigned<std::uint16_t>(*value, 65'535);
        if (const auto value = valueAfter(argc, argv, "--jitter-packets")) config.jitterPackets = parseUnsigned<std::size_t>(*value, 63);
        if (const auto value = valueAfter(argc, argv, "--rx-payload-type")) config.rxPayloadType = parseUnsigned<std::uint8_t>(*value, 127);
        if (const auto value = valueAfter(argc, argv, "--tx-payload-type")) config.txPayloadType = parseUnsigned<std::uint8_t>(*value, 127);
        if (const auto parent = valueAfter(argc, argv, "--parent-pid")) {
            parentProcessId = parseUnsigned<std::uint32_t>(*parent, std::numeric_limits<std::uint32_t>::max());
            if (parentProcessId == 0) throw std::out_of_range("PID parent nul");
        }
        if (config.jitterPackets < 2) throw std::out_of_range("tampon anti-gigue inférieur à 2");
        if (config.streamCount < 1) throw std::out_of_range("nombre de banques inférieur à 1");
    } catch (const std::exception& error) {
        std::cerr << "Paramètre numérique invalide: " << error.what() << '\n';
        return 2;
    }

    if (config.interfaceName.empty() && config.interfaceAddress.empty()) {
        std::cerr << "Une interface Ethernet explicite est obligatoire. Utilisez --list-interfaces.\n";
        return 2;
    }

    lxtool::aes67::LiveEngine engine(config);
    if (!engine.start()) {
        std::cerr << "Démarrage impossible: vérifiez l'interface et la mémoire partagée.\n";
        return 1;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    try {
        if (const auto duration = valueAfter(argc, argv, "--duration")) {
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(parseUnsigned<std::uint32_t>(*duration, 86'400));
        }
    } catch (const std::exception& error) {
        std::cerr << "Durée invalide: " << error.what() << '\n';
        engine.stop();
        return 2;
    }

    std::cout << "AES Bridge démarré sur " << engine.interfaceAddress()
              << " — " << config.streamCount << " banque(s), " << config.streamCount * lxtool::aes67::kAES67ChannelsPerStream << " canaux"
              << " — RX " << config.rxAddress << ':' << config.rxPort
              << " — TX " << config.txAddress << ':' << config.txPort << '\n';
    while (!gStopRequested.load(std::memory_order_relaxed)
        && std::chrono::steady_clock::now() < deadline
        && processIsAlive(parentProcessId)) {
        if (const auto* block = engine.sharedBlock()) printStatus(*block);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    engine.stop();
    return 0;
}
