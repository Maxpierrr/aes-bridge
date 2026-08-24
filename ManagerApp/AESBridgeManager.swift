// SPDX-License-Identifier: GPL-3.0-only
import SwiftUI
import Foundation
import Darwin
import Combine

struct NetworkInterface: Identifiable, Hashable {
    let name: String
    let address: String
    var id: String { name }
}

struct DiscoveredSession: Identifiable, Hashable, Decodable, Sendable {
    let id: String
    let name: String
    let originAddress: String
    let sourceAddress: String
    let multicastAddress: String
    let port: Int
    let payloadType: Int
    let ptpDomain: Int
}

private struct EngineStatusSnapshot: Decodable, Sendable {
    let engineRunning: Bool
    let rxPackets: UInt64
    let txPackets: UInt64
    let lostPackets: UInt64
    let malformedPackets: UInt64
    let sapMalformedPackets: UInt64
    let reconnects: UInt64
    let inputUnderruns: UInt64
    let outputUnderruns: UInt64
    let ringOverruns: UInt64
    let ptpErrors: UInt64
    let ptpOffsetNanoseconds: Int64
    let ptpMeanPathDelayNanoseconds: Int64
    let rxActive: Bool
    let txActive: Bool
    let ptpLocked: Bool
    let sessions: [DiscoveredSession]
}

@MainActor final class EngineModel: ObservableObject {
    @Published var interfaces: [NetworkInterface] = []
    @Published var selectedInterface = ""
    @Published var profile = "raspberry"
    @Published var discoveredSessions: [DiscoveredSession] = []
    @Published var selectedSession = ""
    @Published var rxAddress = "239.69.83.80"
    @Published var rxSourceAddress = ""
    @Published var rxPort = 5004
    @Published var rxPayloadType = 96
    @Published var txAddress = "239.69.83.81"
    @Published var txPort = 5004
    @Published var jitterMilliseconds = 6
    @Published var engineState = "Non connecté"
    @Published var ptpState = "Non validé"
    @Published var ptpOffsetNanoseconds: Int64 = 0
    @Published var ptpPathDelayNanoseconds: Int64 = 0
    @Published var ptpErrors: UInt64 = 0
    @Published var rxPackets: UInt64 = 0
    @Published var txPackets: UInt64 = 0
    @Published var losses: UInt64 = 0
    @Published var errors: UInt64 = 0
    @Published var reconnects: UInt64 = 0
    @Published var inputUnderruns: UInt64 = 0
    @Published var outputUnderruns: UInt64 = 0
    @Published var ringOverruns: UInt64 = 0
    @Published var rxActive = false
    @Published var txActive = false
    private var engineProcess: Process?
    private var restartRequested = false
    private var statusRefreshInFlight = false

    var streamCount: Int { profile == "raspberry" ? 1 : 8 }
    var channelDescription: String { "\(streamCount * 8) × \(streamCount * 8) actifs · \(streamCount) banque(s) AES67" }

    init() { refreshInterfaces() }

    func refreshInterfaces() {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return }
        defer { freeifaddrs(head) }
        var result: [NetworkInterface] = []
        var current: UnsafeMutablePointer<ifaddrs>? = first
        while let item = current {
            let flags = Int32(item.pointee.ifa_flags)
            if let address = item.pointee.ifa_addr,
               address.pointee.sa_family == UInt8(AF_INET),
               (flags & IFF_LOOPBACK) == 0,
               (flags & IFF_UP) != 0 {
                var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                let length = socklen_t(address.pointee.sa_len)
                if getnameinfo(address, length, &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0 {
                    let entry = NetworkInterface(name: String(cString: item.pointee.ifa_name), address: String(cString: host))
                    if !result.contains(where: { $0.name == entry.name }) { result.append(entry) }
                }
            }
            current = item.pointee.ifa_next
        }
        interfaces = result.sorted { $0.name < $1.name }
        if !interfaces.contains(where: { $0.name == selectedInterface }) { selectedInterface = "" }
    }

    private func engineURL() -> URL? {
        if let bundled = Bundle.main.url(forResource: "aes-bridge-engine", withExtension: nil) { return bundled }
        let installed = URL(fileURLWithPath: "/usr/local/libexec/aes-bridge-engine")
        return FileManager.default.isExecutableFile(atPath: installed.path) ? installed : nil
    }

    private func validIPv4(_ value: String, multicast: Bool = false) -> Bool {
        let octets = value.split(separator: ".", omittingEmptySubsequences: false)
        guard octets.count == 4 else { return false }
        let values = octets.compactMap { Int($0) }
        guard values.count == 4, values.allSatisfy({ (0...255).contains($0) }) else { return false }
        return !multicast || (224...239).contains(values[0])
    }

    private func configurationError() -> String? {
        if selectedInterface.isEmpty { return "Choisissez une interface Ethernet" }
        if !validIPv4(rxAddress, multicast: true) { return "Adresse multicast RX invalide" }
        if !rxSourceAddress.isEmpty && !validIPv4(rxSourceAddress) { return "Adresse source RX invalide" }
        if !validIPv4(txAddress, multicast: true) { return "Adresse multicast TX invalide" }
        if !(1...65_535).contains(rxPort) || !(1...65_535).contains(txPort) { return "Port RTP hors plage" }
        if !(0...127).contains(rxPayloadType) { return "Payload type RX hors plage" }
        if !(2...63).contains(jitterMilliseconds) { return "Tampon anti-gigue hors plage" }
        return nil
    }

    func applyProfile() {
        rxSourceAddress = ""
        switch profile {
        case "computer-a":
            rxAddress = "239.69.83.80"
            txAddress = "239.69.83.96"
        case "computer-b":
            rxAddress = "239.69.83.96"
            txAddress = "239.69.83.80"
        default:
            rxAddress = "239.69.83.80"
            txAddress = "239.69.83.81"
        }
    }

    func start() {
        guard engineProcess?.isRunning != true else { engineState = "En fonctionnement"; return }
        engineProcess = nil
        restartRequested = false
        if let error = configurationError() { engineState = error; return }
        guard let executable = engineURL() else { engineState = "Moteur absent de l’application"; return }
        let process = Process()
        let errorOutput = Pipe()
        process.executableURL = executable
        process.arguments = ["--run", "--interface", selectedInterface, "--profile", profile,
            "--rx-group", rxAddress, "--rx-source", rxSourceAddress,
            "--rx-port", String(rxPort), "--rx-payload-type", String(rxPayloadType),
            "--tx-group", txAddress, "--tx-port", String(txPort),
            "--jitter-packets", String(jitterMilliseconds)]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = errorOutput
        process.terminationHandler = { [weak self] finished in
            let detail = String(data: errorOutput.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            DispatchQueue.main.async {
                guard let self, self.engineProcess === finished else { return }
                self.engineProcess = nil
                if self.restartRequested {
                    self.restartRequested = false
                    self.start()
                } else {
                    self.engineState = detail.isEmpty ? "Arrêté" : "Échec : \(detail)"
                }
            }
        }
        do {
            try process.run()
            engineProcess = process
            engineState = "Démarrage…"
        } catch {
            engineState = "Échec : \(error.localizedDescription)"
        }
    }

    func stop() {
        restartRequested = false
        guard let process = engineProcess, process.isRunning else { engineState = "Arrêté"; return }
        process.terminate()
        engineState = "Arrêt…"
    }

    func restart() {
        guard let process = engineProcess, process.isRunning else { start(); return }
        restartRequested = true
        process.terminate()
        engineState = "Relance…"
    }

    func applySelectedSession() {
        guard let session = discoveredSessions.first(where: { $0.id == selectedSession }) else { return }
        let parts = session.name.split(separator: "-")
        let firstChannel = parts.count >= 2 ? Int(parts[parts.count - 2]) ?? 1 : 1
        let bank = max(0, min(7, (firstChannel - 1) / 8))
        rxAddress = multicastBase(for: session.multicastAddress, bank: bank) ?? session.multicastAddress
        rxSourceAddress = session.sourceAddress
        rxPort = session.port
        rxPayloadType = session.payloadType
        if engineProcess?.isRunning == true { restart() }
    }

    private func multicastBase(for address: String, bank: Int) -> String? {
        let octets = address.split(separator: ".").compactMap { UInt8($0) }
        guard octets.count == 4, bank <= Int(octets[3]) else { return nil }
        return "\(octets[0]).\(octets[1]).\(octets[2]).\(Int(octets[3]) - bank)"
    }

    nonisolated private static func readStatus(executable: URL) -> EngineStatusSnapshot? {
        let process = Process()
        let output = Pipe()
        process.executableURL = executable
        process.arguments = ["--status"]
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            guard process.terminationStatus == 0 else { return nil }
            return try JSONDecoder().decode(EngineStatusSnapshot.self,
                from: output.fileHandleForReading.readDataToEndOfFile())
        } catch { return nil }
    }

    private func applyStatus(_ status: EngineStatusSnapshot?) {
        statusRefreshInFlight = false
        guard let status else {
            engineState = engineProcess?.isRunning == true ? "Démarrage…" : "Arrêté"
            rxActive = false
            txActive = false
            return
        }
        engineState = status.engineRunning ? "En fonctionnement" : "Arrêté"
        ptpState = status.ptpLocked ? "Verrouillé" : "Non verrouillé"
        ptpOffsetNanoseconds = status.ptpOffsetNanoseconds
        ptpPathDelayNanoseconds = status.ptpMeanPathDelayNanoseconds
        ptpErrors = status.ptpErrors
        rxPackets = status.rxPackets
        txPackets = status.txPackets
        losses = status.lostPackets
        errors = status.malformedPackets + status.sapMalformedPackets
        reconnects = status.reconnects
        inputUnderruns = status.inputUnderruns
        outputUnderruns = status.outputUnderruns
        ringOverruns = status.ringOverruns
        rxActive = status.rxActive
        txActive = status.txActive
        discoveredSessions = status.sessions
    }

    func refreshStatus() {
        guard !statusRefreshInFlight, let executable = engineURL() else { return }
        statusRefreshInFlight = true
        Task.detached(priority: .utility) {
            let status = EngineModel.readStatus(executable: executable)
            await MainActor.run { [weak self] in self?.applyStatus(status) }
        }
    }
}

struct StatusBadge: View {
    let title: String
    let value: String
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.headline)
        }.frame(maxWidth: .infinity, alignment: .leading).padding(12).background(.quaternary, in: RoundedRectangle(cornerRadius: 10))
    }
}

struct ContentView: View {
    @StateObject private var model = EngineModel()
    private let statusTimer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()
    var body: some View {
        NavigationSplitView {
            List(selection: $model.selectedSession) {
                Section("Sessions AES67 découvertes") {
                    if model.discoveredSessions.isEmpty { Text("Aucune session").foregroundStyle(.secondary) }
                    ForEach(model.discoveredSessions) { session in
                        VStack(alignment: .leading) {
                            Text(session.name)
                            Text("\(session.multicastAddress):\(session.port) · source \(session.sourceAddress)")
                                .font(.caption).foregroundStyle(.secondary)
                        }.tag(session.id)
                    }
                }
            }.navigationTitle("AES Bridge")
        } detail: {
            Form {
                Section("Réseau") {
                    Picker("Profil", selection: $model.profile) {
                        Text("Raspberry Pi · banque 1 · 8×8").tag("raspberry")
                        Text("Ordinateur A · 64×64").tag("computer-a")
                        Text("Ordinateur B · 64×64").tag("computer-b")
                    }.onChange(of: model.profile) { _ in model.applyProfile() }
                    LabeledContent("Canaux", value: model.channelDescription)
                    Picker("Interface Ethernet filaire", selection: $model.selectedInterface) {
                        Text("Choisir…").tag("")
                        ForEach(model.interfaces) { Text("\($0.name) — \($0.address)").tag($0.name) }
                    }
                    Text("Sélection explicite requise : choisissez le port Ethernet câblé, pas le Wi‑Fi.")
                        .font(.caption).foregroundStyle(.secondary)
                    Button("Actualiser les interfaces") { model.refreshInterfaces() }
                }
                Section("Réception AES67 → Mac") {
                    Text(model.selectedSession.isEmpty ? "Démarrez le moteur pour découvrir les sessions SAP" : model.selectedSession)
                    Button("Utiliser la session sélectionnée") { model.applySelectedSession() }
                        .disabled(model.selectedSession.isEmpty)
                    TextField("Multicast RX", text: $model.rxAddress)
                    TextField("Source RX", text: $model.rxSourceAddress)
                    TextField("Port RTP RX", value: $model.rxPort, format: .number)
                    TextField("Payload type RX", value: $model.rxPayloadType, format: .number)
                    Stepper("Tampon anti-gigue : \(model.jitterMilliseconds) ms", value: $model.jitterMilliseconds, in: 2...50)
                }
                Section("Émission Mac → AES67") {
                    TextField("Multicast", text: $model.txAddress)
                    TextField("Port RTP", value: $model.txPort, format: .number)
                    LabeledContent("Format", value: "8 canaux/banque · L24 · 48 kHz · 1 ms")
                }
                Section("État") {
                    HStack { StatusBadge(title: "Moteur", value: model.engineState); StatusBadge(title: "PTP domaine 0", value: model.ptpState) }
                    HStack { StatusBadge(title: "Réception", value: model.rxActive ? "Active" : "En attente"); StatusBadge(title: "Émission", value: model.txActive ? "Active" : "Arrêtée") }
                    Grid(alignment: .leading) {
                        GridRow { Text("RX"); Text("\(model.rxPackets)"); Text("TX"); Text("\(model.txPackets)") }
                        GridRow { Text("Pertes"); Text("\(model.losses)"); Text("Erreurs"); Text("\(model.errors)") }
                        GridRow { Text("Reconnexions"); Text("\(model.reconnects)"); Text("Overruns"); Text("\(model.ringOverruns)") }
                        GridRow { Text("Underruns IN"); Text("\(model.inputUnderruns)"); Text("Underruns OUT"); Text("\(model.outputUnderruns)") }
                        GridRow { Text("Offset PTP"); Text("\(model.ptpOffsetNanoseconds) ns"); Text("Délai PTP"); Text("\(model.ptpPathDelayNanoseconds) ns") }
                        GridRow { Text("Erreurs PTP"); Text("\(model.ptpErrors)"); Text(""); Text("") }
                    }
                }
                Section {
                    HStack { Button("Démarrer") { model.start() }; Button("Arrêter") { model.stop() }; Button("Relancer") { model.restart() } }
                }
                Text("Le périphérique virtuel expose 64×64. Le profil Raspberry active uniquement les canaux 1–8 ; les profils ordinateur utilisent huit banques. PTP et stabilité longue durée restent à valider sur matériel réel.")
                    .font(.caption).foregroundStyle(.secondary)
            }.formStyle(.grouped).padding().navigationTitle("Configuration 64×64")
        }.frame(minWidth: 880, minHeight: 650)
            .onReceive(statusTimer) { _ in model.refreshStatus() }
            .onDisappear { model.stop() }
    }
}

@main struct AESBridgeManagerApp: App {
    var body: some Scene { WindowGroup { ContentView() } }
}
