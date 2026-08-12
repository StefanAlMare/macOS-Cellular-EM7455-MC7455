
import Foundation
import Network
import Darwin

struct LTEState: Codable {
    let helperVersion: String
    let running: Bool
    let pids: [Int32]
    let utun: String
    let ipv4: String
    let ipv6: String
    let provider: String
    let signalBars: Int
    let signalDBm: Int?
    let signalKnown: Bool
    let apnMode: String
    let activeAPN: String
    let preferredAPN: String
    let connectionPhase: String
    let autoEnabled: Bool
    let wifi: Bool
    let ethernet: Bool
    let monitorsReady: Bool
    let primary: String
    let ipv6LTE: Bool
    let lastAction: String
    let lastError: String
    let timestamp: Double
}

final class LinkState: @unchecked Sendable {
    private let lock = NSLock()

    private var wifiValue = false
    private var ethernetValue = false
    private var wifiReadyValue = false
    private var ethernetReadyValue = false

    func setWiFi(_ value: Bool) {
        lock.lock()
        wifiValue = value
        wifiReadyValue = true
        lock.unlock()
    }

    func setEthernet(_ value: Bool) {
        lock.lock()
        ethernetValue = value
        ethernetReadyValue = true
        lock.unlock()
    }

    func snapshot() -> (wifi: Bool, ethernet: Bool, ready: Bool) {
        lock.lock()
        let result = (
            wifiValue,
            ethernetValue,
            wifiReadyValue && ethernetReadyValue
        )
        lock.unlock()
        return result
    }
}

final class CellularLTEHelper {
    private let version = "2.6.2-pkg-ready"

    private let supportDir = "/Library/Application Support/CellularLTE"
    private let commandsDir = "/Library/Application Support/CellularLTE/Commands"
    private let statePath = "/Library/Application Support/CellularLTE/state.json"
    private let configPath = "/Library/Application Support/CellularLTE/config"
    private let apnCachePath = "/Library/Application Support/CellularLTE/apn-cache.tsv"
    private let helperLogPath = "/Library/Application Support/CellularLTE/helper.log"
    private let engineLogPath = "/Library/Application Support/CellularLTE/engine.log"

    private let enginePath = "/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"
    private let statusPath = "/Library/PrivilegedHelperTools/ro.alexd.em7455_status"

    private let links = LinkState()
    private let wifiMonitor = NWPathMonitor(requiredInterfaceType: .wifi)
    private let ethernetMonitor = NWPathMonitor(requiredInterfaceType: .wiredEthernet)

    private var autoEnabled = true
    private var provider = "Cellular"
    private var signalBars = 0
    private var signalDBm: Int? = nil
    private var signalKnown = false
    private var activeAPN = ""
    private var preferredAPN = ""
    private var lastActiveAPNQuery = Date.distantPast
    private var engineStartedAt: Date? = nil
    private var lastLoggedConnectionPhase = "disconnected"
    private var signalFailures = 0
    private var lastSignalQuery = Date.distantPast
    private var operatorFailures = 0
    private var lastOperatorQuery = Date.distantPast

    private var primaryLostSince: Date?
    private var primaryRestoredSince: Date?

    private var lastAction = "helper-started"
    private var lastError = ""

    init() {
        ensureFilesystem()
        autoEnabled = loadAutoSetting()
        startNetworkMonitoring()

        /*
         * Nu atingem Wi-Fi/Ethernet in niciun fel.
         * Helper-ul doar OBSERVA cele doua tipuri de cale.
         */
        log("Helper \(version) started. Auto=\(autoEnabled ? 1 : 0)")
    }

    private func ensureFilesystem() {
        try? FileManager.default.createDirectory(
            atPath: supportDir,
            withIntermediateDirectories: true
        )

        try? FileManager.default.createDirectory(
            atPath: commandsDir,
            withIntermediateDirectories: true
        )

        for path in [
            statePath,
            configPath,
            apnCachePath,
            helperLogPath,
            engineLogPath
        ] {
            if !FileManager.default.fileExists(atPath: path) {
                FileManager.default.createFile(atPath: path, contents: nil)
            }
        }
    }

    private func log(_ message: String) {
        let formatter = ISO8601DateFormatter()
        let line = "\(formatter.string(from: Date())) \(message)\n"

        if !FileManager.default.fileExists(atPath: helperLogPath) {
            FileManager.default.createFile(atPath: helperLogPath, contents: nil)
        }

        guard let handle = FileHandle(forWritingAtPath: helperLogPath) else {
            return
        }

        handle.seekToEndOfFile()

        if let data = line.data(using: .utf8) {
            handle.write(data)
        }

        try? handle.close()
    }

    private func providerCacheKey(_ raw: String) -> String {
        normalizeProvider(raw).uppercased()
    }

    private func loadCachedAPN(for providerName: String) -> String {
        let key = providerCacheKey(providerName)

        guard !key.isEmpty && key != "CELLULAR" else {
            return ""
        }

        guard let text = try? String(
            contentsOfFile: apnCachePath,
            encoding: .utf8
        ) else {
            return ""
        }

        for raw in text.split(separator: "\n") {
            let fields = raw.split(
                separator: "\t",
                maxSplits: 1,
                omittingEmptySubsequences: false
            )

            guard fields.count == 2 else {
                continue
            }

            if String(fields[0]).uppercased() == key {
                return String(fields[1])
                    .trimmingCharacters(
                        in: .whitespacesAndNewlines
                    )
            }
        }

        return ""
    }

    private func saveCachedAPN(
        provider providerName: String,
        apn: String
    ) {
        let key = providerCacheKey(providerName)
        let cleanAPN = apn
            .trimmingCharacters(in: .whitespacesAndNewlines)

        guard
            !key.isEmpty,
            key != "CELLULAR",
            !cleanAPN.isEmpty
        else {
            return
        }

        var entries: [(String, String)] = []

        if let text = try? String(
            contentsOfFile: apnCachePath,
            encoding: .utf8
        ) {
            for raw in text.split(separator: "\n") {
                let fields = raw.split(
                    separator: "\t",
                    maxSplits: 1,
                    omittingEmptySubsequences: false
                )

                if fields.count == 2 {
                    let oldKey = String(fields[0])
                    let oldAPN = String(fields[1])

                    if oldKey.uppercased() != key {
                        entries.append((oldKey, oldAPN))
                    }
                }
            }
        }

        entries.append((key, cleanAPN.lowercased()))

        let output = entries
            .map { "\($0.0)\t\($0.1)" }
            .joined(separator: "\n") + "\n"

        do {
            try output.write(
                toFile: apnCachePath,
                atomically: true,
                encoding: .utf8
            )

            _ = chmod(apnCachePath, 0o644)

            preferredAPN = cleanAPN.lowercased()

            log(
                "APN cache: \(key) -> \(preferredAPN)"
            )
        } catch {
            log(
                "Cannot save APN cache: \(error.localizedDescription)"
            )
        }
    }

    private func refreshPreferredAPN() {
        preferredAPN = loadCachedAPN(for: provider)
    }

    private func loadAutoSetting() -> Bool {
        guard
            let text = try? String(contentsOfFile: configPath, encoding: .utf8),
            !text.isEmpty
        else {
            saveAutoSetting(true)
            return true
        }

        return text
            .split(separator: "\n")
            .contains(where: { $0.trimmingCharacters(in: .whitespaces) == "AUTO=1" })
    }

    private func saveAutoSetting(_ enabled: Bool) {
        let text = enabled ? "AUTO=1\n" : "AUTO=0\n"

        do {
            try text.write(
                toFile: configPath,
                atomically: true,
                encoding: .utf8
            )
            _ = chmod(configPath, 0o644)
        } catch {
            log("Cannot save config: \(error.localizedDescription)")
        }
    }

    private func startNetworkMonitoring() {
        wifiMonitor.pathUpdateHandler = { [links] path in
            links.setWiFi(path.status == .satisfied)
        }

        ethernetMonitor.pathUpdateHandler = { [links] path in
            links.setEthernet(path.status == .satisfied)
        }

        wifiMonitor.start(
            queue: DispatchQueue(label: "ro.alexd.CellularLTE.wifi")
        )

        ethernetMonitor.start(
            queue: DispatchQueue(label: "ro.alexd.CellularLTE.ethernet")
        )
    }

    private func runCapture(
        _ executable: String,
        _ arguments: [String] = []
    ) -> (Int32, String) {
        let process = Process()
        let pipe = Pipe()

        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = pipe
        process.standardInput = FileHandle.nullDevice

        do {
            try process.run()
        } catch {
            return (-1, error.localizedDescription)
        }

        /*
         * Drenam output-ul inainte de wait, aceeasi corectie validata
         * in aplicatia v1.5. Nu vrem pipe deadlock.
         */
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()

        return (
            process.terminationStatus,
            String(data: data, encoding: .utf8) ?? ""
        )
    }

    private func enginePIDs() -> [Int32] {
        let pattern =
            "^" + NSRegularExpression.escapedPattern(for: enginePath) + "$"

        let result = runCapture("/usr/bin/pgrep", ["-f", pattern])

        if result.0 != 0 && result.0 != 1 {
            return []
        }

        return result.1
            .split(whereSeparator: {
                $0 == "\n" || $0 == " " || $0 == "\t"
            })
            .compactMap { Int32($0) }
    }

    private func isPIDRunning(_ pid: Int32) -> Bool {
        guard pid > 0 else { return false }

        if Darwin.kill(pid, 0) == 0 {
            return true
        }

        return errno == EPERM
    }

    private func normalizeProvider(_ raw: String) -> String {
        let clean = raw
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(
                of: #"\s+"#,
                with: " ",
                options: .regularExpression
            )

        guard !clean.isEmpty else {
            return "Cellular"
        }

        /*
         * Unele firmware-uri Sierra/Dell raporteaza numele operatorului
         * duplicat, de exemplu:
         *   telecom.ro telecom.ro
         *
         * Daca sirul este format din doua jumatati identice separate
         * prin spatiu, pastram o singura aparitie.
         */
        let words = clean.split(separator: " ").map(String.init)

        if words.count >= 2 && words.count % 2 == 0 {
            let half = words.count / 2
            let first = Array(words[0..<half])
            let second = Array(words[half..<words.count])

            if first.map({ $0.lowercased() }) ==
               second.map({ $0.lowercased() }) {
                return first.joined(separator: " ")
            }
        }

        return clean
    }

    private func querySignal(force: Bool = false) {
        if !force && Date().timeIntervalSince(lastSignalQuery) < 2.0 {
            return
        }

        lastSignalQuery = Date()

        guard FileManager.default.isExecutableFile(atPath: statusPath) else {
            signalKnown = false
            signalBars = 0
            signalDBm = nil
            return
        }

        /*
         * AT interface = USB interface 3.
         * Motorul MBIM foloseste alte interfete (12/13), deci aceasta
         * interogare este separata de data plane.
         *
         * Daca modemul refuza temporar claim-ul/comanda, nu atingem
         * sesiunea de date: doar marcam telemetria ca necunoscuta.
         */
        let result = runCapture(statusPath, ["signal"])

        guard result.0 == 0 else {
            signalFailures += 1

            if signalFailures >= 3 {
                signalKnown = false
                signalBars = 0
                signalDBm = nil
            }
            return
        }

        var newBars: Int? = nil
        var newDBm: Int? = nil
        var known = false

        for raw in result.1.split(separator: "\n") {
            let line = String(raw)
                .trimmingCharacters(in: .whitespacesAndNewlines)

            if line.hasPrefix("BARS=") {
                newBars = Int(line.dropFirst("BARS=".count))
            } else if line.hasPrefix("DBM=") {
                let value = String(line.dropFirst("DBM=".count))
                newDBm = Int(value)
            } else if line == "SIGNAL_KNOWN=1" {
                known = true
            }
        }

        if known, let bars = newBars {
            signalKnown = true
            signalBars = max(0, min(4, bars))
            signalDBm = newDBm
            signalFailures = 0
        } else {
            signalFailures += 1

            if signalFailures >= 3 {
                signalKnown = false
                signalBars = 0
                signalDBm = nil
            }
        }
    }

    private func queryActiveAPN(force: Bool = false) {
        let engineRunning = !enginePIDs().isEmpty

        if !engineRunning {
            activeAPN = ""
            return
        }

        /*
         * Nu intrebam contextul activ pana cand ruta 1.1.1.1 este deja
         * pe utun. Asa evitam "checking" in timpul negocierii bearer-ului.
         */
        guard !currentUtun().isEmpty else {
            return
        }

        let interval: TimeInterval =
            activeAPN.isEmpty ? 1.0 : 10.0

        if !force &&
           Date().timeIntervalSince(lastActiveAPNQuery) < interval {
            return
        }

        lastActiveAPNQuery = Date()

        guard FileManager.default.isExecutableFile(atPath: statusPath) else {
            return
        }

        let result = runCapture(statusPath, ["active"])

        guard result.0 == 0 else {
            return
        }

        for raw in result.1.split(separator: "\n") {
            let line = String(raw)
                .trimmingCharacters(in: .whitespacesAndNewlines)

            if line.hasPrefix("ACTIVE_APN=") {
                let value = String(
                    line.dropFirst("ACTIVE_APN=".count)
                )
                .trimmingCharacters(
                    in: .whitespacesAndNewlines
                )

                if !value.isEmpty {
                    activeAPN = value

                    /*
                     * Bearer-ul activ este adevarul final.
                     * Il invatam per operator pentru urmatoarea conectare.
                     */
                    saveCachedAPN(
                        provider: provider,
                        apn: value
                    )
                }

                return
            }
        }
    }

    private func queryOperator(force: Bool = false) {
        /*
         * Interogam AT numai cand engine-ul MBIM NU ruleaza.
         * Astfel nu introducem trafic de diagnostic in timpul sesiunii.
         */
        if !enginePIDs().isEmpty {
            return
        }

        if !force && Date().timeIntervalSince(lastOperatorQuery) < 30 {
            return
        }

        lastOperatorQuery = Date()

        guard FileManager.default.isExecutableFile(atPath: statusPath) else {
            return
        }

        let result = runCapture(statusPath, ["all"])

        guard result.0 == 0 else {
            operatorFailures += 1

            if operatorFailures >= 3 {
                provider = "Cellular"
            }
            return
        }

        for raw in result.1.split(separator: "\n") {
            let line = String(raw)

            if line.hasPrefix("OPERATOR=") {
                let value = String(line.dropFirst("OPERATOR=".count))
                    .trimmingCharacters(in: .whitespacesAndNewlines)

                if !value.isEmpty {
                    if value != provider {
                        log("Operator: \(provider) -> \(value)")
                    }

                    provider = normalizeProvider(value)
                    refreshPreferredAPN()
                    operatorFailures = 0
                } else {
                    operatorFailures += 1

                    if operatorFailures >= 3 {
                        provider = "Cellular"
                    }
                }

                return
            }
        }
    }

    private func startEngine(reason: String) {
        let existing = enginePIDs()

        if !existing.isEmpty {
            lastAction = "already-connected"
            return
        }

        if provider == "Cellular" {
            queryOperator(force: true)
        } else {
            refreshPreferredAPN()
        }

        guard FileManager.default.isExecutableFile(atPath: enginePath) else {
            lastError = "LTE engine missing"
            lastAction = "connect-failed"
            log(lastError)
            return
        }

        if !FileManager.default.fileExists(atPath: engineLogPath) {
            FileManager.default.createFile(atPath: engineLogPath, contents: nil)
        }

        guard let logHandle = FileHandle(forWritingAtPath: engineLogPath) else {
            lastError = "Cannot open engine.log"
            lastAction = "connect-failed"
            log(lastError)
            return
        }

        let separator =
            "\n\n===== START LTE \(ISO8601DateFormatter().string(from: Date())) reason=\(reason) =====\n"

        logHandle.seekToEndOfFile()

        if let data = separator.data(using: .utf8) {
            logHandle.write(data)
        }

        activeAPN = ""
        lastActiveAPNQuery = Date.distantPast
        refreshPreferredAPN()

        let process = Process()
        process.executableURL = URL(fileURLWithPath: enginePath)

        var environment = ProcessInfo.processInfo.environment

        if !preferredAPN.isEmpty {
            environment["CELLULAR_APN_PREFERRED"] = preferredAPN
        } else {
            environment.removeValue(
                forKey: "CELLULAR_APN_PREFERRED"
            )
        }

        process.environment = environment
        process.standardOutput = logHandle
        process.standardError = logHandle
        process.standardInput = FileHandle.nullDevice

        do {
            try process.run()
            let pid = process.processIdentifier

            /*
             * Parentul poate inchide handle-ul; copilul are descriptorul
             * duplicat dupa exec.
             */
            try? logHandle.close()

            engineStartedAt = Date()
            lastLoggedConnectionPhase = "connecting"

            lastError = ""
            lastAction = "connect-\(reason)"
            log(
                "Cellular start PID \(pid), reason=\(reason), preferredAPN=" +
                (preferredAPN.isEmpty ? "(discovery)" : preferredAPN)
            )
        } catch {
            try? logHandle.close()
            lastError = error.localizedDescription
            lastAction = "connect-failed"
            log("LTE start error: \(lastError)")
        }
    }

    private func stopEngine(reason: String) {
        var pids = enginePIDs()

        guard !pids.isEmpty else {
            lastAction = "already-disconnected"
            return
        }

        lastAction = "disconnect-\(reason)"
        log("LTE stop requested, PIDs=\(pids), reason=\(reason)")

        /*
         * Semnalul validat in v1.5.
         */
        for pid in pids {
            _ = Darwin.kill(pid, SIGUSR1)
        }

        let cleanDeadline = Date().addingTimeInterval(15)

        while Date() < cleanDeadline {
            pids = enginePIDs()

            if pids.isEmpty {
                lastError = ""
                log("LTE stopped cleanly with SIGUSR1.")
                return
            }

            Thread.sleep(forTimeInterval: 0.25)
        }

        pids = enginePIDs()

        if !pids.isEmpty {
            log("SIGUSR1 timeout; sending SIGTERM to \(pids).")

            for pid in pids {
                _ = Darwin.kill(pid, SIGTERM)
            }

            let termDeadline = Date().addingTimeInterval(2)

            while Date() < termDeadline {
                pids = enginePIDs()

                if pids.isEmpty {
                    lastError = ""
                    log("LTE stopped after SIGTERM.")
                    return
                }

                Thread.sleep(forTimeInterval: 0.20)
            }
        }

        pids = enginePIDs()

        if !pids.isEmpty {
            log("SIGTERM timeout; SIGKILL for \(pids).")

            for pid in pids {
                _ = Darwin.kill(pid, SIGKILL)
            }

            Thread.sleep(forTimeInterval: 0.5)
            lastError = "LTE required forced stop"
        }
    }

    private func processCommands() {
        guard
            let entries = try? FileManager.default.contentsOfDirectory(
                atPath: commandsDir
            )
        else {
            return
        }

        let commands = entries
            .filter { $0.hasSuffix(".command") }
            .sorted()

        for name in commands {
            let path = commandsDir + "/" + name

            guard let content = try? String(
                contentsOfFile: path,
                encoding: .utf8
            ) else {
                try? FileManager.default.removeItem(atPath: path)
                continue
            }

            let command = content
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .uppercased()

            /*
             * Scoatem fisierul INAINTE de executie pentru ca aceeasi
             * comanda sa nu fie reluata dupa un restart.
             */
            try? FileManager.default.removeItem(atPath: path)

            switch command {
            case "AUTO_ON":
                autoEnabled = true
                saveAutoSetting(true)

                primaryLostSince = nil
                primaryRestoredSince = nil

                lastAction = "auto-enabled"
                lastError = ""
                log("Automatic fallback enabled.")

            case "AUTO_OFF":
                autoEnabled = false
                saveAutoSetting(false)

                primaryLostSince = nil
                primaryRestoredSince = nil

                lastAction = "auto-disabled"
                lastError = ""
                log("Automatic fallback disabled.")

            case "CONNECT":
                /*
                 * O actiune manuala inseamna mod manual.
                 * Altfel Auto ar putea opri imediat sesiunea deoarece
                 * vede Wi-Fi/Ethernet activ.
                 */
                autoEnabled = false
                saveAutoSetting(false)

                primaryLostSince = nil
                primaryRestoredSince = nil

                startEngine(reason: "manual")

            case "DISCONNECT":
                autoEnabled = false
                saveAutoSetting(false)

                primaryLostSince = nil
                primaryRestoredSince = nil

                stopEngine(reason: "manual")

            case "REFRESH_OPERATOR":
                queryOperator(force: true)
                lastAction = "operator-refresh"

            default:
                log("Unknown command ignored: \(command)")
            }
        }
    }

    private func applyAutomaticPolicy() {
        guard autoEnabled else {
            primaryLostSince = nil
            primaryRestoredSince = nil
            return
        }

        let snapshot = links.snapshot()

        /*
         * Nu luam nicio decizie pana cand AMBELE monitoare au dat
         * cel putin un rezultat.
         */
        guard snapshot.ready else {
            return
        }

        let primaryAvailable = snapshot.wifi || snapshot.ethernet
        let running = !enginePIDs().isEmpty

        if primaryAvailable {
            primaryLostSince = nil

            if primaryRestoredSince == nil {
                primaryRestoredSince = Date()
            }

            if running,
               let since = primaryRestoredSince,
               Date().timeIntervalSince(since) >= 3 {

                stopEngine(reason: "primary-restored")
                primaryRestoredSince = nil
            }
        } else {
            primaryRestoredSince = nil

            if primaryLostSince == nil {
                primaryLostSince = Date()
            }

            if !running,
               let since = primaryLostSince,
               Date().timeIntervalSince(since) >= 1 {

                startEngine(reason: "fallback")
                primaryLostSince = nil
            }
        }
    }

    private func currentUtun() -> String {
        guard !enginePIDs().isEmpty else {
            return ""
        }

        let result = runCapture(
            "/sbin/route",
            ["-n", "get", "1.1.1.1"]
        )

        guard result.0 == 0 else {
            return ""
        }

        for raw in result.1.split(separator: "\n") {
            let line = String(raw)
                .trimmingCharacters(in: .whitespaces)

            if line.hasPrefix("interface:") {
                let value = line
                    .replacingOccurrences(
                        of: "interface:",
                        with: ""
                    )
                    .trimmingCharacters(in: .whitespaces)

                return value.hasPrefix("utun") ? value : ""
            }
        }

        return ""
    }

    private func currentIPv4(interface: String) -> String {
        guard !interface.isEmpty else {
            return ""
        }

        let result = runCapture(
            "/sbin/ifconfig",
            [interface]
        )

        guard result.0 == 0 else {
            return ""
        }

        for raw in result.1.split(separator: "\n") {
            let line = String(raw)
                .trimmingCharacters(in: .whitespaces)

            if line.hasPrefix("inet ") {
                let fields = line.split(separator: " ")

                if fields.count >= 2 {
                    return String(fields[1])
                }
            }
        }

        return ""
    }

    private func currentIPv6(interface: String) -> String {
        guard !interface.isEmpty else {
            return ""
        }

        let result = runCapture(
            "/sbin/ifconfig",
            [interface]
        )

        guard result.0 == 0 else {
            return ""
        }

        for raw in result.1.split(separator: "\n") {
            let line = String(raw)
                .trimmingCharacters(in: .whitespaces)

            guard line.hasPrefix("inet6 ") else {
                continue
            }

            let fields = line.split(separator: " ")

            guard fields.count >= 2 else {
                continue
            }

            var address = String(fields[1])

            /*
             * Link-local fe80:: nu inseamna Internet IPv6 celular.
             * Eliminam si scope-ul de forma %utunX.
             */
            if let percent = address.firstIndex(of: "%") {
                address = String(address[..<percent])
            }

            let lower = address.lowercased()

            if lower.hasPrefix("fe80:") ||
               lower == "::1" ||
               lower == "::" {
                continue
            }

            return address
        }

        return ""
    }

    private func writeState() {
        let snapshot = links.snapshot()
        let pids = enginePIDs()
        let running = !pids.isEmpty
        let utun = running ? currentUtun() : ""
        let ipv4 = running ? currentIPv4(interface: utun) : ""
        let ipv6 = running ? currentIPv6(interface: utun) : ""

        let connectionPhase: String

        if !running {
            connectionPhase = "disconnected"
        } else if !utun.isEmpty && !ipv4.isEmpty {
            connectionPhase = "connected"
        } else {
            connectionPhase = "connecting"
        }

        if connectionPhase != lastLoggedConnectionPhase {
            if connectionPhase == "connected",
               let started = engineStartedAt {

                let elapsed = Date().timeIntervalSince(started)
                log(
                    String(
                        format:
                        "TIMING: engine process -> connected %.3f s, utun=%@, ipv4=%@",
                        elapsed,
                        utun,
                        ipv4
                    )
                )
            } else {
                log("TIMING: phase -> \(connectionPhase)")
            }

            lastLoggedConnectionPhase = connectionPhase

            if connectionPhase == "disconnected" {
                engineStartedAt = nil
            }
        }

        let primary: String

        if snapshot.ethernet && snapshot.wifi {
            primary = "Ethernet + Wi-Fi"
        } else if snapshot.ethernet {
            primary = "Ethernet"
        } else if snapshot.wifi {
            primary = "Wi-Fi"
        } else {
            primary = "None"
        }

        let state = LTEState(
            helperVersion: version,
            running: running,
            pids: pids,
            utun: utun,
            ipv4: ipv4,
            ipv6: ipv6,
            provider: provider,
            signalBars: signalBars,
            signalDBm: signalDBm,
            signalKnown: signalKnown,
            apnMode: "adaptive-cache-with-network-default-discovery",
            activeAPN: activeAPN,
            preferredAPN: preferredAPN,
            connectionPhase: connectionPhase,
            autoEnabled: autoEnabled,
            wifi: snapshot.wifi,
            ethernet: snapshot.ethernet,
            monitorsReady: snapshot.ready,
            primary: primary,
            ipv6LTE: !ipv6.isEmpty,
            lastAction: lastAction,
            lastError: lastError,
            timestamp: Date().timeIntervalSince1970
        )

        guard let data = try? JSONEncoder().encode(state) else {
            return
        }

        let tmp = statePath + ".tmp"

        do {
            try data.write(
                to: URL(fileURLWithPath: tmp),
                options: .atomic
            )

            _ = chmod(tmp, 0o644)

            try? FileManager.default.removeItem(atPath: statePath)
            try FileManager.default.moveItem(
                atPath: tmp,
                toPath: statePath
            )

            _ = chmod(statePath, 0o644)
        } catch {
            log("State write error: \(error.localizedDescription)")
        }
    }

    func run() {
        /*
         * Dupa boot/login acordam monitoarelor timp sa publice
         * primele cai inainte de prima decizie Auto.
         */
        let startupGraceUntil = Date().addingTimeInterval(3)

        while true {
            autoreleasepool {
                processCommands()

                if Date() >= startupGraceUntil {
                    applyAutomaticPolicy()
                }

                queryOperator()
                querySignal()
                queryActiveAPN()
                writeState()
            }

            Thread.sleep(forTimeInterval: 0.5)
        }
    }
}

CellularLTEHelper().run()
