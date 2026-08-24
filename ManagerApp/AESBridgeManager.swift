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

struct DiscoveredSession: Identifiable, Hashable {
    let id: String
    let name: String
    let originAddress: String
    let sourceAddress: String
    let multicastAddress: String
    let port: Int
    let payloadType: Int
    let ptpDomain: Int
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
    @Published var rxActive = false
    @Published var txActive = false
    private var engineProcess: Process?

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
        if selectedInterface.isEmpty { selectedInterface = interfaces.first(where: { $0.name.hasPrefix("en") })?.name ?? interfaces.first?.name ?? "" }
    }

    private func engineURL() -> URL? {
        if let bundled = Bundle.main.url(forResource: "aes-bridge-engine", withExtension: nil) { return bundled }
        let installed = URL(fileURLWithPath: "/usr/local/libexec/aes-bridge-engine")
        return FileManager.default.isExecutableFile(atPath: installed.path) ? installed : nil
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
        guard !selectedInterface.isEmpty else { engineState = "Choisissez une interface Ethernet"; return }
        guard let executable = engineURL() else { engineState = "Moteur absent de l’application"; return }
        let process = Process()
        process.executableURL = executable
        process.arguments = ["--run", "--interface", selectedInterface, "--profile", profile,
            "--rx-group", rxAddress, "--rx-source", rxSourceAddress,
            "--rx-port", String(rxPort), "--rx-payload-type", String(rxPayloadType),
            "--tx-group", txAddress, "--tx-port", String(txPort),
            "--jitter-packets", String(jitterMilliseconds)]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        process.terminationHandler = { [weak self] _ in
            DispatchQueue.main.async { self?.engineState = "Arrêté" }
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
        guard let process = engineProcess, process.isRunning else { engineState = "Arrêté"; return }
        process.terminate()
        engineState = "Arrêt…"
    }

    func restart() {
        stop()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in self?.start() }
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

    func refreshStatus() {
        guard let executable = engineURL() else { return }
        let process = Process()
        let output = Pipe()
        process.executableURL = executable
        process.arguments = ["--status"]
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            guard process.terminationStatus == 0,
                  let object = try JSONSerialization.jsonObject(with: output.fileHandleForReading.readDataToEndOfFile()) as? [String: Any]
            else { engineState = engineProcess?.isRunning == true ? "Démarrage…" : "Arrêté"; return }
            engineState = (object["engineRunning"] as? Bool) == true ? "En fonctionnement" : "Arrêté"
            ptpState = (object["ptpLocked"] as? Bool) == true ? "Verrouillé" : "Non verrouillé"
            ptpOffsetNanoseconds = (object["ptpOffsetNanoseconds"] as? NSNumber)?.int64Value ?? 0
            ptpPathDelayNanoseconds = (object["ptpMeanPathDelayNanoseconds"] as? NSNumber)?.int64Value ?? 0
            ptpErrors = (object["ptpErrors"] as? NSNumber)?.uint64Value ?? 0
            rxPackets = (object["rxPackets"] as? NSNumber)?.uint64Value ?? 0
            txPackets = (object["txPackets"] as? NSNumber)?.uint64Value ?? 0
            losses = (object["lostPackets"] as? NSNumber)?.uint64Value ?? 0
            errors = ((object["malformedPackets"] as? NSNumber)?.uint64Value ?? 0)
                + ((object["sapMalformedPackets"] as? NSNumber)?.uint64Value ?? 0)
            rxActive = (object["rxActive"] as? Bool) == true
            txActive = (object["txActive"] as? Bool) == true
            if let sessions = object["sessions"] as? [[String: Any]] {
                discoveredSessions = sessions.compactMap { value in
                    guard let id = value["id"] as? String,
                          let name = value["name"] as? String,
                          let origin = value["originAddress"] as? String,
                          let source = value["sourceAddress"] as? String,
                          let multicast = value["multicastAddress"] as? String,
                          let port = (value["port"] as? NSNumber)?.intValue,
                          let payloadType = (value["payloadType"] as? NSNumber)?.intValue,
                          let ptpDomain = (value["ptpDomain"] as? NSNumber)?.intValue
                    else { return nil }
                    return DiscoveredSession(id: id, name: name, originAddress: origin,
                        sourceAddress: source, multicastAddress: multicast, port: port,
                        payloadType: payloadType, ptpDomain: ptpDomain)
                }
            }
        } catch {
            engineState = engineProcess?.isRunning == true ? "Démarrage…" : "Arrêté"
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
                    Picker("Interface Ethernet", selection: $model.selectedInterface) {
                        ForEach(model.interfaces) { Text("\($0.name) — \($0.address)").tag($0.name) }
                    }
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
        }.frame(minWidth: 880, minHeight: 610).onReceive(statusTimer) { _ in model.refreshStatus() }
    }
}

@main struct AESBridgeManagerApp: App {
    var body: some Scene { WindowGroup { ContentView() } }
}
