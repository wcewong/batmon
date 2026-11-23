import AppKit

func kindSymbol(_ kind: Int) -> String {
    switch kind {
    case 1: return "computermouse.fill"
    case 2: return "headphones"
    case 3: return "keyboard.fill"
    case 4: return "gamecontroller.fill"
    case 5: return "iphone"
    case 6: return "mic.fill"
    case 7: return "hifispeaker.fill"
    default: return "dot.radiowaves.left.and.right"
    }
}

func batteryNSColor(_ percent: Int, charging: Bool) -> NSColor {
    if charging { return .systemGreen }
    switch percent {
    case ..<0: return .secondaryLabelColor
    case 0..<20: return .systemRed
    case 20..<50: return .systemOrange
    default: return .labelColor
    }
}

/// Manages one NSStatusItem per device (Windows-taskbar style: each device is its own menu-bar
/// icon + battery %, no click needed), plus a persistent control item whose menu toggles which
/// devices are shown. Hidden devices are remembered across launches in UserDefaults.
@MainActor
final class StatusBarController: NSObject {
    private let model: BatteryModel
    private var items: [String: NSStatusItem] = [:]   // device id -> status item
    private var controlItem: NSStatusItem?
    private var lastDevices: [Device] = []            // every device seen last poll (incl. hidden)

    private let hiddenKey = "hiddenDeviceIDs"
    private var hidden: Set<String> {
        didSet { UserDefaults.standard.set(Array(hidden), forKey: hiddenKey) }
    }

    private let iconKey = "iconOverrides"
    private var iconOverrides: [String: String] {
        didSet { UserDefaults.standard.set(iconOverrides, forKey: iconKey) }
    }

    // Icon choices offered in each device's "Icon" submenu (label -> SF Symbol, nil = automatic).
    private let iconChoices: [(String, String?)] = [
        ("Automatic", nil),
        ("Knob / Dial", "dial.medium.fill"),
        ("Keyboard", "keyboard.fill"),
        ("Mouse", "computermouse.fill"),
        ("Trackpad", "rectangle.and.hand.point.up.left.fill"),
        ("Headphones", "headphones"),
        ("Game Controller", "gamecontroller.fill"),
        ("Speaker", "hifispeaker.fill"),
        ("Microphone", "mic.fill"),
    ]

    init(model: BatteryModel) {
        self.model = model
        self.hidden = Set(UserDefaults.standard.stringArray(forKey: hiddenKey) ?? [])
        self.iconOverrides = (UserDefaults.standard.dictionary(forKey: iconKey) as? [String: String]) ?? [:]
        super.init()
        setupControlItem()
    }

    /// The SF Symbol to show for a device: a user override if set, otherwise an auto choice. The BT
    /// minor type reports a trackpad as a "Mouse", so special-case the name for a proper glyph.
    private func symbol(for d: Device) -> String {
        if let override = iconOverrides[d.id] { return override }
        if d.name.localizedCaseInsensitiveContains("trackpad") {
            return "rectangle.and.hand.point.up.left.fill"
        }
        return kindSymbol(d.kind)
    }

    func sync(devices: [Device]) {
        lastDevices = devices
        let visible = devices.filter { !hidden.contains($0.id) }
        let visibleIDs = Set(visible.map(\.id))

        for (id, item) in items where !visibleIDs.contains(id) {
            NSStatusBar.system.removeStatusItem(item)
            items[id] = nil
        }
        for d in visible {
            let item = items[d.id] ?? NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
            items[d.id] = item
            configure(item, for: d)
        }
        updateControlMenu()
    }

    // ---- per-device items ----

    private func configure(_ item: NSStatusItem, for d: Device) {
        guard let button = item.button else { return }
        let img = NSImage(systemSymbolName: symbol(for: d), accessibilityDescription: d.name)
        img?.isTemplate = true
        button.image = img
        button.imagePosition = .imageLeading
        button.imageHugsTitle = true

        let text: String
        if !d.connected { text = " off" }
        else if d.percent >= 0 { text = " \(d.percent)%" }
        else if !d.levelText.isEmpty { text = " \(d.levelText)" }
        else { text = " ?" }  // connected, battery not reported yet (e.g. just after waking)
        button.attributedTitle = NSAttributedString(string: text, attributes: [
            .foregroundColor: batteryNSColor(d.percent, charging: d.charging),
            .font: NSFont.systemFont(ofSize: NSFont.systemFontSize),
        ])
        button.appearsDisabled = !d.connected
        button.toolTip = d.detail.isEmpty ? d.name : "\(d.name) — \(d.detail)"
        item.menu = deviceMenu(for: d)
    }

    private func deviceMenu(for d: Device) -> NSMenu {
        let m = NSMenu()
        addDisabled(m, d.name.isEmpty ? "Unknown device" : d.name)
        var status = d.connected
            ? (d.percent >= 0 ? "Battery: \(d.percent)%" : (d.levelText.isEmpty ? "Battery: unknown" : "Battery: \(d.levelText)"))
            : "Disconnected"
        if d.charging { status += " · charging" }
        addDisabled(m, status)
        if !d.via.isEmpty { addDisabled(m, "Via \(d.via)") }
        if !d.detail.isEmpty { addDisabled(m, d.detail) }
        m.addItem(.separator())
        let iconItem = NSMenuItem(title: "Icon", action: nil, keyEquivalent: "")
        iconItem.submenu = iconSubmenu(for: d)
        m.addItem(iconItem)
        let hide = NSMenuItem(title: "Hide from Menu Bar", action: #selector(hideDevice(_:)), keyEquivalent: "")
        hide.target = self
        hide.representedObject = d.id
        m.addItem(hide)
        m.addItem(.separator())
        addAction(m, "Refresh Now", #selector(refreshAction), key: "r")
        addAction(m, "Quit Batmon", #selector(quitAction), key: "q")
        return m
    }

    private func iconSubmenu(for d: Device) -> NSMenu {
        let sub = NSMenu()
        let current = iconOverrides[d.id]  // nil = automatic
        for (label, sym) in iconChoices {
            let it = NSMenuItem(title: label, action: #selector(setIcon(_:)), keyEquivalent: "")
            it.target = self
            it.representedObject = ["id": d.id, "symbol": sym ?? ""]
            it.state = (current == sym) ? .on : .off
            if let sym, let img = NSImage(systemSymbolName: sym, accessibilityDescription: label) {
                img.isTemplate = true
                it.image = img
            }
            sub.addItem(it)
        }
        return sub
    }

    // ---- control item (always present) ----

    private func setupControlItem() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        let img = NSImage(systemSymbolName: "slider.horizontal.3", accessibilityDescription: "Batmon")
        img?.isTemplate = true
        item.button?.image = img
        item.button?.toolTip = "Batmon — show/hide devices"
        controlItem = item
        updateControlMenu()
    }

    private func updateControlMenu() {
        guard let item = controlItem else { return }
        let m = NSMenu()
        if lastDevices.isEmpty {
            addDisabled(m, model.lastUpdated == nil ? "Scanning…" : "No wireless devices")
        } else {
            addDisabled(m, "Show in Menu Bar")
            for d in lastDevices {
                let name = d.name.isEmpty ? "Unknown device" : d.name
                let suffix = d.percent >= 0 ? "  \(d.percent)%" : ""
                let it = NSMenuItem(title: name + suffix, action: #selector(toggleDevice(_:)), keyEquivalent: "")
                it.target = self
                it.representedObject = d.id
                it.state = hidden.contains(d.id) ? .off : .on
                m.addItem(it)
            }
        }
        m.addItem(.separator())
        addAction(m, "Refresh Now", #selector(refreshAction), key: "r")
        addAction(m, "Quit Batmon", #selector(quitAction), key: "q")
        item.menu = m
    }

    // ---- actions ----

    @objc private func toggleDevice(_ sender: NSMenuItem) {
        guard let id = sender.representedObject as? String else { return }
        if hidden.contains(id) { hidden.remove(id) } else { hidden.insert(id) }
        sync(devices: lastDevices)
    }

    @objc private func hideDevice(_ sender: NSMenuItem) {
        guard let id = sender.representedObject as? String else { return }
        hidden.insert(id)
        sync(devices: lastDevices)
    }

    @objc private func setIcon(_ sender: NSMenuItem) {
        guard let info = sender.representedObject as? [String: String], let id = info["id"] else { return }
        let sym = info["symbol"] ?? ""
        if sym.isEmpty { iconOverrides[id] = nil } else { iconOverrides[id] = sym }
        sync(devices: lastDevices)
    }

    @objc private func refreshAction() { model.refresh() }
    @objc private func quitAction() { NSApplication.shared.terminate(nil) }

    // ---- helpers ----

    private func addDisabled(_ m: NSMenu, _ title: String) {
        let it = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        it.isEnabled = false
        m.addItem(it)
    }

    private func addAction(_ m: NSMenu, _ title: String, _ sel: Selector, key: String) {
        let it = NSMenuItem(title: title, action: sel, keyEquivalent: key)
        it.target = self
        m.addItem(it)
    }
}
