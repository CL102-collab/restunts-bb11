#!/usr/bin/env python3
"""Test native Amiga executables using FS-UAE and open AROS ROMs.

Requires FS-UAE 3.2.x and an X11 display (or Xvfb). Test runs copy game assets,
so configuration and high scores in the source directory cannot be changed.
The default is an unexpanded A500+; --machine a4000 uses an A4000/040 with
2 MB Chip RAM and 4 MB motherboard Fast RAM. Its 68040 timing is not cycle-exact.
Use --mode capture to record an interactive game session for visual inspection;
capture mode deliberately does not claim that the game passed an automated test.
"""

import argparse
import ctypes
import ctypes.util
import math
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import zipfile


class X11Input:
    """Send real X11 key/mouse events; no xdotool or Python packages required."""

    def __init__(self, display):
        self.x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
        self.xtst = ctypes.CDLL(ctypes.util.find_library("Xtst") or "libXtst.so.6")
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
        self.x11.XStringToKeysym.restype = ctypes.c_ulong
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_uint
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self.xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self.xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
        self.display = self.x11.XOpenDisplay(display.encode())
        if not self.display:
            raise RuntimeError(f"Cannot open X11 display {display}")

    def key_state(self, name, pressed):
        keysym = self.x11.XStringToKeysym(name.encode())
        code = self.x11.XKeysymToKeycode(self.display, keysym)
        if not code:
            raise ValueError(f"Unknown X11 key name: {name}")
        self.xtst.XTestFakeKeyEvent(self.display, code, int(pressed), 0)
        self.x11.XFlush(self.display)

    def key(self, name):
        self.key_state(name, True)
        time.sleep(0.12)
        self.key_state(name, False)

    def click(self, x, y, button):
        self.xtst.XTestFakeMotionEvent(self.display, -1, x, y, 0)
        self.xtst.XTestFakeButtonEvent(self.display, button, 1, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.12)
        self.xtst.XTestFakeButtonEvent(self.display, button, 0, 0)
        self.x11.XFlush(self.display)

    def close(self):
        self.x11.XCloseDisplay(self.display)


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def program_path(value):
    found = shutil.which(value)
    if not found:
        raise ValueError(f"Executable not found: {value}")
    return Path(found).resolve()


def aros_roms(emulator, directory):
    """Load bundled ROM files explicitly: FS-UAE's 'internal' adds hidden RAM."""
    # install-amiga-emulator.sh places a newer, smaller-footprint AROS here.
    if len(emulator.parents) > 3:
        installed_rom = emulator.parents[3] / "aros.rom"
        if installed_rom.is_file():
            return installed_rom, None
    names = ("aros-amiga-m68k-rom.bin", "aros-amiga-m68k-ext.bin")
    archive = emulator.parent / "fs-uae.dat"
    if archive.is_file():
        with zipfile.ZipFile(archive) as data:
            for name in names:
                (directory / name).write_bytes(data.read(f"share/fs-uae/{name}"))
        return tuple(directory / name for name in names)
    for location in (emulator.parent / "../share/fs-uae", Path("/usr/share/fs-uae")):
        if all((location / name).is_file() for name in names):
            return tuple((location / name).resolve() for name in names)
    raise RuntimeError("Cannot locate FS-UAE's bundled AROS ROM files; use --kickstart with your own ROM")


CAR_RESOURCE_EXTENSIONS = frozenset((".res", ".pre", ".3sh", ".p3s", ".vsh", ".pvs",
                                     ".xvs", ".pes", ".esh"))


def copy_car_resources(source_directory, destination_directory):
    """Keep the packaged car folder and user overrides in the emulator disk."""
    if not source_directory.is_dir():
        return
    for source in source_directory.iterdir():
        if source.is_file() and source.suffix.lower() in CAR_RESOURCE_EXTENSIONS:
            destination_directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination_directory / source.name.upper())


def main():
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, help="Native executable; defaults to test-amiga-platform for tests, restunts for capture")
    parser.add_argument("--game-directory", type=Path, default=repository / "stunts")
    parser.add_argument("--fs-uae", default="fs-uae")
    parser.add_argument("--xvfb", default="Xvfb")
    parser.add_argument("--display", help="Use an existing X11 display instead of starting Xvfb")
    parser.add_argument("--kickstart", type=Path, help="Optional user-supplied Kickstart ROM; defaults to bundled AROS")
    parser.add_argument("--machine", choices=("a500plus", "a4000"), default="a500plus", help="A500+ or A4000/040 with 4 MB motherboard Fast RAM")
    parser.add_argument("--chip-memory", type=int, choices=(1024, 2048), help="Chip RAM in KB; defaults to 1024 for A500+, 2048 for A4000")
    parser.add_argument("--keep-cli", action="store_true", help="Keep the boot console open, retaining its shell and window memory")
    parser.add_argument("--mode", choices=("selftest", "smoke", "capture"), default="selftest")
    parser.add_argument("--args", help="Override executable arguments (quote as one string)")
    parser.add_argument("--expect", help="Required stdout marker; overrides the selected test mode's marker")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--output-directory", type=Path, help="New directory for copied assets, logs, and screenshots")
    parser.add_argument("--key", action="append", default=[], metavar="SECONDS:KEY", help="Send an X11 key, e.g. 45:Return or 60:Escape")
    parser.add_argument("--hold", action="append", default=[], metavar="SECONDS:DURATION:KEY", help="Hold a key for live driving, e.g. 180:30:Up")
    parser.add_argument("--click", action="append", default=[], metavar="SECONDS:X:Y[:BUTTON]", help="Click absolute host-window coordinates (default left button)")
    parser.add_argument("--capture-at", action="append", type=float, default=[], metavar="SECONDS", help="Save an emulator screenshot at this elapsed time")
    args = parser.parse_args()
    is_a4000 = args.machine == "a4000"
    model, cpu, chipset = ("A4000/040", "68040", "AGA") if is_a4000 else ("A500+", "68000", "ECS")
    if args.chip_memory is None:
        args.chip_memory = 2048 if is_a4000 else 1024
    motherboard_ram = 4096 if is_a4000 else 0
    if args.executable is None:
        name = "restunts" if args.mode == "capture" else "test-amiga-platform"
        args.executable = repository / "out/amiga" / name
    if not args.executable.is_file():
        parser.error(f"Native Amiga executable is missing: {args.executable}")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if args.kickstart and not args.kickstart.is_file():
        parser.error(f"Kickstart ROM is missing: {args.kickstart}")
    try:
        emulator = program_path(args.fs_uae)
        xvfb_binary = None if args.display else program_path(args.xvfb)
        events = [(float(t), "key", (key,)) for t, key in (item.split(":", 1) for item in args.key)]
        for item in args.hold:
            fields = item.split(":", 2)
            if len(fields) != 3:
                raise ValueError("--hold expects SECONDS:DURATION:KEY")
            start, duration, key = float(fields[0]), float(fields[1]), fields[2]
            if not math.isfinite(duration) or duration <= 0:
                raise ValueError("Key hold duration must be finite and positive")
            events.extend(((start, "key_state", (key, True)),
                           (start + duration, "key_state", (key, False))))
        for item in args.click:
            fields = item.split(":")
            if len(fields) not in (3, 4):
                raise ValueError("--click expects SECONDS:X:Y[:BUTTON]")
            events.append((float(fields[0]), "click", (int(fields[1]), int(fields[2]), int(fields[3]) if len(fields) == 4 else 1)))
        events.extend((value, "key", ("Print",)) for value in args.capture_at)
        if any(not math.isfinite(event[0]) or event[0] < 0 for event in events):
            raise ValueError("Event times must be finite and nonnegative")
        events.sort(key=lambda event: event[0])
    except (ValueError, OSError) as error:
        parser.error(str(error))

    if args.output_directory:
        directory = args.output_directory.resolve()
        directory.mkdir(parents=True, exist_ok=False)
    else:
        directory = Path(tempfile.mkdtemp(prefix="restunts-amiga-"))
    disk = directory / "disk"
    disk.mkdir()
    (disk / "S").mkdir()
    screenshots = directory / "screenshots"
    screenshots.mkdir()
    rom, extension = (args.kickstart.resolve(), None) if args.kickstart else aros_roms(emulator, directory)
    if args.game_directory.is_dir():
        for source in args.game_directory.iterdir():
            if source.is_file() and source.suffix.lower() not in (".exe", ".com", ".bat", ".uaem"):
                shutil.copyfile(source, disk / source.name.upper())
    copy_car_resources(repository / "assets/cars", disk / "cars")
    if args.game_directory.is_dir():
        for source in args.game_directory.iterdir():
            if source.is_dir() and source.name.lower() == "cars":
                copy_car_resources(source, disk / "cars")
    shutil.copyfile(args.executable, disk / "Restunts")
    command_args = args.args
    if command_args is None:
        command_args = "--nointro" if args.mode == "capture" else f"--amiga-{args.mode}"
    # Quote each argument for the Amiga shell; reject shell control characters.
    argv = shlex.split(command_args)
    if any(any(character in value for character in '\r\n"*') for value in argv):
        parser.error('Arguments must not contain newlines, double quotes, or Amiga shell escapes (*)')
    shell_args = " ".join(f'"{value}"' for value in argv)
    test_sequence = (
        "Stack 20480\n"
        f"Restunts {shell_args} >DH0:RESULT.TXT\n"
        "Echo DONE >DH0:DONE.TXT\n"
    )
    if args.keep_cli:
        (disk / "S/startup-sequence").write_text(test_sequence)
    else:
        # Closing the original console frees its window, bitmap, and shell stack.
        # The background script retains completion reporting after the game exits.
        (disk / "S/test-sequence").write_text(test_sequence)
        (disk / "S/startup-sequence").write_text(
            "Stack 20480\nRun >DH0:RUN.TXT Execute S:test-sequence\nEndCLI\n"
        )
    configuration = directory / f"{args.machine}.fs-uae"
    extension_configuration = f"kickstart_ext_file = {extension}\n" if extension else ""
    configuration.write_text(
        "[fs-uae]\n"
        f"amiga_model = {model}\ncpu = {cpu}\nchip_memory = {args.chip_memory}\n"
        f"motherboard_ram = {motherboard_ram}\nfast_memory = 0\nzorro_iii_memory = 0\nslow_memory = 0\n"
        "jit_compiler = 0\n"
        f"kickstart_file = {rom}\n"
        f"{extension_configuration}"
        f"hard_drive_0 = {disk}\nhard_drive_0_label = RESTUNTS\n"
        f"base_dir = {directory / 'fs-uae'}\nlogs_dir = {directory / 'logs'}\n"
        f"screenshots_output_dir = {screenshots}\nscreenshots_output_prefix = amiga\n"
        "floppy_drive_0 =\nfloppy_drive_1 =\naudio_driver = dummy\nfloppy_drive_volume = 0\n"
        "joystick_port_0 = mouse\njoystick_port_1 = none\n"
        "automatic_input_grab = 0\nwindow_width = 800\nwindow_height = 600\nfullscreen = 0\nvideo_sync = off\n"
    )
    environment = dict(os.environ)
    environment["LD_LIBRARY_PATH"] = str(emulator.parent) + (":" + environment["LD_LIBRARY_PATH"] if environment.get("LD_LIBRARY_PATH") else "")
    xvfb = None
    process = None
    inputs = None
    print(f"{model} / {cpu} / {chipset} / {args.chip_memory} KB Chip RAM / "
          f"{motherboard_ram} KB motherboard Fast RAM; artifacts: {directory}", flush=True)
    if is_a4000:
        print("68040 uses FS-UAE's fastest interpreter mode (JIT off); timings are not a 25 MHz hardware benchmark.", flush=True)
    try:
        display = args.display
        if not display:
            number = next(number for number in range(90, 190) if not Path(f"/tmp/.X11-unix/X{number}").exists() and not Path(f"/tmp/.X{number}-lock").exists())
            display = f":{number}"
            with (directory / "xvfb.log").open("w") as log:
                xvfb = subprocess.Popen([str(xvfb_binary), display, "-screen", "0", "1024x768x24", "-nolisten", "tcp"], stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 5
            while not Path(f"/tmp/.X11-unix/X{number}").exists():
                if xvfb.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"Xvfb did not start; see {directory / 'xvfb.log'}")
                time.sleep(0.05)
        environment["DISPLAY"] = display
        inputs = X11Input(display)
        started = time.monotonic()
        with (directory / "emulator.log").open("w") as log:
            process = subprocess.Popen([str(emulator), str(configuration)], cwd=directory, env=environment, stdout=log, stderr=subprocess.STDOUT)
        while process.poll() is None:
            elapsed = time.monotonic() - started
            while events and elapsed >= events[0][0]:
                _, action, parameters = events.pop(0)
                getattr(inputs, action)(*parameters)
            if (disk / "DONE.TXT").exists() or elapsed >= args.timeout:
                break
            time.sleep(0.1)
        if process.poll() is None:
            inputs.key("Print")
            time.sleep(0.4)
    finally:
        stop(process)
        if inputs:
            inputs.close()
        stop(xvfb)

    result_file = disk / "RESULT.TXT"
    result = result_file.read_text(errors="replace") if result_file.exists() else ""
    print(result.strip() or "No program output was produced.")
    emulator_log = directory / "logs/fs-uae.log.txt"
    if emulator_log.is_file() and "Non-autoconfig RAM" in emulator_log.read_text(errors="replace"):
        print("FAILED: emulator added unexpected non-autoconfig RAM.")
        return 1
    if args.mode == "capture" and not args.expect:
        print(f"Capture finished; inspect {screenshots}. This is not an automated pass.")
        return 0 if list(screenshots.glob("*.png")) else 1
    expected = args.expect or f"AMIGA {args.mode.upper()} PASS"
    if (disk / "DONE.TXT").exists() and expected in result:
        print(f"Native {cpu} test passed in {time.monotonic() - started:.1f}s.")
        return 0
    print(f"FAILED: expected completed program containing {expected!r}; inspect {directory}.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
