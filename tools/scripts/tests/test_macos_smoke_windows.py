"""Exercise the native capture helper against normal and modal Cocoa windows."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "macos-smoke.swift"
COMPILE_TIMEOUT = 120
TEST_TIMEOUT = 30
PERMISSION_UNAVAILABLE = 77
FIXTURE = r'''
@main
private struct WindowSelectionTest {
    private static let smallFrame = NSRect(x: 100, y: 100, width: 320, height: 200)
    private static let largeFrame = NSRect(x: 100, y: 100, width: 640, height: 480)
    private static let pollSeconds = 0.1
    private static let readySeconds = 5.0
    private static let unavailableExitCode: Int32 = 77

    @MainActor
    static func main() async {
        guard CGPreflightScreenCaptureAccess() else { Darwin.exit(unavailableExitCode) }
        guard #available(macOS 14.0, *) else { Darwin.exit(unavailableExitCode) }
        let app = NSApplication.shared
        app.setActivationPolicy(.regular)
        app.finishLaunching()
        let normal = NSWindow(contentRect: smallFrame, styleMask: [.titled],
                              backing: .buffered, defer: false)
        let panel = NSPanel(contentRect: largeFrame, styleMask: [.titled],
                            backing: .buffered, defer: false)
        // A modal session controls event dispatch; set the window level explicitly.
        panel.level = .modalPanel
        normal.orderFrontRegardless()
        let session = app.beginModalSession(for: panel)
        defer {
            app.endModalSession(session)
            panel.orderOut(nil)
            normal.orderOut(nil)
        }
        do {
            try require(panel.level != .normal, "Modal fixture must use an elevated window layer")
            let deadline = Date().addingTimeInterval(readySeconds)
            var selected: SCWindow?
            repeat {
                selected = try? await visibleWindow(pid: getpid())
                if selected?.windowID == CGWindowID(panel.windowNumber) { break }
                try await sleepSeconds(pollSeconds)
            } while Date() < deadline
            try require(selected?.windowID == CGWindowID(panel.windowNumber),
                        "Capture must select the larger modal panel instead of the normal window")
            try require(selected?.windowLayer == NSWindow.Level.modalPanel.rawValue,
                        "Captured panel must retain its elevated window layer")
            try require(selected?.owningApplication?.processID == getpid(),
                        "Captured window must belong to the requested process")
            try require((selected?.frame.width ?? 0) > 0 && (selected?.frame.height ?? 0) > 0,
                        "Captured window must have positive bounds")
            if (try? await visibleWindow(pid: pid_t.max)) != nil {
                throw SmokeError("Capture must not fall back to a different process")
            }
        } catch {
            FileHandle.standardError.write(Data("\(error.localizedDescription)\n".utf8))
            Darwin.exit(EXIT_FAILURE)
        }
    }
}
'''


@unittest.skipUnless(sys.platform == "darwin", "Requires Cocoa and ScreenCaptureKit")
class MacOSWindowSelectionTests(unittest.TestCase):
    def test_modal_panel_is_captured_without_weakening_process_selection(self):
        # Compile the production helper functions with a fixture entry point.
        helper_source, separator, _ = SCRIPT.read_text(encoding="utf-8").partition("\n@main\n")
        self.assertTrue(separator, "Native helper entry point is missing")
        with tempfile.TemporaryDirectory(prefix="macos-smoke-windows-") as temporary:
            work = Path(temporary)
            source, helper = work / "windows.swift", work / "windows"
            source.write_text(helper_source + "\n" + FIXTURE, encoding="utf-8")
            compiled = subprocess.run(["/usr/bin/xcrun", "swiftc", "-swift-version", "5",
                                       "-parse-as-library", source, "-o", helper],
                                      capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([helper], capture_output=True, text=True, timeout=TEST_TIMEOUT)
        if result.returncode == PERMISSION_UNAVAILABLE:
            self.skipTest("Requires macOS 14+ with screen recording permission")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
