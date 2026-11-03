// Native evidence and input helpers for the packaged macOS GUI smoke test.
// Build with: xcrun swiftc -swift-version 5 -parse-as-library macos-smoke.swift -o macos-smoke
import AppKit
import ApplicationServices
import AudioToolbox
import AVFoundation
import CoreAudio
import CoreMedia
import CoreVideo
import Darwin
import Foundation
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers
import Vision

private enum Limits {
    static let operationSeconds = 30.0
    static let maximumRecordingSeconds = 60.0
    static let maximumKeySeconds = 5.0
    static let defaultKeySeconds = 0.1
    static let clickSeconds = 0.1
    static let activationSeconds = 0.15
    static let sampleRate = 48_000
    static let channels = 2
    static let pcmBits = 16
    static let audioVideoSize = 2
    static let minimumTextHeight: Float = 0.006
    static let nanosecondsPerSecond = 1_000_000_000.0
    static let keyCodes: [String: CGKeyCode] = [
        "return": 36, "space": 49, "up": 126, "f3": 99, "escape": 53,
    ]
}

private struct SmokeError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
    init(_ message: String) { self.message = message }
}

private func require(_ condition: Bool, _ message: String) throws {
    if !condition { throw SmokeError(message) }
}

private func jsonData(_ value: [String: Any]) throws -> Data {
    try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
}

private func printJSON(_ value: [String: Any]) throws {
    var data = try jsonData(value)
    data.append(0x0a)
    FileHandle.standardOutput.write(data)
}

private func positivePID(_ value: String) throws -> pid_t {
    guard let pid = pid_t(value), pid > 0 else { throw SmokeError("Invalid process ID") }
    return pid
}

private func boundedSeconds(_ value: String, maximum: Double) throws -> Double {
    guard let seconds = Double(value), seconds.isFinite, seconds > 0, seconds <= maximum else {
        throw SmokeError("Duration must be positive and no greater than \(maximum) seconds")
    }
    return seconds
}

private func sleepSeconds(_ seconds: Double) async throws {
    try await Task.sleep(nanoseconds: UInt64(seconds * Limits.nanosecondsPerSecond))
}

private func watchdog(seconds: Double) -> DispatchSourceTimer {
    let timer = DispatchSource.makeTimerSource(queue: .global(qos: .utility))
    timer.schedule(deadline: .now() + seconds)
    timer.setEventHandler {
        FileHandle.standardError.write(Data("macos-smoke: native operation timed out\n".utf8))
        Darwin.exit(EXIT_FAILURE)
    }
    timer.resume()
    return timer
}

private func audioScalar<T>(_ device: AudioObjectID, _ selector: AudioObjectPropertySelector,
                            initial: T) throws -> T {
    var address = AudioObjectPropertyAddress(mSelector: selector,
                                            mScope: kAudioObjectPropertyScopeGlobal,
                                            mElement: kAudioObjectPropertyElementMain)
    var value = initial
    var size = UInt32(MemoryLayout<T>.size)
    let status = withUnsafeMutableBytes(of: &value) { storage in
        AudioObjectGetPropertyData(device, &address, 0, nil, &size, storage.baseAddress!)
    }
    try require(status == noErr, "CoreAudio property \(selector) failed (\(status))")
    return value
}

private func audioName(_ device: AudioObjectID) throws -> String {
    var address = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName,
                                            mScope: kAudioObjectPropertyScopeGlobal,
                                            mElement: kAudioObjectPropertyElementMain)
    var value: Unmanaged<CFString>?
    var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
    let status = withUnsafeMutablePointer(to: &value) { storage in
        AudioObjectGetPropertyData(device, &address, 0, nil, &size, storage)
    }
    try require(status == noErr, "Cannot read CoreAudio device name (\(status))")
    guard let value = value else { throw SmokeError("Missing CoreAudio device name") }
    // CoreAudio returns CF-valued properties with a +1 retain count.
    return value.takeRetainedValue() as String
}

private func audioOutputInfo() throws -> [String: Any] {
    let device = try audioScalar(AudioObjectID(kAudioObjectSystemObject),
                                kAudioHardwarePropertyDefaultOutputDevice,
                                initial: AudioDeviceID(kAudioObjectUnknown))
    try require(device != kAudioObjectUnknown, "CoreAudio has no default output device")
    let alive = try audioScalar(device, kAudioDevicePropertyDeviceIsAlive, initial: UInt32(0))
    try require(alive != 0, "The default CoreAudio output device is not alive")
    let name = try audioName(device)
    let sampleRate = try audioScalar(device, kAudioDevicePropertyNominalSampleRate,
                                    initial: Float64(0))
    try require(sampleRate.isFinite && sampleRate > 0, "Invalid CoreAudio sample rate")
    let transport = try audioScalar(device, kAudioDevicePropertyTransportType, initial: UInt32(0))
    var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration,
                                            mScope: kAudioDevicePropertyScopeOutput,
                                            mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    var status = AudioObjectGetPropertyDataSize(device, &address, 0, nil, &size)
    try require(status == noErr && size >= MemoryLayout<AudioBufferList>.size,
                "Cannot read CoreAudio output channels (\(status))")
    let storage = UnsafeMutableRawPointer.allocate(byteCount: Int(size),
                                                   alignment: MemoryLayout<AudioBufferList>.alignment)
    defer { storage.deallocate() }
    status = AudioObjectGetPropertyData(device, &address, 0, nil, &size, storage)
    try require(status == noErr, "Cannot read CoreAudio output channel data (\(status))")
    let list = storage.assumingMemoryBound(to: AudioBufferList.self)
    let channels = UnsafeMutableAudioBufferListPointer(list).reduce(0) {
        $0 + Int($1.mNumberChannels)
    }
    try require(channels > 0, "The default CoreAudio device has no playback channels")
    return ["id": Int(device), "name": name, "channels": channels,
            "sample_rate": sampleRate, "transport_type": Int(transport)]
}

@available(macOS 14.0, *)
private func preflight() async throws -> [String: Any] {
    let screen = CGPreflightScreenCaptureAccess()
    try require(screen, "Screen recording permission is unavailable; no permission prompt was opened")
    let content = try await SCShareableContent.excludingDesktopWindows(true,
                                                                      onScreenWindowsOnly: true)
    try require(!content.displays.isEmpty, "ScreenCaptureKit cannot find an active display")
    return ["screen_capture": screen, "accessibility": AXIsProcessTrusted(),
            "post_event_access": CGPreflightPostEventAccess(),
            "output_device": try audioOutputInfo(), "displays": content.displays.count,
            "system_version": ProcessInfo.processInfo.operatingSystemVersionString]
}

@available(macOS 14.0, *)
private func visibleWindow(pid: pid_t) async throws -> SCWindow {
    try require(CGPreflightScreenCaptureAccess(), "Screen recording permission is unavailable")
    let content = try await SCShareableContent.excludingDesktopWindows(true,
                                                                      onScreenWindowsOnly: true)
    let windows = content.windows.filter {
        $0.owningApplication?.processID == pid && $0.windowLayer == 0 &&
            $0.frame.width > 0 && $0.frame.height > 0
    }
    guard let window = windows.max(by: {
        $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height
    }) else { throw SmokeError("No visible application window for PID \(pid)") }
    return window
}

@available(macOS 14.0, *)
private func capture(pid: pid_t, destination: URL) async throws -> [String: Any] {
    let window = try await visibleWindow(pid: pid)
    let filter = SCContentFilter(desktopIndependentWindow: window)
    let configuration = SCStreamConfiguration()
    let scale = CGFloat(filter.pointPixelScale)
    configuration.width = max(1, Int((window.frame.width * scale).rounded()))
    configuration.height = max(1, Int((window.frame.height * scale).rounded()))
    configuration.showsCursor = false
    if #available(macOS 14.2, *) { configuration.ignoreShadowsSingleWindow = true }
    let image = try await SCScreenshotManager.captureImage(contentFilter: filter,
                                                          configuration: configuration)
    let capturedAt = Date().timeIntervalSince1970
    guard let writer = CGImageDestinationCreateWithURL(destination as CFURL,
                                                       UTType.png.identifier as CFString, 1, nil) else {
        throw SmokeError("Cannot create PNG destination \(destination.path)")
    }
    CGImageDestinationAddImage(writer, image, nil)
    try require(CGImageDestinationFinalize(writer), "Could not finish captured PNG")
    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    request.usesLanguageCorrection = false
    request.recognitionLanguages = ["en-US"]
    request.minimumTextHeight = Limits.minimumTextHeight
    try VNImageRequestHandler(cgImage: image, options: [:]).perform([request])
    let texts: [[String: Any]] = (request.results ?? []).compactMap { observation in
        guard let result = observation.topCandidates(1).first else { return nil }
        let box = observation.boundingBox
        return ["text": result.string, "x": box.origin.x, "y": box.origin.y,
                "width": box.width, "height": box.height]
    }
    return ["pid": Int(pid), "window_id": Int(window.windowID),
            "width": image.width, "height": image.height, "captured_at": capturedAt,
            "window_x": window.frame.origin.x, "window_y": window.frame.origin.y,
            "window_width": window.frame.width, "window_height": window.frame.height,
            "texts": texts]
}

@available(macOS 14.0, *)
private final class AudioRecorder: NSObject, SCStreamOutput, SCStreamDelegate {
    let queue = DispatchQueue(label: "restunts.macos-smoke.audio")
    private let destination: URL
    private var file: AVAudioFile?
    private var failure: Error?
    private var frames: UInt64 = 0
    private var accepting = true

    init(destination: URL) {
        self.destination = destination
        super.init()
    }

    func stream(_ stream: SCStream, didStopWithError error: Error) {
        queue.async {
            self.failure = self.failure ?? error
            self.accepting = false
        }
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
                of outputType: SCStreamOutputType) {
        guard outputType == .audio, accepting else { return }
        do {
            try append(sampleBuffer)
        } catch {
            failure = failure ?? error
            accepting = false
        }
    }

    private func append(_ sampleBuffer: CMSampleBuffer) throws {
        try require(sampleBuffer.isValid && CMSampleBufferDataIsReady(sampleBuffer),
                    "ScreenCaptureKit delivered invalid audio data")
        guard var description = sampleBuffer.formatDescription?.audioStreamBasicDescription else {
            throw SmokeError("Missing captured audio format")
        }
        try require(description.mFormatID == kAudioFormatLinearPCM &&
                    description.mFormatFlags & kAudioFormatFlagIsFloat != 0 &&
                    description.mBitsPerChannel == UInt32(MemoryLayout<Float>.size * 8),
                    "ScreenCaptureKit audio is not Float32 PCM")
        try require(description.mSampleRate == Double(Limits.sampleRate) &&
                    description.mChannelsPerFrame == UInt32(Limits.channels),
                    "Unexpected captured audio sample rate or channel count")
        let frameCount = CMSampleBufferGetNumSamples(sampleBuffer)
        try require(frameCount > 0 && frameCount <= Int(UInt32.max), "Invalid audio frame count")
        guard let format = AVAudioFormat(streamDescription: &description) else {
            throw SmokeError("Cannot decode captured audio format")
        }
        try sampleBuffer.withAudioBufferList { buffers, _ in
            var channels = 0
            for buffer in buffers {
                let count = Int(buffer.mNumberChannels)
                try require(count > 0 && count <= Limits.channels,
                            "Invalid captured audio buffer channel count")
                let bytes = frameCount * count * MemoryLayout<Float>.size
                try require(Int(buffer.mDataByteSize) == bytes,
                            "Malformed captured audio buffer size")
                guard let data = buffer.mData else { throw SmokeError("Missing audio samples") }
                let samples = data.assumingMemoryBound(to: Float.self)
                for index in 0..<(frameCount * count) {
                    try require(samples[index].isFinite, "Captured audio contains non-finite samples")
                }
                channels += count
            }
            try require(channels == Limits.channels, "Incomplete captured audio channels")
            guard let pcm = AVAudioPCMBuffer(pcmFormat: format,
                                             bufferListNoCopy: buffers.unsafePointer,
                                             deallocator: nil) else {
                throw SmokeError("Cannot construct captured audio PCM buffer")
            }
            try require(Int(pcm.frameCapacity) >= frameCount, "Short captured audio PCM buffer")
            pcm.frameLength = AVAudioFrameCount(frameCount)
            if file == nil {
                let settings: [String: Any] = [
                    AVFormatIDKey: kAudioFormatLinearPCM,
                    AVSampleRateKey: Limits.sampleRate,
                    AVNumberOfChannelsKey: Limits.channels,
                    AVLinearPCMBitDepthKey: Limits.pcmBits,
                    AVLinearPCMIsFloatKey: false,
                    AVLinearPCMIsBigEndianKey: false,
                    AVLinearPCMIsNonInterleaved: false,
                ]
                file = try AVAudioFile(forWriting: destination, settings: settings,
                                       commonFormat: .pcmFormatFloat32,
                                       interleaved: format.isInterleaved)
            }
            guard let file = file else { throw SmokeError("Missing WAV output") }
            let processing = file.processingFormat
            try require(processing.commonFormat == format.commonFormat &&
                        processing.sampleRate == format.sampleRate &&
                        processing.channelCount == format.channelCount &&
                        processing.isInterleaved == format.isInterleaved,
                        "Captured audio format changed")
            try file.write(from: pcm)
            frames += UInt64(frameCount)
        }
    }

    // stopCapture completes first; this barrier drains queued callbacks and closes
    // AVAudioFile, which finalizes the WAV header before the caller reports success.
    func finish() throws -> UInt64 {
        try queue.sync {
            accepting = false
            file = nil
            if let failure = failure { throw failure }
            try require(frames > 0, "ScreenCaptureKit recorded no audio buffers")
            return frames
        }
    }
}

@available(macOS 14.0, *)
private func recordAudio(pid: pid_t, destination: URL, seconds: Double,
                         ready: URL) async throws -> [String: Any] {
    try require(!FileManager.default.fileExists(atPath: ready.path),
                "Audio readiness file already exists")
    _ = try audioOutputInfo()
    let window = try await visibleWindow(pid: pid)
    let configuration = SCStreamConfiguration()
    configuration.width = Limits.audioVideoSize
    configuration.height = Limits.audioVideoSize
    configuration.minimumFrameInterval = CMTime(value: 1, timescale: 1)
    configuration.showsCursor = false
    configuration.capturesAudio = true
    configuration.sampleRate = Limits.sampleRate
    configuration.channelCount = Limits.channels
    configuration.excludesCurrentProcessAudio = true
    let recorder = AudioRecorder(destination: destination)
    let stream = SCStream(filter: SCContentFilter(desktopIndependentWindow: window),
                          configuration: configuration, delegate: recorder)
    try stream.addStreamOutput(recorder, type: .audio, sampleHandlerQueue: recorder.queue)
    let startedAt = Date().timeIntervalSince1970
    do {
        try await stream.startCapture()
        try jsonData(["pid": Int(pid), "window_id": Int(window.windowID),
                      "started_at": startedAt, "sample_rate": Limits.sampleRate,
                      "channels": Limits.channels]).write(to: ready, options: .atomic)
        try await sleepSeconds(seconds)
        try await stream.stopCapture()
        let frames = try recorder.finish()
        return ["pid": Int(pid), "frames": frames, "sample_rate": Limits.sampleRate,
                "channels": Limits.channels, "duration": Double(frames) / Double(Limits.sampleRate),
                "started_at": startedAt, "ended_at": Date().timeIntervalSince1970]
    } catch {
        try? await stream.stopCapture()
        _ = try? recorder.finish()
        throw error
    }
}

@MainActor
private func findApplication(executable: URL) throws -> [String: Any] {
    let expected = executable.resolvingSymlinksInPath().standardizedFileURL
    let matches = NSWorkspace.shared.runningApplications.filter {
        $0.executableURL?.resolvingSymlinksInPath().standardizedFileURL == expected
    }
    try require(matches.count == 1, "Expected one running packaged game, found \(matches.count)")
    return ["pid": Int(matches[0].processIdentifier), "executable": expected.path]
}

@MainActor
private func sendKey(pid: pid_t, name: String, seconds: Double) async throws -> [String: Any] {
    try require(CGPreflightPostEventAccess() || AXIsProcessTrusted(),
                "Posting keyboard events is not authorized; no permission prompt was opened")
    guard let code = Limits.keyCodes[name] else { throw SmokeError("Unsupported key: \(name)") }
    guard let app = NSRunningApplication(processIdentifier: pid), !app.isTerminated else {
        throw SmokeError("Target application is no longer running")
    }
    try require(app.activate(options: [.activateIgnoringOtherApps]), "Cannot activate target game")
    try await sleepSeconds(Limits.activationSeconds)
    guard let down = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: true),
          let up = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: false) else {
        throw SmokeError("Cannot create keyboard events")
    }
    down.flags = []
    up.flags = []
    down.postToPid(pid)
    defer { up.postToPid(pid) }
    try await sleepSeconds(seconds)
    return ["pid": Int(pid), "key": name, "held_seconds": seconds]
}

@available(macOS 14.0, *)
@MainActor
private func click(pid: pid_t, point: CGPoint) async throws -> [String: Any] {
    try require(point.x.isFinite && point.y.isFinite, "Click coordinates must be finite")
    try require(CGPreflightPostEventAccess() || AXIsProcessTrusted(),
                "Posting mouse events is not authorized; no permission prompt was opened")
    guard let app = NSRunningApplication(processIdentifier: pid), !app.isTerminated else {
        throw SmokeError("Target application is no longer running")
    }
    try require(app.activate(options: [.activateIgnoringOtherApps]), "Cannot activate target game")
    try await sleepSeconds(Limits.activationSeconds)
    let window = try await visibleWindow(pid: pid)
    try require(window.frame.contains(point), "Click lies outside the target application window")
    guard let move = CGEvent(mouseEventSource: nil, mouseType: .mouseMoved,
                            mouseCursorPosition: point, mouseButton: .left),
          let down = CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown,
                            mouseCursorPosition: point, mouseButton: .left),
          let up = CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp,
                          mouseCursorPosition: point, mouseButton: .left) else {
        throw SmokeError("Cannot create mouse events")
    }
    move.flags = []
    down.flags = []
    up.flags = []
    try require(CGWarpMouseCursorPosition(point) == .success, "Cannot position the mouse cursor")
    move.postToPid(pid)
    down.postToPid(pid)
    defer { up.postToPid(pid) }
    try await sleepSeconds(Limits.clickSeconds)
    return ["pid": Int(pid), "x": point.x, "y": point.y, "window_id": Int(window.windowID)]
}

@main
private struct MacOSSmoke {
    @MainActor
    static func main() async {
        do {
            let arguments = Array(CommandLine.arguments.dropFirst())
            guard let command = arguments.first else { throw SmokeError("Missing helper command") }
            let timer = watchdog(seconds: Limits.operationSeconds + Limits.maximumRecordingSeconds)
            defer { timer.cancel() }
            guard #available(macOS 14.0, *) else {
                throw SmokeError("macOS 14 or later is required for the native smoke helper")
            }
            let result: [String: Any]
            switch command {
            case "preflight" where arguments.count == 1:
                result = try await preflight()
            case "find" where arguments.count == 2:
                result = try findApplication(executable: URL(fileURLWithPath: arguments[1]))
            case "capture" where arguments.count == 3:
                result = try await capture(pid: positivePID(arguments[1]),
                                           destination: URL(fileURLWithPath: arguments[2]))
            case "audio" where arguments.count == 5:
                result = try await recordAudio(pid: positivePID(arguments[1]),
                                               destination: URL(fileURLWithPath: arguments[2]),
                                               seconds: boundedSeconds(arguments[3],
                                                                       maximum: Limits.maximumRecordingSeconds),
                                               ready: URL(fileURLWithPath: arguments[4]))
            case "key" where arguments.count == 3 || arguments.count == 4:
                let seconds = try arguments.count == 4
                    ? boundedSeconds(arguments[3], maximum: Limits.maximumKeySeconds)
                    : Limits.defaultKeySeconds
                result = try await sendKey(pid: positivePID(arguments[1]), name: arguments[2],
                                           seconds: seconds)
            case "click" where arguments.count == 4:
                guard let x = Double(arguments[2]), let y = Double(arguments[3]) else {
                    throw SmokeError("Click coordinates must be numbers")
                }
                result = try await click(pid: positivePID(arguments[1]),
                                         point: CGPoint(x: x, y: y))
            default:
                throw SmokeError("Usage: macos-smoke preflight | find EXECUTABLE | capture PID PNG | " +
                                 "audio PID WAV SECONDS READY_JSON | key PID NAME [HOLD_SECONDS] | " +
                                 "click PID X Y")
            }
            try printJSON(result)
        } catch {
            FileHandle.standardError.write(Data("macos-smoke: \(error.localizedDescription)\n".utf8))
            Darwin.exit(EXIT_FAILURE)
        }
    }
}
