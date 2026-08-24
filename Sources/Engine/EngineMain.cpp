// SPDX-License-Identifier: GPL-3.0-only
#include "Core/Constants.hpp"
#include "Core/SDP.hpp"
#include "Core/SharedAudioMemory.hpp"
#include "Engine/LiveEngine.hpp"

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {
std::atomic<bool> gStopRequested{false};

void requestStop(int) {
    gStopRequested.store(true, std::memory_order_relaxed);
}

void listInterfaces() {
    ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0) return;
    for (auto* item = head; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || (item->ifa_flags & IFF_LOOPBACK) != 0) continue;
        char address[INET_ADDRSTRLEN]{};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) std::cout << item->ifa_name << "\t" << address << "\n";
    }
    freeifaddrs(head);
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

void printStatus(const lxtool::aes67::SharedAudioBlock& block) {
    std::cout << "RX " << block.statistics.rxPackets.load(std::memory_order_relaxed)
              << "  TX " << block.statistics.txPackets.load(std::memory_order_relaxed)
              << "  pertes " << block.statistics.packetsLost.load(std::memory_order_relaxed)
              << "  erreurs " << block.statistics.malformedPackets.load(std::memory_order_relaxed)
              << "  reconnexions " << block.statistics.reconnects.load(std::memory_order_relaxed)
              << "  PTP " << (block.ptpLocked.load(std::memory_order_relaxed) ? "VERROUILLÉ" : "NON VERROUILLÉ")
              << '\n';
}

void printStatusJson(const lxtool::aes67::SharedAudioBlock& block) {
    std::cout << "{\"engineRunning\":" << (block.engineRunning.load(std::memory_order_relaxed) ? "true" : "false")
              << ",\"rxPackets\":" << block.statistics.rxPackets.load(std::memory_order_relaxed)
              << ",\"txPackets\":" << block.statistics.txPackets.load(std::memory_order_relaxed)
              << ",\"lostPackets\":" << block.statistics.packetsLost.load(std::memory_order_relaxed)
              << ",\"malformedPackets\":" << block.statistics.malformedPackets.load(std::memory_order_relaxed)
              << ",\"reconnects\":" << block.statistics.reconnects.load(std::memory_order_relaxed)
              << ",\"ptpLocked\":" << (block.ptpLocked.load(std::memory_order_relaxed) ? "true" : "false")
              << "}\n";
}

void usage() {
    std::cout << "AES Bridge engine 0.2 (prototype à valider sur matériel)\n"
              << "  --list-interfaces\n"
              << "  --print-tx-sdp <adresse-ip-interface>\n"
              << "  --status\n"
              << "  --run --interface <nom> [--interface-address <IPv4>]\n"
              << "        [--rx-group <IPv4>] [--tx-group <IPv4>]\n"
              << "        [--rx-port <port>] [--tx-port <port>]\n"
              << "        [--jitter-packets <paquets>] [--duration <secondes>] [--no-sap]\n";
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
        if (!shared.open(false)) {
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
    config.rxAddress = valueAfter(argc, argv, "--rx-group").value_or(config.rxAddress);
    config.txAddress = valueAfter(argc, argv, "--tx-group").value_or(config.txAddress);
    config.enableSAP = !hasOption(argc, argv, "--no-sap");

    try {
        if (const auto value = valueAfter(argc, argv, "--rx-port")) config.rxPort = static_cast<std::uint16_t>(std::stoul(*value));
        if (const auto value = valueAfter(argc, argv, "--tx-port")) config.txPort = static_cast<std::uint16_t>(std::stoul(*value));
        if (const auto value = valueAfter(argc, argv, "--jitter-packets")) config.jitterPackets = static_cast<std::size_t>(std::stoul(*value));
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
    const auto duration = valueAfter(argc, argv, "--duration");
    const auto deadline = duration
        ? std::chrono::steady_clock::now() + std::chrono::seconds(std::stoul(*duration))
        : std::chrono::steady_clock::time_point::max();

    std::cout << "AES Bridge démarré sur " << engine.interfaceAddress()
              << " — RX " << config.rxAddress << ':' << config.rxPort
              << " — TX " << config.txAddress << ':' << config.txPort << '\n';
    while (!gStopRequested.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline) {
        if (const auto* block = engine.sharedBlock()) printStatus(*block);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    engine.stop();
    return 0;
}
