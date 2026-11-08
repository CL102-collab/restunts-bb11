"""Exercise production mouse input against a real Cocoa window."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "macos-smoke.swift"
COMPILE_TIMEOUT = 120
TEST_TIMEOUT = 45
PERMISSION_UNAVAILABLE = 77
FIXTURE = r'''
private final class MouseInputView: NSView {
    var downPoints: [CGPoint] = []
    var upPoints: [CGPoint] = []
    var clickCounts: [Int] = []

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseDown(with event: NSEvent) {
        downPoints.append(convert(event.locationInWindow, from: nil))
        clickCounts.append(event.clickCount)
    }

    override func mouseUp(with event: NSEvent) {
        upPoints.append(convert(event.locationInWindow, from: nil))
        clickCounts.append(event.clickCount)
    }
}

@main
private struct MouseInputTest {
    private static let windowFrame = NSRect(x: 100, y: 100, width: 640, height: 480)
    private static let localTarget = CGPoint(x: 195, y: 125)
    private static let coordinateTolerance = 2.0
    private static let expectedClickCount = 1
    private static let outsideOffset = 50.0
    private static let pollSeconds = 0.02
    private static let operationSeconds = 15.0
    private static let deliverySeconds = 1.0
    private static let unavailableExitCode: Int32 = 77

    @MainActor
    private static func pumpEvents() {
        let app = NSApplication.shared
        while let event = app.nextEvent(matching: .any, until: .distantPast,
                                        inMode: .default, dequeue: true) {
            app.sendEvent(event)
        }
    }

    @MainActor
    private static func runClick(helper: URL, work: URL, label: String,
                                 point: CGPoint) async throws -> (Int32, Data, Data) {
        let stdoutURL = work.appendingPathComponent(label + ".stdout")
        let stderrURL = work.appendingPathComponent(label + ".stderr")
        try Data().write(to: stdoutURL)
        try Data().write(to: stderrURL)
        let stdout = try FileHandle(forWritingTo: stdoutURL)
        let stderr = try FileHandle(forWritingTo: stderrURL)
        defer {
            try? stdout.close()
            try? stderr.close()
        }
        let process = Process()
        process.executableURL = helper
        process.arguments = ["click", String(getpid()), String(Double(point.x)), String(Double(point.y))]
        process.standardOutput = stdout
        process.standardError = stderr
        try process.run()
        defer {
            if process.isRunning { Darwin.kill(process.processIdentifier, SIGKILL) }
        }
        let deadline = Date().addingTimeInterval(operationSeconds)
        while process.isRunning && Date() < deadline {
            pumpEvents()
            try await sleepSeconds(pollSeconds)
        }
        try require(!process.isRunning, "Production click helper timed out")
        // Posting an event is asynchronous; drain events delivered after helper exit.
        let deliveryDeadline = Date().addingTimeInterval(deliverySeconds)
        while Date() < deliveryDeadline {
            pumpEvents()
            try await sleepSeconds(pollSeconds)
        }
        return (process.terminationStatus, try Data(contentsOf: stdoutURL),
                try Data(contentsOf: stderrURL))
    }

    @MainActor
    static func main() async {
        guard #available(macOS 14.0, *), CGPreflightScreenCaptureAccess(),
              CGPreflightPostEventAccess() || AXIsProcessTrusted() else {
            Darwin.exit(unavailableExitCode)
        }
        let app = NSApplication.shared
        app.setActivationPolicy(.regular)
        app.finishLaunching()
        let window = NSWindow(contentRect: windowFrame, styleMask: [.titled],
                              backing: .buffered, defer: false)
        let view = MouseInputView(frame: NSRect(origin: .zero, size: windowFrame.size))
        window.contentView = view
        window.makeKeyAndOrderFront(nil)
        defer { window.orderOut(nil) }
        do {
            try require(CommandLine.arguments.count == 3, "Missing production helper or work folder")
            let helper = URL(fileURLWithPath: CommandLine.arguments[1])
            let work = URL(fileURLWithPath: CommandLine.arguments[2])
            guard let mainScreen = NSScreen.screens.first else {
                throw SmokeError("Mouse fixture has no display")
            }
            // AppKit screen coordinates grow upwards; Quartz input grows downwards.
            let appKitPoint = window.convertPoint(toScreen: view.convert(localTarget, to: nil))
            let target = CGPoint(x: appKitPoint.x, y: mainScreen.frame.maxY - appKitPoint.y)
            let (status, output, errors) = try await runClick(helper: helper, work: work,
                                                             label: "inside", point: target)
            try require(status == EXIT_SUCCESS,
                        "Production click failed: " + String(decoding: output + errors, as: UTF8.self))
            try require(view.downPoints.count == 1 && view.upPoints.count == 1,
                        "Production click must deliver one mouse-down and one mouse-up to the target view")
            try require(view.clickCounts.allSatisfy { $0 == expectedClickCount },
                        "Production mouse edges must identify a single click")
            for point in view.downPoints + view.upPoints {
                try require(abs(point.x - localTarget.x) <= coordinateTolerance &&
                            abs(point.y - localTarget.y) <= coordinateTolerance,
                            "Production click was delivered at the wrong view coordinates: \(point)")
            }
            guard let metadata = try JSONSerialization.jsonObject(with: output) as? [String: Any] else {
                throw SmokeError("Production click did not return JSON metadata")
            }
            try require((metadata["pid"] as? NSNumber)?.int32Value == getpid(),
                        "Production click must report the target process")
            try require((metadata["window_id"] as? NSNumber)?.uint32Value == CGWindowID(window.windowNumber),
                        "Production click must report the target window")
            let outside = CGPoint(x: window.frame.maxX + outsideOffset, y: target.y)
            let (outsideStatus, _, outsideErrors) = try await runClick(
                helper: helper, work: work, label: "outside", point: outside)
            try require(outsideStatus != EXIT_SUCCESS,
                        "Production click must reject coordinates outside the target window")
            try require(String(decoding: outsideErrors, as: UTF8.self)
                            .contains("Click lies outside the target application window"),
                        "Outside click failed for an unexpected reason: " +
                        String(decoding: outsideErrors, as: UTF8.self))
            try require(view.downPoints.count == 1 && view.upPoints.count == 1,
                        "Rejected outside click must not deliver mouse events")
        } catch {
            FileHandle.standardError.write(Data("\(error.localizedDescription)\n".utf8))
            Darwin.exit(EXIT_FAILURE)
        }
    }
}
'''


@unittest.skipUnless(sys.platform == "darwin", "Requires Cocoa and WindowServer input")
class MacOSMouseInputTests(unittest.TestCase):
    def test_production_click_delivers_mouse_edges_at_target_coordinates(self):
        helper_source, separator, _ = SCRIPT.read_text(encoding="utf-8").partition("\n@main\n")
        self.assertTrue(separator, "Native helper entry point is missing")
        with tempfile.TemporaryDirectory(prefix="macos-smoke-input-") as temporary:
            work = Path(temporary)
            source, fixture = work / "input.swift", work / "input"
            production = work / "macos-smoke"
            source.write_text(helper_source + "\n" + FIXTURE, encoding="utf-8")
            for source_file, executable in ((source, fixture), (SCRIPT, production)):
                compiled = subprocess.run(
                    ["/usr/bin/xcrun", "swiftc", "-swift-version", "5", "-parse-as-library",
                     source_file, "-o", executable],
                    capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([fixture, production, work], capture_output=True, text=True,
                                    timeout=TEST_TIMEOUT)
        if result.returncode == PERMISSION_UNAVAILABLE:
            self.skipTest("Requires macOS 14+ with screen recording and input permission")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
