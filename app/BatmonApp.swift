import SwiftUI
import AppKit
import Combine

@main
struct BatmonApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    var body: some Scene {
        // Menu-bar-only app (LSUIElement); no real window. Status items are managed by AppDelegate.
        Settings { EmptyView() }
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let model = BatteryModel()
    private var statusBar: StatusBarController?
    private var cancellable: AnyCancellable?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let controller = StatusBarController(model: model)
        statusBar = controller
        // One menu-bar item per device, refreshed on every poll.
        cancellable = model.$devices
            .receive(on: RunLoop.main)
            .sink { devices in controller.sync(devices: devices) }
        model.startIfNeeded()

        // Re-poll the moment the Mac wakes, so batteries reappear without waiting for the next tick.
        NSWorkspace.shared.notificationCenter.addObserver(
            self, selector: #selector(systemDidWake), name: NSWorkspace.didWakeNotification, object: nil)
    }

    @objc private func systemDidWake() {
        model.refresh()
        // Right after waking, a reconnecting BLE device's battery may not be re-read yet; poll again
        // shortly to pick up the value once the system has it.
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
            Task { @MainActor in self?.model.refresh() }
        }
    }
}
