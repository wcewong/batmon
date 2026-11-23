import Foundation
import Combine
import os

private let log = Logger(subsystem: "ee.chong.wong.batmon", category: "poll")

struct Device: Identifiable {
    let id: String
    let name: String
    let via: String
    let detail: String
    let levelText: String
    let note: String
    var percent: Int       // -1 unknown; filled in by the live BLE read
    let kind: Int
    let charging: Bool
    let full: Bool
    let connected: Bool
    let vid: UInt16
}

@MainActor
final class BatteryModel: ObservableObject {
    @Published var devices: [Device] = []
    @Published var lastUpdated: Date?
    @Published var polling = false

    private var timer: Timer?
    private var started = false
    private let ble = BLEReader()  // live BLE battery reads (CoreBluetooth, main queue)
    private var bleCache: [String: Int] = [:]  // last-known BLE level per device name

    /// The lowest connected battery level, for the menu-bar glyph. nil when nothing is known.
    var lowestPercent: Int? {
        devices.filter { $0.connected && $0.percent >= 0 }.map(\.percent).min()
    }

    func startIfNeeded(interval: TimeInterval = 60) {
        guard !started else { return }
        started = true
        var interval = interval
        if let s = ProcessInfo.processInfo.environment["BATMON_POLL_SECS"], let v = TimeInterval(s) {
            interval = v
        }
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
    }

    func refresh() {
        guard !polling else { return }
        polling = true
        // Run the (blocking) poll on a plain GCD background thread, not the Swift cooperative pool:
        // poll() blocks on CoreBluetooth / system_profiler, and blocking a cooperative thread can
        // deadlock the concurrency runtime.
        DispatchQueue.global(qos: .utility).async {
            let devs = BatteryModel.poll()
            log.info("poll complete: \(devs.count, privacy: .public) device(s)")
            Task { @MainActor in
                var d = devs
                self.overlayCache(&d)   // show last-known BLE levels immediately (no flicker to "?")
                self.devices = BatteryModel.sort(d)
                self.lastUpdated = Date()
                self.polling = false
                // Then refresh with a fresh BLE read (e.g. a knob macOS hasn't re-read after wake).
                self.ble.read { levels in
                    MainActor.assumeIsolated { self.applyBLE(levels) }
                }
            }
        }
    }

    /// Merge live BLE battery levels onto the current devices, matched by fuzzy name.
    private func applyBLE(_ levels: [String: Int]) {
        guard !levels.isEmpty else { return }   // a failed read keeps the last value (see bleCache)
        for (k, v) in levels { bleCache[k] = v }
        var devs = devices
        var changed = false
        for i in devs.indices {
            for (name, pct) in levels where BatteryModel.namesMatch(devs[i].name, name) {
                if devs[i].percent != pct { devs[i].percent = pct; changed = true }
                break
            }
        }
        if changed { devices = BatteryModel.sort(devs) }
    }

    /// Overlay the last-known BLE levels onto devices that currently have no battery value, so a
    /// transient read miss does not flip a device back to "?".
    private func overlayCache(_ devs: inout [Device]) {
        guard !bleCache.isEmpty else { return }
        for i in devs.indices where devs[i].percent < 0 {
            for (name, pct) in bleCache where BatteryModel.namesMatch(devs[i].name, name) {
                devs[i].percent = pct
                break
            }
        }
    }

    private static func sort(_ devs: [Device]) -> [Device] {
        devs.sorted { lhs, rhs in
            if lhs.connected != rhs.connected { return lhs.connected }
            return sortKey(lhs) < sortKey(rhs)
        }
    }

    private static func sortKey(_ d: Device) -> Int { d.percent < 0 ? 101 : d.percent }

    /// True when one name is a substring of the other (>=4 chars), e.g. "LIFT" vs "Logitech LIFT…".
    private static func namesMatch(_ a: String, _ b: String) -> Bool {
        let x = a.lowercased(), y = b.lowercased()
        guard x.count >= 4, y.count >= 4 else { return x == y }
        return x.contains(y) || y.contains(x)
    }

    /// Calls the C++ core through the bridge. Runs off the main thread.
    private nonisolated static func poll() -> [Device] {
        let r = batmon_poll()
        defer { batmon_free(r) }
        var out: [Device] = []
        out.reserveCapacity(Int(r.count))
        for i in 0..<Int(r.count) {
            let d = r.items[i]
            out.append(Device(
                id: String(cString: d.key),
                name: String(cString: d.name),
                via: String(cString: d.via),
                detail: String(cString: d.detail),
                levelText: String(cString: d.level_text),
                note: String(cString: d.note),
                percent: Int(d.percent),
                kind: Int(d.kind),
                charging: d.charging != 0,
                full: d.full != 0,
                connected: d.connected != 0,
                vid: d.vid))
        }
        return out
    }
}
