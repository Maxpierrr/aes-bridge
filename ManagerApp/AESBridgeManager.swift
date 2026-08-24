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

@MainActor final class EngineModel: ObservableObject {
    @Published var interfaces: [NetworkInterface] = []
    @Published var selectedInterface = ""
    @Published var discoveredSessions: [String] = []
    @Published var selectedSession = ""
    @Published var rxAddress = "239.69.83.80"
    @Published var rxPort = 5004
    @Published var txAddress = "239.69.83.81"
    @Published var txPort = 5004
    @Published var jitterMilliseconds = 6
    @Published var engineState = "Non connecté"
    @Published var ptpState = "Non validé"
    @Published var rxPackets: UInt64 = 0
    @Published var txPackets: UInt64 = 0
    @Published var losses: UInt64 = 0
    @Published var errors: UInt64 = 0
    private var engineProcess: Process?

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

    func start() {
        guard engineProcess?.isRunning != true else { engineState = "En fonctionnement"; return }
        guard !selectedInterface.isEmpty else { engineState = "Choisissez une interface Ethernet"; return }
        guard let executable = engineURL() else { engineState = "Moteur absent de l’application"; return }
        let process = Process()
        process.executableURL = executable
        process.arguments = ["--run", "--interface", selectedInterface,
            "--rx-group", rxAddress, "--rx-port", String(rxPort),
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
            rxPackets = (object["rxPackets"] as? NSNumber)?.uint64Value ?? 0
            txPackets = (object["txPackets"] as? NSNumber)?.uint64Value ?? 0
            losses = (object["lostPackets"] as? NSNumber)?.uint64Value ?? 0
            errors = (object["malformedPackets"] as? NSNumber)?.uint64Value ?? 0
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
                    ForEach(model.discoveredSessions, id: \.self) { Text($0).tag($0) }
                }
            }.navigationTitle("AES Bridge")
        } detail: {
            Form {
                Section("Réseau") {
                    Picker("Interface Ethernet", selection: $model.selectedInterface) {
                        ForEach(model.interfaces) { Text("\($0.name) — \($0.address)").tag($0.name) }
                    }
                    Button("Actualiser les interfaces") { model.refreshInterfaces() }
                }
                Section("Réception Pi → Mac") {
                    Text(model.selectedSession.isEmpty ? "Sélectionnez la session LXToolPi-Inputs-1-8" : model.selectedSession)
                    TextField("Multicast RX", text: $model.rxAddress)
                    TextField("Port RTP RX", value: $model.rxPort, format: .number)
                    Stepper("Tampon anti-gigue : \(model.jitterMilliseconds) ms", value: $model.jitterMilliseconds, in: 2...50)
                }
                Section("Émission Mac → Pi") {
                    TextField("Multicast", text: $model.txAddress)
                    TextField("Port RTP", value: $model.txPort, format: .number)
                    LabeledContent("Format", value: "8 canaux · L24 · 48 kHz · 1 ms")
                }
                Section("État") {
                    HStack { StatusBadge(title: "Moteur", value: model.engineState); StatusBadge(title: "PTP domaine 0", value: model.ptpState) }
                    Grid(alignment: .leading) {
                        GridRow { Text("RX"); Text("\(model.rxPackets)"); Text("TX"); Text("\(model.txPackets)") }
                        GridRow { Text("Pertes"); Text("\(model.losses)"); Text("Erreurs"); Text("\(model.errors)") }
                    }
                }
                Section {
                    HStack { Button("Démarrer") { model.start() }; Button("Arrêter") { model.stop() }; Button("Relancer") { model.restart() } }
                }
                Text("Prototype : le transport TX/RX fonctionne en boucle locale. PTP, découverte SAP et stabilité restent à valider avec le Raspberry Pi réel.")
                    .font(.caption).foregroundStyle(.secondary)
            }.formStyle(.grouped).padding().navigationTitle("Configuration 8×8")
        }.frame(minWidth: 880, minHeight: 610).onReceive(statusTimer) { _ in model.refreshStatus() }
    }
}

@main struct AESBridgeManagerApp: App {
    var body: some Scene { WindowGroup { ContentView() } }
}
