
import AppKit
import Foundation

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

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let supportDir = "/Library/Application Support/CellularLTE"
    private let commandsDir = "/Library/Application Support/CellularLTE/Commands"
    private let statePath = "/Library/Application Support/CellularLTE/state.json"
    private let engineLogPath = "/Library/Application Support/CellularLTE/engine.log"
    private let helperLogPath = "/Library/Application Support/CellularLTE/helper.log"

    private var statusItem: NSStatusItem!

    private var statusLine: NSMenuItem!
    private var operatorLine: NSMenuItem!
    private var signalLine: NSMenuItem!
    private var apnLine: NSMenuItem!
    private var modeLine: NSMenuItem!
    private var primaryLine: NSMenuItem!
    private var ipv4Line: NSMenuItem!
    private var ipv6Line: NSMenuItem!

    private var autoItem: NSMenuItem!
    private var connectItem: NSMenuItem!
    private var disconnectItem: NSMenuItem!

    private var currentState: LTEState?
    private var timer: Timer?

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)

        statusItem = NSStatusBar.system.statusItem(
            withLength: NSStatusItem.variableLength
        )

        if let button = statusItem.button {
            button.imagePosition = .imageLeft
            button.image = signalImage(bars: 0, known: false)
            button.title = " Cellular"
            button.toolTip = "Cellular"
        }

        let menu = NSMenu()
        menu.delegate = self

        statusLine = disabledItem("Cellular: checking…")
        operatorLine = disabledItem("Operator: checking…")
        signalLine = disabledItem("Signal: checking…")
        apnLine = disabledItem("APN: Auto • ready")
        modeLine = disabledItem("Mode: checking…")
        primaryLine = disabledItem("Primary: checking…")
        ipv4Line = disabledItem("IPv4: —")
        ipv6Line = disabledItem("Cellular IPv6: —")

        menu.addItem(statusLine)
        menu.addItem(operatorLine)
        menu.addItem(signalLine)
        menu.addItem(apnLine)
        menu.addItem(modeLine)
        menu.addItem(primaryLine)
        menu.addItem(ipv4Line)
        menu.addItem(ipv6Line)

        menu.addItem(.separator())

        autoItem = NSMenuItem(
            title: "Automatic fallback",
            action: #selector(toggleAuto),
            keyEquivalent: ""
        )

        autoItem.target = self
        menu.addItem(autoItem)

        connectItem = NSMenuItem(
            title: "Connect manually",
            action: #selector(connectLTE),
            keyEquivalent: ""
        )

        connectItem.target = self
        menu.addItem(connectItem)

        disconnectItem = NSMenuItem(
            title: "Disconnect manually",
            action: #selector(disconnectLTE),
            keyEquivalent: ""
        )

        disconnectItem.target = self
        menu.addItem(disconnectItem)

        menu.addItem(.separator())

        let refreshOperatorItem = NSMenuItem(
            title: "Refresh operator",
            action: #selector(refreshOperator),
            keyEquivalent: ""
        )

        refreshOperatorItem.target = self
        menu.addItem(refreshOperatorItem)

        let engineLogItem = NSMenuItem(
            title: "Open log",
            action: #selector(openEngineLog),
            keyEquivalent: ""
        )

        engineLogItem.target = self
        menu.addItem(engineLogItem)

        let helperLogItem = NSMenuItem(
            title: "Open helper log",
            action: #selector(openHelperLog),
            keyEquivalent: ""
        )

        helperLogItem.target = self
        menu.addItem(helperLogItem)

        let folderItem = NSMenuItem(
            title: "Open Cellular folder",
            action: #selector(openSupportFolder),
            keyEquivalent: ""
        )

        folderItem.target = self
        menu.addItem(folderItem)

        menu.addItem(.separator())

        let loginLine = disabledItem("Launch at login: Enabled")
        menu.addItem(loginLine)

        let quitItem = NSMenuItem(
            title: "Quit Cellular",
            action: #selector(quitApp),
            keyEquivalent: "q"
        )

        quitItem.target = self
        menu.addItem(quitItem)

        statusItem.menu = menu

        refreshStatus()

        timer = Timer.scheduledTimer(
            withTimeInterval: 1.0,
            repeats: true
        ) { [weak self] _ in
            self?.refreshStatus()
        }
    }

    func menuWillOpen(_ menu: NSMenu) {
        /*
         * Doar citim un JSON local mic. Nu lansam ps/route/shell
         * pe main thread, deci pastram corectia de responsivitate v1.5.
         */
        refreshStatus()
    }

    private func signalImage(bars: Int, known: Bool) -> NSImage {
        let size = NSSize(width: 17, height: 14)
        let active = max(0, min(4, bars))

        let image = NSImage(
            size: size,
            flipped: false
        ) { rect in
            let heights: [CGFloat] = [3.0, 6.0, 9.0, 12.0]
            let width: CGFloat = 2.6
            let gap: CGFloat = 1.4
            let startX: CGFloat = 0.8
            let baseline: CGFloat = 1.0

            for i in 0..<4 {
                let alpha: CGFloat

                if !known {
                    alpha = 0.22
                } else if i < active {
                    alpha = 1.0
                } else {
                    alpha = 0.20
                }

                NSColor.labelColor
                    .withAlphaComponent(alpha)
                    .setFill()

                let x = startX + CGFloat(i) * (width + gap)
                let barRect = NSRect(
                    x: x,
                    y: baseline,
                    width: width,
                    height: heights[i]
                )

                NSBezierPath(
                    roundedRect: barRect,
                    xRadius: 0.8,
                    yRadius: 0.8
                ).fill()
            }

            return true
        }

        image.isTemplate = false
        return image
    }

    private func disabledItem(_ title: String) -> NSMenuItem {
        let item = NSMenuItem(
            title: title,
            action: nil,
            keyEquivalent: ""
        )

        item.isEnabled = false
        return item
    }

    private func loadState() -> LTEState? {
        guard
            let data = try? Data(
                contentsOf: URL(fileURLWithPath: statePath)
            ),
            !data.isEmpty
        else {
            return nil
        }

        return try? JSONDecoder().decode(
            LTEState.self,
            from: data
        )
    }

    private func normalizedProvider(_ raw: String) -> String {
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

    private func shortProvider(_ provider: String) -> String {
        let clean = normalizedProvider(provider)

        if clean.isEmpty || clean == "Cellular" {
            return "Cellular"
        }

        if clean.count <= 16 {
            return clean
        }

        return String(clean.prefix(15)) + "…"
    }

    private func refreshStatus() {
        guard let state = loadState() else {
            currentState = nil

            statusLine.title = "Cellular: helper unavailable"
            operatorLine.title = "Operator: —"
            signalLine.title = "Signal: —"
            apnLine.title = "APN: —"
            modeLine.title = "Mode: —"
            primaryLine.title = "Primary: —"
            ipv4Line.title = "IPv4: —"
            ipv6Line.title = "Cellular IPv6: —"

            autoItem.state = .off
            connectItem.isEnabled = false
            disconnectItem.isEnabled = false

            statusItem.button?.title = " Cellular"
            statusItem.button?.image = signalImage(bars: 0, known: false)
            return
        }

        /*
         * Daca state-ul nu a fost actualizat > 5 sec, daemonul este
         * probabil indisponibil.
         */
        let age = Date().timeIntervalSince1970 - state.timestamp

        guard age < 5 else {
            currentState = state

            statusLine.title = "Cellular: helper stale"
            operatorLine.title = "Operator: \(normalizedProvider(state.provider))"
            signalLine.title = signalText(state)
            apnLine.title = apnText(state)
            modeLine.title = "Mode: —"
            primaryLine.title = "Primary: —"
            ipv4Line.title = "IPv4: —"
            ipv6Line.title = "Cellular IPv6: —"

            connectItem.isEnabled = false
            disconnectItem.isEnabled = false

            statusItem.button?.title =
                " \(shortProvider(state.provider))"
            statusItem.button?.image = signalImage(
                bars: state.signalBars,
                known: state.signalKnown
            )

            return
        }

        currentState = state

        statusItem.button?.title =
            " \(shortProvider(state.provider))"
        statusItem.button?.image = signalImage(
            bars: state.signalBars,
            known: state.signalKnown
        )

        if !state.monitorsReady {
            primaryLine.title = "Primary: detecting…"
        } else {
            primaryLine.title = "Primary: \(state.primary)"
        }

        operatorLine.title =
            "Operator: \(normalizedProvider(state.provider))"

        signalLine.title = signalText(state)

        modeLine.title =
            state.autoEnabled
            ? "Mode: Automatic fallback"
            : "Mode: Manual"

        autoItem.state =
            state.autoEnabled ? .on : .off

        switch state.connectionPhase {
        case "connected":
            let detail =
                state.utun.isEmpty ? "" : " • \(state.utun)"

            statusLine.title =
                "Cellular: Connected\(detail)"

            ipv4Line.title =
                state.ipv4.isEmpty
                ? "IPv4: connected"
                : "IPv4: \(state.ipv4)"

        case "connecting":
            statusLine.title = "Cellular: Connecting…"
            ipv4Line.title = "IPv4: waiting for bearer…"

        default:
            statusLine.title = "Cellular: Disconnected"
            ipv4Line.title = "IPv4: —"
        }

        /*
         * UI future-ready:
         * - daca motorul configureaza in viitor un IPv6 global pe utun,
         *   helper-ul il detecteaza si il afisam automat;
         * - pe bearer-ul actual, operatorul nu livreaza IPv6.
         *
         * Wi-Fi/Ethernet IPv6 nu este modificat de Cellular.
         */
        if state.ipv6LTE && !state.ipv6.isEmpty {
            ipv6Line.title = "Cellular IPv6: \(state.ipv6)"
        } else if state.connectionPhase == "connected" {
            ipv6Line.title = "Cellular IPv6: Carrier unavailable"
        } else {
            ipv6Line.title = "Cellular IPv6: —"
        }

        if !state.lastError.isEmpty {
            statusLine.title += " ⚠︎"
        }

        connectItem.isEnabled = !state.running
        disconnectItem.isEnabled = state.running
    }

    private func apnText(_ state: LTEState) -> String {
        if !state.activeAPN.isEmpty {
            return "APN: Auto • \(state.activeAPN)"
        }

        if state.connectionPhase == "connecting" {
            if !state.preferredAPN.isEmpty {
                return "APN: Auto • trying \(state.preferredAPN)"
            }

            return "APN: Auto • discovering"
        }

        if !state.preferredAPN.isEmpty {
            return "APN: Auto • \(state.preferredAPN)"
        }

        return "APN: Auto • ready"
    }

    private func signalText(_ state: LTEState) -> String {
        guard state.signalKnown else {
            return "Signal: unavailable"
        }

        let filled = String(repeating: "▮", count: max(0, min(4, state.signalBars)))
        let empty = String(repeating: "▯", count: max(0, 4 - state.signalBars))

        if let dbm = state.signalDBm {
            return "Signal: \(filled)\(empty) • \(dbm) dBm"
        }

        return "Signal: \(filled)\(empty)"
    }

    private func sendCommand(_ command: String) {
        let fm = FileManager.default

        guard fm.fileExists(atPath: commandsDir) else {
            showAlert(
                title: "Helper indisponibil",
                message:
                    "Lipsește folderul de comenzi:\n\(commandsDir)"
            )
            return
        }

        let name =
            String(format: "%.0f", Date().timeIntervalSince1970 * 1000)
            + "-"
            + UUID().uuidString
            + ".command"

        let path = commandsDir + "/" + name

        do {
            try (command + "\n").write(
                toFile: path,
                atomically: true,
                encoding: .utf8
            )
        } catch {
            showAlert(
                title: "Nu pot trimite comanda",
                message:
                    "\(error.localizedDescription)\n\n" +
                    "Verifică helper-ul Cellular."
            )
        }
    }

    @objc private func toggleAuto() {
        let enabled =
            !(currentState?.autoEnabled ?? false)

        sendCommand(
            enabled ? "AUTO_ON" : "AUTO_OFF"
        )
    }

    @objc private func connectLTE() {
        sendCommand("CONNECT")
    }

    @objc private func disconnectLTE() {
        sendCommand("DISCONNECT")
    }

    @objc private func refreshOperator() {
        sendCommand("REFRESH_OPERATOR")
    }

    @objc private func openEngineLog() {
        NSWorkspace.shared.open(
            URL(fileURLWithPath: engineLogPath)
        )
    }

    @objc private func openHelperLog() {
        NSWorkspace.shared.open(
            URL(fileURLWithPath: helperLogPath)
        )
    }

    @objc private func openSupportFolder() {
        NSWorkspace.shared.open(
            URL(fileURLWithPath: supportDir)
        )
    }

    @objc private func quitApp() {
        NSApp.terminate(nil)
    }

    private func showAlert(
        title: String,
        message: String
    ) {
        NSApp.activate(ignoringOtherApps: true)

        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()

app.delegate = delegate
app.run()
