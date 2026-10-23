#!/usr/bin/env python3
"""Verify original replay-dump failure cleanup with independent DOS IRQ snapshots."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import zipfile


ROOT = Path(__file__).resolve().parents[2]
ASSET_EXTENSIONS = {
    ".3sh", ".drv", ".fnt", ".hig", ".kms", ".p3s", ".pes", ".plb",
    ".pre", ".pvs", ".res", ".sfx", ".trk", ".vce", ".vsh",
}

# Independent 90-byte .COM, loaded at offset 100h. Snapshot layout:
#   IN AL,21h; MOV [153h],AL              -- master PIC mask (one byte)
#   XOR AX,AX; MOV ES,AX; read ES:20h/22h -- INT 8 offset/segment (two words)
#   PUSHF; POP AX; MOV [158h],AX          -- FLAGS (one word)
# DOS INT 21h functions 3Ch/40h/3Eh write seven bytes to IRQCHK.BIN, then
# function 4Ch exits with 0 on success or 1 on create/write/short-write failure.
IRQCHECK_COM = bytes.fromhex(
    "e421a2530131c08ec026a12000a3540126a12200a356019c58a3580131c9"
    "ba4801b43ccd21721c89c3b90700ba5301b440cd21720e83f8077509b43e"
    "cd21b8004ccd21b8014ccd21"
) + b"IRQCHK.BIN\0" + bytes(7)

BATCH = """@echo off
IRQCHECK.COM
if errorlevel 1 goto failed
ren IRQCHK.BIN BEFORE.BIN
REPLDUMO.EXE "0000" 1 > DUMP.LOG
if errorlevel 1 goto expected
goto failed
:expected
IRQCHECK.COM
if errorlevel 1 goto failed
echo EXPECTED > STATUS.TXT
goto finished
:failed
echo UNEXPECTED > STATUS.TXT
:finished
echo DONE > DONE.TXT
"""


def copy_assets(source, destination):
    seen = {}
    for path in sorted(source.iterdir()):
        if not path.is_file() or path.suffix.lower() not in ASSET_EXTENSIONS:
            continue
        name = path.name.upper()
        if name in seen and path.read_bytes() != seen[name].read_bytes():
            raise ValueError(f"Conflicting DOS asset names: {path} and {seen[name]}")
        seen[name] = path
        shutil.copyfile(path, destination / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "stunts/repldumo.exe")
    parser.add_argument("--dosbox", default="dosbox-x")
    parser.add_argument("--timer-test", type=Path, help="Optional standalone DOS timer IRQ test")
    args = parser.parse_args()
    if not args.executable.is_file():
        parser.error(f"Original replay-dump executable is missing: {args.executable}")
    if args.timer_test is not None and not args.timer_test.is_file():
        parser.error(f"DOS timer IRQ test executable is missing: {args.timer_test}")

    with tempfile.TemporaryDirectory(prefix="restunts-original-exits-") as temporary:
        game = Path(temporary)

        def read(name):
            path = game / name
            return path.read_bytes() if path.is_file() else b""

        copy_assets(ROOT / "stunts", game)
        copy_assets(ROOT / "tools/scripts/cars", game)
        copy_assets(ROOT / "assets/cars", game)
        with zipfile.ZipFile(ROOT / "tools/scripts/rpls_golden/replays.zip") as archive:
            (game / "0000.RPL").write_bytes(archive.read("0000.rpl"))
        shutil.copyfile(args.executable, game / "REPLDUMO.EXE")
        (game / "IRQCHECK.COM").write_bytes(IRQCHECK_COM)
        batch = BATCH
        if args.timer_test is not None:
            shutil.copyfile(args.timer_test, game / "TIMERCHK.EXE")
            batch = batch.replace(
                "@echo off\n",
                "@echo off\nTIMERCHK.EXE > TIMER.LOG\nif errorlevel 1 goto failed\n",
                1,
            )
        (game / "CHECK.BAT").write_bytes(batch.replace("\n", "\r\n").encode())
        # DOS create cannot replace a directory. This reaches the output-open
        # error after initialization has registered the game's interrupt handlers.
        (game / "0000.BIN").mkdir()
        command = [
            args.dosbox, "-silent", "-conf", str(ROOT / "tools/scripts/dosbox.proc.conf"),
            "-noautoexec", "-set", "cpu core=dynamic", "-set", "cpu cycles=max",
            "-c", f'mount c "{game}"', "-c", "c:", "-c", "CHECK.BAT",
        ]
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with (game / "dosbox.log").open("w") as log:
            process = subprocess.Popen(command, cwd=game, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 60
                # Redirection creates the file before echo writes its contents.
                while read("DONE.TXT").strip() != b"DONE":
                    if process.poll() is not None or time.monotonic() >= deadline:
                        break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.kill()  # SIGKILL avoids DOSBox's confirmation dialog.
                process.wait()

        before, after = read("BEFORE.BIN"), read("IRQCHK.BIN")
        dump = read("DUMP.LOG").decode(errors="replace")
        failures = []
        if args.timer_test is not None:
            timer = read("TIMER.LOG").decode(errors="replace")
            if "Offline dump timer IRQ checks passed" not in timer:
                failures.append(f"Standalone DOS timer IRQ checks failed: {timer.strip()}")
        if read("DONE.TXT").strip() != b"DONE" or read("STATUS.TXT").strip() != b"EXPECTED":
            failures.append("Expected a completed run with a nonzero dump exit status")
        if "Creating output file '0000.BIN'... FAIL" not in dump:
            failures.append("The intended DOS output-create failure was not reached")
        if len(before) != 7 or len(after) != 7:
            failures.append("Missing independent IRQ snapshots")
        else:
            if before[0] != after[0] or before[0] & 1:
                failures.append("The original PIC mask was not restored with IRQ0 enabled")
            if before[1:5] != after[1:5]:
                failures.append("The original INT 8 timer vector was not restored")
            if not (struct.unpack_from("<H", before, 5)[0] & 0x200
                    and struct.unpack_from("<H", after, 5)[0] & 0x200):
                failures.append("The interrupt flag must be set before and after the dump")
        print(f"Executable SHA256: {hashlib.sha256(read('REPLDUMO.EXE')).hexdigest()}")
        print(f"IRQ snapshot before={before.hex()} after={after.hex()}")
        if failures:
            print(dump or read("dosbox.log").decode(errors="replace"))
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1
        if args.timer_test is not None:
            print("Standalone DOS timer IRQ checks passed")
        print("Original replay-dump failure cleanup checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
