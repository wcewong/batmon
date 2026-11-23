import CoreBluetooth
import Foundation

/// Reads the standard BLE Battery Service (0x180F / 0x2A19) of every peripheral the system is
/// connected to. CoreBluetooth delivers its callbacks as main-queue blocks, so everything here runs
/// on the main thread asynchronously (no blocking) — the reliable way to drive it inside an app.
final class BLEReader: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private let svc = CBUUID(string: "180F")
    private let chr = CBUUID(string: "2A19")
    private var pending: Set<CBPeripheral> = []
    private var results: [String: Int] = [:]           // peripheral name -> battery %
    private var completion: (([String: Int]) -> Void)?
    private var timeout: DispatchWorkItem?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)  // nil = main queue
    }

    /// Reads all connected peripherals' battery levels; `completion` runs on the main thread. A new
    /// read while one is in flight is ignored (returns empty).
    func read(completion: @escaping ([String: Int]) -> Void) {
        guard self.completion == nil else { completion([:]); return }
        self.completion = completion
        results = [:]
        let t = DispatchWorkItem { [weak self] in self?.finish() }
        timeout = t
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: t)  // safety cap
        if central.state == .poweredOn { start() }  // else wait for state callback
    }

    private func start() {
        let conn = central.retrieveConnectedPeripherals(withServices: [svc])
        pending = Set(conn)
        guard !conn.isEmpty else { finish(); return }
        for p in conn {
            p.delegate = self
            central.connect(p, options: nil)
        }
    }

    private func finish() {
        timeout?.cancel(); timeout = nil
        for p in pending { central.cancelPeripheralConnection(p) }
        pending.removeAll()
        let c = completion; completion = nil
        c?(results)
    }

    private func drop(_ p: CBPeripheral) {
        if pending.remove(p) != nil { central.cancelPeripheralConnection(p) }
        if pending.isEmpty { finish() }
    }

    // MARK: CBCentralManagerDelegate (all on main queue)

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        guard completion != nil else { return }
        if c.state == .poweredOn { start() } else { finish() }  // off / unauthorized -> no results
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([svc])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        drop(p)
    }

    // MARK: CBPeripheralDelegate

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let s = p.services?.first(where: { $0.uuid == svc }) else { drop(p); return }
        p.discoverCharacteristics([chr], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard error == nil, let ch = s.characteristics?.first(where: { $0.uuid == chr }) else { drop(p); return }
        p.readValue(for: ch)
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        if error == nil, let data = ch.value, let b = data.first, let name = p.name {
            results[name] = Int(b)
        }
        drop(p)
    }
}
