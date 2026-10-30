#!/usr/bin/env python3
"""Assemble GitHub Pages with verified browser and TempleOS downloads."""

import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / "site"
WEBSITE_FILES = ("index.html", "quick-guide.html", "styles.css", "site.js",
                 "hypervision-timing.html", "hypervision-timing.css", "hypervision-timing.js",
                 "templeos/index.html", "templeos/templeos.css")
TEMPLEOS_DOWNLOAD_DIRECTORY = "downloads/templeos"
TEMPLEOS_DOWNLOAD_FILES = ("RESTUNTS.ISO", "RESTUNTS_STOCK_TEMPLEOS.ZIP", "README.MD")
TEMPLEOS_CHECKSUM_FILE = "SHA256SUMS.TXT"
ASSET_SUFFIXES = {".svg", ".png", ".jpg", ".jpeg", ".webp", ".gif", ".ico",
                  ".woff", ".woff2", ".txt", ".md", ".html"}
HOSTED_GAME_FILES = ("restunts.html", "tracked.html", "THIRD-PARTY-NOTICES.txt",
                     "share/licenses/restunts/SDL-LICENSE.txt",
                     "share/licenses/restunts/Nuked-OPL2-LICENSE")
DOWNLOAD_NAME = "browser-runtime.zip"
SPEC = importlib.util.spec_from_file_location("release_packages", ROOT / "tools/scripts/release-packages.py")
PACKAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGES)


def website_files(source):
    PACKAGES.require(source.is_dir() and not source.is_symlink(), f"Missing website directory: {source}")
    files = {}
    downloads = tuple(f"{TEMPLEOS_DOWNLOAD_DIRECTORY}/{name}"
                      for name in (*TEMPLEOS_DOWNLOAD_FILES, TEMPLEOS_CHECKSUM_FILE))
    for name in (*WEBSITE_FILES, *downloads):
        path = source / name
        PACKAGES.checked_path(name)
        for parent in path.relative_to(source).parents:
            PACKAGES.require(not (source / parent).is_symlink(),
                             f"Website directory must not be a symlink: {source / parent}")
        PACKAGES.require(path.is_file() and not path.is_symlink(), f"Missing regular website file: {path}")
        files[name] = path
    verify_templeos_downloads(source / TEMPLEOS_DOWNLOAD_DIRECTORY)
    assets = source / "assets"
    PACKAGES.require(assets.is_dir() and not assets.is_symlink(), f"Missing website assets: {assets}")
    for path in sorted(assets.rglob("*")):
        PACKAGES.require(not path.is_symlink(), f"Website asset must not be a symlink: {path}")
        if path.is_dir():
            continue
        name = path.relative_to(source).as_posix()
        PACKAGES.checked_path(name)
        PACKAGES.require(path.is_file() and path.suffix.lower() in ASSET_SUFFIXES,
                         f"Unsupported website asset: {name}")
        files[name] = path
    return files


def verify_templeos_downloads(directory):
    checksums = {}
    for line in (directory / TEMPLEOS_CHECKSUM_FILE).read_text(encoding="ascii").splitlines():
        parts = line.split("  ")
        PACKAGES.require(len(parts) == 2 and PACKAGES.SHA256_PATTERN.fullmatch(parts[0]),
                         "Invalid TempleOS download checksum")
        digest, name = parts
        PACKAGES.require(name in TEMPLEOS_DOWNLOAD_FILES and name not in checksums,
                         f"Unexpected or duplicate TempleOS download: {name}")
        checksums[name] = digest
    PACKAGES.require(set(checksums) == set(TEMPLEOS_DOWNLOAD_FILES),
                     "Missing TempleOS download checksums")
    for name, digest in checksums.items():
        PACKAGES.require(PACKAGES.sha256((directory / name).read_bytes()) == digest,
                         f"TempleOS download SHA256 mismatch: {name}")


def build_pages(browser_package, output, commit, source=SITE):
    PACKAGES.require(PACKAGES.COMMIT_PATTERN.fullmatch(commit), "Source commit must be a full Git SHA-1")
    PACKAGES.require(browser_package.suffix == ".zip" and browser_package.is_file() and
                     not browser_package.is_symlink(), f"Missing regular browser ZIP: {browser_package}")
    checksum = browser_package.with_name(browser_package.name + ".sha256")
    PACKAGES.require(checksum.is_file() and not checksum.is_symlink(), f"Missing regular checksum: {checksum}")
    PACKAGES.verify_package(browser_package, "browser", commit)
    game_files, _ = PACKAGES.archive_contents(browser_package)
    landing_files = website_files(source)
    PACKAGES.require(not output.is_symlink(), f"Output must not be a symlink: {output}")
    resolved_output = output.resolve()
    resolved_source = source.resolve()
    PACKAGES.require(resolved_output != resolved_source and resolved_source not in resolved_output.parents
                     and resolved_output not in resolved_source.parents,
                     "Output must be separate from the website source directory")
    PACKAGES.require(not output.exists() or (output.is_dir() and not any(output.iterdir())),
                     f"Output directory must be empty: {output}")

    # Validation completes before any output is created. No source/game directory
    # is copied wholesale, and package paths are never passed to extractall().
    output.mkdir(parents=True, exist_ok=True)
    for name, path in landing_files.items():
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
    for name in HOSTED_GAME_FILES:
        destination = output / "game" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(game_files[name])
    archive_data = browser_package.read_bytes()
    (output / "game" / DOWNLOAD_NAME).write_bytes(archive_data)
    (output / "game" / (DOWNLOAD_NAME + ".sha256")).write_text(
        f"{PACKAGES.sha256(archive_data)}  {DOWNLOAD_NAME}\n", encoding="ascii")
    (output / ".nojekyll").touch()
    print(f"Built GitHub Pages in {output} from browser package commit {commit}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser-package", required=True, type=Path,
                        help="Complete browser ZIP with its adjacent .sha256 sidecar")
    parser.add_argument("--output", type=Path, default=ROOT / "out/github-pages",
                        help="New or empty destination directory (default: out/github-pages)")
    parser.add_argument("--commit", help="Expected package source commit (default: repository HEAD)")
    args = parser.parse_args()
    try:
        commit = args.commit or subprocess.check_output(
            ["git", "-c", f"safe.directory={ROOT}", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        build_pages(args.browser_package, args.output, commit)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError,
            zipfile.BadZipFile, tarfile.TarError) as error:
        parser.exit(1, f"Pages build failed: {error}\n")


if __name__ == "__main__":
    main()
