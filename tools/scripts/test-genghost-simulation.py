#!/usr/bin/env python3
"""Compare branching ghost simulations with the independent repldump driver."""

import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


GAMESTATE_SIZE = 1120
REPLAY_INPUT_OFFSET = 1828
PHYSICS_MODES = (
    (),
    ("--pg:off",),
    ("--lc:off",),
    ("--lcb:off",),
    ("--owoot",),
    ("--owoot", "--pg:off", "--lc:off", "--lcb:off"),
)


def main():
    simulation, repldump, data_directory, corpus = (Path(argument).resolve() for argument in sys.argv[1:])
    # Geometry tests are independent of the proprietary game resources.
    subprocess.run([str(simulation), "--geometry-only"], check=True, timeout=30)
    available = {path.name.upper(): path for path in data_directory.iterdir()} if data_directory.is_dir() else {}
    required = (("GAME.RES", "GAME.PRE"), ("CARLM02.RES", "CARLM02.PRE"),
                ("STLM02.P3S", "STLM02.3SH"))
    missing = [" or ".join(names) for names in required if not any(name in available for name in names)]
    if missing:
        print("SKIP: original Stunts resources are needed: " + "; ".join(missing), flush=True)
        return 77
    with zipfile.ZipFile(corpus) as archive:
        fixtures = [("0000", archive.read("0000.rpl"))]
        controller_fixtures = []
        # Exercise guidance identity through rigid/flexible power gear and the
        # modern jump-heavy Jaguar lap, when their optional assets are installed.
        for name, car in (("0027", "VETT"), ("0066", "PMIN"), ("3856", "JAGU")):
            car_data = any(f"CAR{car}.{suffix}" in available for suffix in ("RES", "PRE"))
            car_shape = any(f"ST{car}.{suffix}" in available for suffix in ("P3S", "3SH"))
            if car_data and car_shape:
                fixtures.append((name, archive.read(name + ".rpl")))
        # Fastest verified archive laps for these tracks. Preserve each car's
        # actual dimensions and native stunt orientation in the restriction
        # checks: loop rotations 0/256, slalom stones, clear UD core, and
        # horizontal corks in both orientations and traversal directions.
        for name, car, prefix, model in (("1572", "ZGT3", 838, 27),
                                         ("o0226", "VETT", 469, 34),
                                         ("o0226", "VETT", 780, 27),
                                         ("1531", "ZMP4", 1499, 33),
                                         ("a0405", "PMIN", 643, 35),
                                         ("0750", "JAGU", 61, 35),
                                         ("3649", "DMCB", 499, 35),
                                         ("2015", "ANSX", 343, 35)):
            car_data = any(f"CAR{car}.{suffix}" in available for suffix in ("RES", "PRE"))
            car_shape = any(f"ST{car}.{suffix}" in available for suffix in ("P3S", "3SH"))
            if car_data and car_shape:
                controller_fixtures.append((name, archive.read(name + ".rpl"), prefix, model))
    if "DEFAULT.RPL" in available:
        fixtures.append(("DEFAULT", available["DEFAULT.RPL"].read_bytes()))
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with tempfile.TemporaryDirectory(prefix="genghost-") as directory:
        temporary = Path(directory)
        if "DEFAULT.TRK" in available:
            # DEFAULT.RPL embeds a different course. Initialize the branch
            # regression from the actual DEFAULT track, using installed LM02.
            header = bytearray(fixtures[0][1][:26])
            struct.pack_into("<H", header, 24, 1)
            source = temporary / "dualway.RPL"
            source.write_bytes(header + available["DEFAULT.TRK"].read_bytes() + b"\0")
            for switches in PHYSICS_MODES:
                subprocess.run([str(simulation), "--data-dir", str(data_directory), str(source),
                                "--dualway-check", *switches],
                               env=environment, check=True, timeout=120)
        for fixture_name, recording, prefix, model in controller_fixtures:
            source = temporary / "source.RPL"
            source.write_bytes(recording)
            subprocess.run([str(simulation), "--data-dir", str(data_directory), str(source),
                            "--controller-check", str(prefix), str(model)],
                           env=environment, check=True, timeout=120)
            print(f"{fixture_name}: winning-prefix controller restrictions passed", flush=True)
        for fixture_name, recording in fixtures:
            source = temporary / "source.RPL"
            source.write_bytes(recording)
            count = struct.unpack_from("<H", recording, 24)[0]
            for switches in PHYSICS_MODES:
                output = temporary / "ghost.RPL"
                command = [str(simulation), "--data-dir", str(data_directory),
                           str(source), str(output), *switches]
                subprocess.run(command, env=environment, check=True, timeout=120)
                normalized = output.read_bytes()
                assert struct.unpack_from("<H", normalized, 22)[0] == 20
                assert len(normalized) == REPLAY_INPUT_OFFSET + count
                assert normalized[26:REPLAY_INPUT_OFFSET] == recording[26:REPLAY_INPUT_OFFSET]
                assert normalized[REPLAY_INPUT_OFFSET:] == recording[REPLAY_INPUT_OFFSET:]
                subprocess.run([str(repldump), "--data-dir", str(data_directory),
                                str(output), "1", *switches],
                               env=environment, check=True, timeout=120)
                actual = output.with_suffix(".RPL.states").read_bytes()
                reference = output.with_suffix(".BNI").read_bytes()
                assert len(actual) == 2 + count * GAMESTATE_SIZE
                if actual != reference:
                    offset = next((index for index, pair in enumerate(zip(actual, reference))
                                   if pair[0] != pair[1]), min(len(actual), len(reference)))
                    raise AssertionError(f"{fixture_name} {switches}: simulation differs from repldump "
                                         f"at byte {offset}, frame {(offset - 2) // GAMESTATE_SIZE}")
                mode = " ".join(switches) or "legacy physics"
                print(f"{fixture_name}: {count} identical replay states; {mode}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
