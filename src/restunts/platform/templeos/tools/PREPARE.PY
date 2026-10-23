#!/usr/bin/env python3
"""Prepare HolyC sources, optionally with private uppercase game data."""

import argparse
from pathlib import Path
import shutil


PORT = Path(__file__).resolve().parent.parent
BUNDLED_CARS = PORT.parents[3] / "assets/cars"
SOURCE_EXTENSIONS = {".HC", ".HH"}
CAR_EXTENSIONS = {".RES", ".3SH", ".VSH"}
DATA_EXTENSIONS = {
    ".RES", ".PRE", ".PVS", ".VSH", ".XVS", ".PES", ".ESH", ".3SH", ".P3S",
    ".TRK", ".RPL", ".HIG", ".CFG", ".DAT", ".DRV", ".VCE", ".SFX",
    ".KMS", ".FNT", ".PLB",
}


def collect_resources(directory, extensions):
    assets = {}
    for source in sorted(directory.iterdir()):
        if source.is_file() and source.suffix.upper() in extensions:
            name = source.name.upper()
            if name in assets:
                if source.read_bytes() != assets[name].read_bytes():
                    raise ValueError("Different files have the same uppercase name: " + name)
            else:
                assets[name] = source
    return assets


def prepare(data, destination):
    data = data.resolve(strict=True) if data is not None else None
    destination = destination.resolve()
    if data is not None and not data.is_dir():
        raise ValueError("--data must name the original game directory")
    if destination.exists():
        raise ValueError("Choose a new output directory; existing packages are never overwritten")
    if data is not None and (destination == data or data in destination.parents):
        raise ValueError("The output must be outside the original game directory")
    assets = collect_resources(data, DATA_EXTENSIONS) if data is not None else {}
    if data is not None and not any(name in assets for name in ("MISC.RES", "MISC.PRE")):
        raise ValueError("The data directory has no MISC.RES or MISC.PRE")
    car_directories = [source for source in data.iterdir()
                       if source.is_dir() and source.name.upper() == "CARS"] if data else []
    if len(car_directories) > 1:
        raise ValueError("Directories have the same uppercase name: CARS")
    if car_directories:
        assets.update({"CARS/" + name: source for name, source in
                       collect_resources(car_directories[0], DATA_EXTENSIONS).items()})
    bundled_cars = collect_resources(BUNDLED_CARS, CAR_EXTENSIONS)
    if not bundled_cars:
        raise ValueError("No bundled car resources found")
    code = [source for source in PORT.rglob("*")
            if source.is_file() and source.suffix in SOURCE_EXTENSIONS
            and not {"tools", "package"}.intersection(source.relative_to(PORT).parts)
            and source.name != "REPLAY_EXPECTED.HH"
            and not source.name.startswith("TEST_")]
    if not code:
        raise ValueError("No HolyC sources found; run TRANSLATE.PY first")
    (destination / "PORT").mkdir(parents=True)
    (destination / "DATA").mkdir()
    for source in code:
        target = destination / "PORT" / source.relative_to(PORT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    shutil.copyfile(PORT / "README.TXT", destination / "PORT" / "README.TXT")
    (destination / "PORT/CARS").mkdir()
    for name, source in bundled_cars.items():
        shutil.copyfile(source, destination / "PORT/CARS" / name)
    for name, source in assets.items():
        target = destination / "DATA" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    print(f"Prepared {len(code)} HolyC files, {len(bundled_cars)} bundled car resources, "
          f"and {len(assets)} private game resources in {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path,
                        help="Optional original game directory for a private local copy")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.data, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
