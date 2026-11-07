"""Exercise the native capture helper against normal and modal Cocoa windows."""

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
@main
private struct WindowSelectionTest {
    private static let smallFrame = NSRect(x: 100, y: 100, width: 320, height: 200)
    private static let largeFrame = NSRect(x: 100, y: 100, width: 640, height: 480)
    private static let pollSeconds = 0.1
    private static let readySeconds = 5.0
    private static let captureSeconds = 30.0
    private static let unavailableExitCode: Int32 = 77

    @available(macOS 14.0, *)
    private static func checkAudioFailure(window: SCWindow, destination: URL) throws {
        let streamError = NSError(domain: "FixtureStreamFailure", code: Int(EXIT_FAILURE))
        let stoppingError = NSError(domain: "FixtureStopFailure", code: Int(EXIT_FAILURE))
        let recorder = AudioRecorder(destination: destination)
        let stream = SCStream(filter: SCContentFilter(desktopIndependentWindow: window),
                              configuration: SCStreamConfiguration(), delegate: recorder)
        // Queue the same delegate failure that a terminated capture stream reports.
        recorder.stream(stream, didStopWithError: streamError)
        let unstarted = AudioRecorder(destination: destination)
        for (recording, expected) in [(recorder, streamError), (unstarted, stoppingError)] {
            do {
                _ = try recording.finish(stoppingError: stoppingError)
                throw SmokeError("A failed recorder must not finish successfully")
            } catch {
                let actual = error as NSError
                try require(actual.domain == expected.domain && actual.code == expected.code,
                            "Recorder must preserve the original failure over cleanup or empty audio")
            }
        }
    }

    @MainActor
    private static func checkCapture(helper: URL, destination: URL, panel: NSPanel) async throws {
        let stdoutURL = destination.appendingPathExtension("stdout")
        let stderrURL = destination.appendingPathExtension("stderr")
        try Data().write(to: stdoutURL)
        try Data().write(to: stderrURL)
        let stdout = try FileHandle(forWritingTo: stdoutURL)
        let stderr = try FileHandle(forWritingTo: stderrURL)
        defer {
            try? stdout.close()
            try? stderr.close()
        }
        // A fresh CLI process must initialize its own WindowServer connection.
        let process = Process()
        process.executableURL = helper
        process.arguments = ["capture", String(getpid()), destination.path]
        process.standardOutput = stdout
        process.standardError = stderr
        try process.run()
        defer {
            if process.isRunning { Darwin.kill(process.processIdentifier, SIGKILL) }
        }
        let deadline = Date().addingTimeInterval(captureSeconds)
        while process.isRunning && Date() < deadline {
            // Keep the fixture's main actor responsive while its window is captured.
            try await sleepSeconds(pollSeconds)
        }
        try require(!process.isRunning, "Production capture helper timed out")
        let output = try Data(contentsOf: stdoutURL)
        let errors = try Data(contentsOf: stderrURL)
        try require(process.terminationStatus == EXIT_SUCCESS,
                    "Production capture failed (\(process.terminationStatus)): " +
                    String(decoding: output + errors, as: UTF8.self))
        guard let metadata = try JSONSerialization.jsonObject(with: output) as? [String: Any] else {
            throw SmokeError("Production capture did not return JSON metadata")
        }
        try require((metadata["pid"] as? NSNumber)?.int32Value == getpid(),
                    "Production capture must report the fixture process")
        try require((metadata["window_id"] as? NSNumber)?.uint32Value == CGWindowID(panel.windowNumber),
                    "Production capture must select the modal panel")
        guard let source = CGImageSourceCreateWithURL(destination as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
            throw SmokeError("Production capture did not write a decodable PNG")
        }
        try require(image.width > 0 && image.height > 0,
                    "Production capture must have positive image dimensions")
        try require((metadata["width"] as? NSNumber)?.intValue == image.width &&
                    (metadata["height"] as? NSNumber)?.intValue == image.height,
                    "Production capture metadata must match the decoded image")
    }

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
            try require(CommandLine.arguments.count == 3, "Missing production helper or PNG path")
            guard let window = selected else { throw SmokeError("Missing selected fixture window") }
            try checkAudioFailure(window: window,
                                  destination: URL(fileURLWithPath: CommandLine.arguments[2])
                                      .appendingPathExtension("wav"))
            try await checkCapture(helper: URL(fileURLWithPath: CommandLine.arguments[1]),
                                   destination: URL(fileURLWithPath: CommandLine.arguments[2]),
                                   panel: panel)
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
        # Keep fixture inspection and the unmodified production CLI in separate processes.
        helper_source, separator, _ = SCRIPT.read_text(encoding="utf-8").partition("\n@main\n")
        self.assertTrue(separator, "Native helper entry point is missing")
        with tempfile.TemporaryDirectory(prefix="macos-smoke-windows-") as temporary:
            work = Path(temporary)
            source, helper = work / "windows.swift", work / "windows"
            production = work / "macos-smoke"
            source.write_text(helper_source + "\n" + FIXTURE, encoding="utf-8")
            compiled = subprocess.run(["/usr/bin/xcrun", "swiftc", "-swift-version", "5",
                                       "-parse-as-library", source, "-o", helper],
                                      capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            compiled = subprocess.run(["/usr/bin/xcrun", "swiftc", "-swift-version", "5",
                                       "-parse-as-library", SCRIPT, "-o", production],
                                      capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([helper, production, work / "capture.png"],
                                    capture_output=True, text=True, timeout=TEST_TIMEOUT)
        if result.returncode == PERMISSION_UNAVAILABLE:
            self.skipTest("Requires macOS 14+ with screen recording permission")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
