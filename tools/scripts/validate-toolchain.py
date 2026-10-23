#!/usr/bin/env python3
"""Compare DOS builds with archived physics and rebuilt incremental renderer references."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[2]
ORACLES = ROOT / "tools/oracles/borland"
ASSET_EXTENSIONS = {
    ".3sh", ".drv", ".fnt", ".hig", ".kms", ".p3s", ".pes", ".plb",
    ".pre", ".pvs", ".res", ".sfx", ".trk", ".vce", ".vsh",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_assets(source, destination):
    seen = {}
    for path in sorted(source.iterdir()):
        if not path.is_file() or path.suffix.lower() not in ASSET_EXTENSIONS:
            continue
        name = path.name.upper()
        if name in seen and digest(path) != digest(seen[name]):
            raise ValueError(f"Conflicting DOS asset names: {path} and {seen[name]}")
        seen[name] = path
        shutil.copyfile(path, destination / name)


def copy_oracle_cache(source, game, manifest):
    cached = json.loads((source / "inputs.json").read_text())
    for key in ("corpus_sha256", "camera", "target", "replays", "assets"):
        if cached[key] != manifest[key]:
            raise ValueError(f"Oracle cache inputs differ: {key}")
    for name in ("REPLDUMO.EXE", "PIXLDUMO.EXE"):
        if cached["executables"][name] != manifest["executables"][name]:
            raise ValueError(f"Oracle cache executable differs: {name}")
    files = {p.name.upper(): p for p in (source / "game").iterdir() if p.is_file()}
    copied = {}
    for replay in manifest["replays"]:
        for extension in ("BIN", "PDO"):
            name = f"{Path(replay).stem}.{extension}".upper()
            if name not in files or name + ".PENDING" in files:
                continue
            path = files[name]
            if path.stat().st_size == 0:
                continue
            destination = game / name
            shutil.copyfile(path, destination)
            copied[name] = digest(destination)
    manifest["oracle_cache"] = {"directory": str(source), "outputs": copied}
    print(f"Reused {len(copied)} completed oracle outputs with matching inputs", flush=True)


def prepare(args):
    # Require a new directory: cached dumps must never hide a failed execution.
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "driver.pid").write_text(str(os.getpid()) + "\n")
    game = args.output / "game"
    game.mkdir()
    copy_assets(ROOT / "stunts", game)
    copy_assets(ROOT / "tools/scripts/cars", game)
    copy_assets(ROOT / "assets/cars", game)
    for line in (ORACLES / "SHA256SUMS").read_text().splitlines():
        expected, name = line.split()
        source = ORACLES / name
        if digest(source) != expected:
            raise ValueError(f"Archived oracle checksum mismatch: {source}")
        if name.upper() == "REPLDUMO.EXE":
            shutil.copyfile(source, game / name.upper())
    for name in ("REPLDUMP.EXE", "PIXLDUMP.EXE", "PIXLDUMO.EXE"):
        candidates = [p for p in args.candidate_directory.iterdir()
                      if p.is_file() and p.name.upper() == name]
        if len(candidates) != 1:
            raise ValueError(f"Expected one {name} in {args.candidate_directory}")
        shutil.copyfile(candidates[0], game / name)

    corpus = ROOT / "tools/scripts/rpls_golden/replays.zip"
    with zipfile.ZipFile(corpus) as archive:
        names = sorted(name for name in archive.namelist()
                       if Path(name).suffix.lower() == ".rpl")
        if not 1 <= args.count <= len(names):
            raise ValueError(f"Replay count must be between 1 and {len(names)}")
        selected = [names[index * len(names) // args.count]
                    for index in range(args.count)]
        if len({name.upper() for name in selected}) != len(selected):
            raise ValueError("Selected replay names collide under DOS casing")
        for name in selected:
            if Path(name).name != name:
                raise ValueError(f"Expected a top-level replay filename: {name}")
            (game / name).write_bytes(archive.read(name))

    manifest = {
        "corpus": str(corpus.relative_to(ROOT)),
        "corpus_sha256": digest(corpus),
        "selection": "ordinal sort; index * corpus_count // selected_count",
        "corpus_count": len(names),
        "selected_count": len(selected),
        "camera": 2,
        "target": 0,
        "executables": {p.name: digest(p) for p in sorted(game.glob("*.EXE"))},
        "replays": {name: digest(game / name) for name in selected},
        "assets": {p.name: digest(p) for p in sorted(game.iterdir())
                   if p.suffix.lower() in ASSET_EXTENSIONS},
    }
    if args.oracle_cache:
        copy_oracle_cache(args.oracle_cache.resolve(), game, manifest)
    (args.output / "inputs.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.output / "replays.txt").write_bytes(("\r\n".join(selected) + "\r\n").encode())
    return game


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="New directory for isolated inputs, dumps and reports")
    parser.add_argument("--candidate-directory", type=Path, default=ROOT / "stunts")
    parser.add_argument("--oracle-cache", type=Path,
                        help="Prior validation directory with identical oracle inputs")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--workers", type=int, choices=range(1, 65), default=4,
                        metavar="1-64")
    parser.add_argument("--physics-timeout", type=int, default=60)
    parser.add_argument("--renderer-timeout", type=int, default=240)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.candidate_directory = args.candidate_directory.resolve()
    if args.physics_timeout <= 0 or args.renderer_timeout <= 0:
        parser.error("Execution timeouts must be positive")
    game = prepare(args)
    print(f"Prepared {args.count} replays in {game}", flush=True)
    if args.prepare_only:
        return 0

    runner = args.output / "runner"
    project = ROOT / "tools/scripts/dumpsrv/dumpsrv/dumpsrv.csproj"
    subprocess.run(["dotnet", "publish", str(project), "--configuration", "Release",
                    "--output", str(runner)], check=True)
    common = ["dotnet", str(runner / "dumpsrv.dll")]
    result = subprocess.run(common + [
        "run", "-GameDirectory", str(game), "-OutputDirectory", str(args.output / "results"),
        "-DosBoxConfigPath", str(ROOT / "tools/scripts/dosbox.proc.conf"),
        "-PartitionCount", str(args.workers), "-DosBoxTimeoutSeconds", str(args.physics_timeout),
        "-RendererTimeoutSeconds", str(args.renderer_timeout), "-RendererTestPercentage", "100",
    ])
    # Merge also verifies exact completed replay identities and complete coverage.
    merged = subprocess.run(common + [
        "merge", "-ReplayDirectory", str(game), "-ResultsDirectory", str(args.output / "results"),
        "-ShardCount", "1", "-RendererTestPercentage", "100",
        "-OutputFile", str(args.output / "partitions_all.txt"),
        "-SummaryFile", str(args.output / "summary.md"),
    ])
    return result.returncode or merged.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"Validation failed: {error}", file=sys.stderr)
        sys.exit(1)
