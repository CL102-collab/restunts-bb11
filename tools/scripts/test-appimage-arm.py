#!/usr/bin/env python3
"""Test an archived ARM AppImage with its pinned historical libc under QEMU user mode."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import tempfile
import threading
import time


ABI_SPEC = importlib.util.spec_from_file_location(
    "appimage_abi_check", Path(__file__).with_name("check-appimage-abi.py"))
ABI = importlib.util.module_from_spec(ABI_SPEC)
ABI_SPEC.loader.exec_module(ABI)
TARGET_CPUS = {
    "linux-appimage-armv7": "cortex-a7",
    "linux-appimage-arm64": "cortex-a53",
}
BATCH_TIMEOUT = 180
STARTUP_TIMEOUT = 12
AUDIO_TIMEOUT = 60
KILL_TIMEOUT = 5
PCM_BYTES = 65536
BMP_BYTES = 65078
BMP_SHA256 = "0713b4ca9a6db8ba1ebe8696f3baa9a97798b02b69556e2d1862a073a6fbfe80"
HASH_CHUNK_BYTES = 1024 * 1024
HOST_LIBRARY_VARIABLES = frozenset({"LD_LIBRARY_PATH", "LD_PRELOAD", "LIBRARY_PATH",
                                    "APPDIR", "APPIMAGE_EXTRACT_AND_RUN"})


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(HASH_CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def host_environment():
    return {key: value for key, value in os.environ.items()
            if key not in HOST_LIBRARY_VARIABLES and not key.startswith("QEMU_")}


def target_environment(environment, values):
    require(all("," not in value for value in values.values()), "Invalid QEMU environment value")
    return dict(environment, QEMU_SET_ENV=",".join(key + "=" + value
                                                 for key, value in values.items()))


def resolve_dependency(reported, sysroot, libraries):
    absolute = Path(reported)
    require(absolute.is_absolute(), f"Relative target dependency: {reported}")
    prefixed = sysroot / reported.lstrip("/")
    resolved = (prefixed if prefixed.exists() else absolute).resolve(strict=True)
    require(resolved.is_relative_to(sysroot) or resolved.is_relative_to(libraries),
            f"Dependency escaped pinned SDK/AppImage: {resolved}")
    return resolved


def terminate(process):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            process.wait()
            return
        try:
            process.wait(timeout=KILL_TIMEOUT)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def run(command, log, environment, timeout=BATCH_TIMEOUT, expect_timeout=False, cwd=None):
    with log.open("w") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                   env=environment, cwd=cwd, start_new_session=True)
        try:
            result = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            terminate(process)
            require(expect_timeout, f"Timed out; see {log}")
            return log.read_text(errors="replace")
        require(not expect_timeout, f"Game exited before its menu deadline ({result}); see {log}")
        require(result == 0, f"Command exited {result}; see {log}")
    return log.read_text(errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=TARGET_CPUS)
    parser.add_argument("archive", type=Path)
    parser.add_argument("game_data", type=Path)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    args.archive = args.archive.resolve(strict=True)
    args.game_data = args.game_data.resolve(strict=True)
    args.results = args.results.resolve()
    args.results.mkdir(parents=True, exist_ok=True)
    target_abi = ABI.TARGETS[args.target]
    interpreter, baseline = target_abi.interpreter, target_abi.distribution
    glibc = ".".join(map(str, target_abi.glibc_version))
    cpu = TARGET_CPUS[args.target]
    evidence = {"target": args.target, "baseline": baseline, "glibc": glibc,
                "archive_sha256": checksum(args.archive), "qemu_cpu": cpu,
                "kernel_tested": False, "fuse_tested": False, "hardware_tested": False,
                "passed": False}
    environment = host_environment()
    tool_root = Path(environment.get("RESTUNTS_TOOLCHAIN_ROOT",
                    str(Path(environment.get("RUNNER_TEMP", "/tmp")) / "restunts-toolchains")))
    try:
        env_file = tool_root / "appimage-sysroots" / (args.target + ".env")
        exports = subprocess.check_output(
            ["bash", "-c", 'set -eu; source "$1"; env -0', "bash", str(env_file)],
            env=environment)
        sdk_environment = dict(item.split("=", 1) for item in exports.decode().split("\0") if item)
        sysroot = Path(sdk_environment["RESTUNTS_APPIMAGE_SYSROOT"]).resolve(strict=True)
        requested_qemu = sdk_environment["RESTUNTS_APPIMAGE_QEMU"]
        qemu = shutil.which(requested_qemu, path=sdk_environment.get("PATH"))
        require(qemu is not None, f"Missing QEMU executable: {requested_qemu}")
        cpu = sdk_environment.get("RESTUNTS_APPIMAGE_QEMU_CPU", cpu)
        evidence["qemu_cpu"] = cpu
        loader = sysroot / interpreter.lstrip("/")
        require(loader.is_file() and loader.resolve().is_relative_to(sysroot),
                f"Missing historical loader inside SDK: {loader}")
        loader_header = subprocess.check_output(["readelf", "-hW", str(loader)],
                                               env=dict(environment, LC_ALL="C"), text=True)
        for field, expected in (("Class", target_abi.elf_class), ("Machine", target_abi.machine)):
            require(re.search(rf"^\s*{field}:\s*{re.escape(expected)}\s*$", loader_header, re.MULTILINE),
                    f"Wrong loader {field}: {loader}")
        evidence["loader_sha256"] = checksum(loader)
        evidence["qemu_version"] = subprocess.check_output([qemu, "--version"], env=environment,
                                                          text=True).splitlines()[0]
        with tempfile.TemporaryDirectory(prefix="restunts-arm-compat-") as temporary:
            work = Path(temporary)
            package, extraction, data = (work / name for name in ("package", "extraction", "data"))
            package.mkdir()
            extraction.mkdir()
            run(["tar", "-xzf", str(args.archive), "-C", str(package)],
                args.results / "archive.log", environment)
            image = package / "bin/restunts.AppImage"
            ABI.check_elf(image, args.target, static=True)
            qemu_command = [qemu, "-cpu", cpu, "-L", str(sysroot)]
            run(qemu_command + [str(image), "--appimage-extract"],
                args.results / "extract.log", environment, cwd=extraction)
            appdir = extraction / "squashfs-root"
            require((appdir / "AppRun").is_file(), "The archived AppImage did not extract AppRun")
            libraries = appdir / "usr/lib"
            guest_variables = {"LD_LIBRARY_PATH": str(libraries),
                               "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
                               "LANG": "C", "LC_ALL": "C",
                               "RESTUNTS_ALSA_CONFIG_DIR": str(appdir / "usr/share/alsa"),
                               "XLOCALEDIR": str(appdir / "usr/share/X11/locale")}

            def guest_environment(extra=None):
                values = dict(guest_variables, **(extra or {}))
                return target_environment(environment, values)

            binaries = [appdir / "usr/bin" / name for name in
                        ("restunts", "restunts-launcher", "genghost-gui", "repldump", "pixldump", "genghost",
                         "restunts-gtk-modules")]
            shared = sorted(path for path in libraries.rglob("*")
                            if path.is_file() and (path.name.endswith(".so") or ".so." in path.name))
            require(shared, "No bundled shared libraries found")
            libc = None
            for index, binary in enumerate(binaries + shared):
                headers = ABI.check_elf(binary, args.target)
                if binary in binaries:
                    require(f"Requesting program interpreter: {interpreter}]" in headers,
                            f"Unexpected program interpreter: {binary}")
                # Let QEMU place an executable's interpreter normally. Running
                # the loader as the main program can put it inside a large
                # non-PIE executable's fixed address range on 32-bit ARM.
                if binary in binaries:
                    linkage_command = qemu_command + [str(binary)]
                    linkage_environment = guest_environment({"LD_TRACE_LOADED_OBJECTS": "1"})
                else:
                    linkage_command = qemu_command + [str(loader), "--list", str(binary)]
                    linkage_environment = guest_environment()
                linkage = run(linkage_command, args.results / f"linkage-{index:03d}.log",
                              linkage_environment)
                require("not found" not in linkage, f"Unresolved dependencies for {binary}")
                for soname, reported in re.findall(r"([^\s]+)\s+=>\s+(/[^\s]+)", linkage):
                    resolved = resolve_dependency(reported, sysroot, libraries)
                    if soname == "libc.so.6":
                        require(resolved.is_relative_to(sysroot), "glibc must come from the old SDK")
                        libc = resolved
            require(libc is not None, "The target loader did not resolve libc.so.6")
            libc_version = run(qemu_command + [str(libc)], args.results / "libc.log", environment)
            require(re.search(r"release version\s+" + re.escape(glibc) + r"(?:[.,\s]|$)", libc_version),
                    f"The SDK is not the promised glibc {glibc}; see libc.log")
            evidence["libc_sha256"] = checksum(libc)
            game = appdir / "usr/bin/restunts"
            for option, expected in (("--help", "Usage: restunts"), ("--version", "Version "),
                                     ("--licenses", "Nuked OPL2 Lite")):
                output = run(qemu_command + [str(game), option],
                             args.results / (option.lstrip("-") + ".log"), guest_environment())
                require(expected in output, f"Unexpected {option} output")
            run(qemu_command + [str(appdir / "usr/bin/genghost"), "--help"],
                args.results / "genghost.log", guest_environment())
            shutil.copytree(args.game_data, data)
            for path in data.rglob("*"):
                path.chmod(path.stat().st_mode | 0o200)
            run(qemu_command + [str(game), "--data-dir", str(data), "--nointro"],
                args.results / "startup.log", guest_environment(), timeout=STARTUP_TIMEOUT,
                expect_timeout=True)
            bmp = data / "DEFAULT.2.0.5.PDD.bmp"
            bmp.unlink(missing_ok=True)
            run(qemu_command + [str(appdir / "usr/bin/pixldump"), "--data-dir", str(data),
                                "DEFAULT.RPL", "2", "0", "5"],
                args.results / "render.log", guest_environment())
            require(bmp.is_file() and bmp.stat().st_size == BMP_BYTES and
                    checksum(bmp) == BMP_SHA256, "Replay BMP differs from the original renderer")
            shutil.copyfile(bmp, args.results / bmp.name)
            evidence["bmp_sha256"] = checksum(bmp)
            capture_audio(qemu_command + [str(game), "--data-dir", str(data), "--nointro"],
                          guest_environment, work, args.results)
            evidence["pcm_sha256"] = checksum(args.results / "audio.raw")
            evidence["pcm_bytes"] = PCM_BYTES
            evidence["passed"] = True
            print(f"RESTUNTS_COMPATIBILITY_PASSED target={args.target} userspace={baseline} glibc={glibc}")
    except Exception as error:
        evidence["error"] = str(error)
        raise
    finally:
        (args.results / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")


def capture_audio(command, guest_environment, work, results):
    fifo = work / "audio.pipe"
    os.mkfifo(fifo)
    holding = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    configuration = work / "alsa.conf"
    configuration.write_text('pcm.!default {\n type file\n slave.pcm { type null }\n'
                             f' file "{fifo}"\n format "raw"\n}}\n')
    stop, captured = threading.Event(), threading.Event()

    def receive():
        descriptor = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
        sample = bytearray()
        try:
            while not stop.is_set():
                if not select.select([descriptor], [], [], 0.25)[0]:
                    continue
                block = os.read(descriptor, PCM_BYTES - len(sample))
                if captured.is_set():
                    continue
                sample.extend(block)
                if len(sample) == PCM_BYTES:
                    if any(sample):
                        (results / "audio.raw").write_bytes(sample)
                        captured.set()
                    sample.clear()
        finally:
            os.close(descriptor)

    reader = threading.Thread(target=receive, daemon=True)
    reader.start()
    process = None
    try:
        with (results / "audio.log").open("w") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       env=guest_environment({"SDL_AUDIODRIVER": "alsa",
                                                              "ALSA_CONFIG_PATH": str(configuration)}),
                                       start_new_session=True)
            deadline = time.monotonic() + AUDIO_TIMEOUT
            while not captured.wait(timeout=0.25) and process.poll() is None:
                if time.monotonic() >= deadline:
                    break
            require(captured.is_set(), f"Bundled ALSA produced no nonzero PCM; see {results / 'audio.log'}")
    finally:
        if process is not None:
            terminate(process)
        os.close(holding)
        stop.set()
        reader.join(timeout=KILL_TIMEOUT)
    require(process.returncode in (0, -signal.SIGTERM, -signal.SIGKILL),
            f"ALSA game failed with exit {process.returncode}; see {results / 'audio.log'}")


if __name__ == "__main__":
    main()
