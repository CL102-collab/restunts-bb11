#!/usr/bin/env python3
"""Create and verify complete release archives without original game data."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import subprocess
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = "PACKAGE-MANIFEST.json"
MANIFEST_VERSION = 1
README = "README-PACKAGE.txt"
DOS_EXECUTABLES = ("restunts", "restunto", "repldump", "repldumo", "pixldump", "pixldumo")
NATIVE_EXECUTABLES = ("restunts", "repldump", "pixldump")
TARGETS = {
    "android-armv7": ".zip",
    "android-arm64": ".zip",
    "dos16": ".zip",
    "dos32": ".zip",
    "freebsd-x64": ".tar.gz",
    "haiku-x64": ".tar.gz",
    "haiku-x86": ".tar.gz",
    "linux-arm32": ".tar.gz",
    "linux-arm64": ".tar.gz",
    "linux-x86": ".tar.gz",
    "linux-x86-no-sse2": ".tar.gz",
    "linux-x64": ".tar.gz",
    "netbsd-x64": ".tar.gz",
    "netbsd-x86": ".tar.gz",
    "netbsd-x86-no-sse2": ".tar.gz",
    "openbsd-x64": ".tar.gz",
    "windows-arm64": ".zip",
    "windows-x86": ".zip",
    "windows-x86-no-sse2": ".zip",
    "windows-x64": ".zip",
    "macos-universal": ".tar.gz",
    "browser": ".zip",
}
BSD_TARGETS = {
    "freebsd-x64": "FreeBSD x86-64",
    "openbsd-x64": "OpenBSD x86-64",
    "netbsd-x64": "NetBSD x86-64",
    "netbsd-x86": "NetBSD 32-bit x86 with SSE2",
    "netbsd-x86-no-sse2": "NetBSD 32-bit x86; SSE2 instructions are disabled",
}
HAIKU_TARGETS = {"haiku-x64": "Haiku x86-64", "haiku-x86": "Haiku 32-bit x86 with SSE2"}
ELF_TARGETS = {**BSD_TARGETS, **HAIKU_TARGETS}
ANDROID_TARGETS = {"android-armv7": "armeabi-v7a", "android-arm64": "arm64-v8a"}
ANDROID_TOOLCHAIN_PROPERTIES = ROOT / "android/toolchain.properties"
ANDROID_MINIMUM_KEYS = {"android-armv7": "ANDROID_ARMV7_MIN_SDK", "android-arm64": "ANDROID_ARM64_MIN_SDK"}
ANDROID_VERSIONS = {21: "5.0", 28: "9"}
ANDROID_ADAPTIVE_ICON_API = 26
ANDROID_FALLBACK_ICON = "res/mipmap-anydpi-v21/ic_launcher.xml"
ANDROID_APK = "bin/restunts.apk"
ANDROID_LIBRARIES = ("libSDL3.so", "libmain.so", "libnuked-opl2.so")
ANDROID_LICENSES = ("THIRD-PARTY-NOTICES.txt", "SDL-LICENSE.txt", "Nuked-OPL2-LICENSE.txt",
                    "stb-LICENSE.txt", "libvpx-LICENSE.txt", "libvpx-PATENTS.txt", "nestegg-LICENSE.txt")
ANDROID_DEX_PATTERN = re.compile(r"classes(?:[2-9]|[1-9][0-9]+)?\.dex")
ANDROID_SIGNATURE_PATTERN = re.compile(r"META-INF/[A-Za-z0-9_-]+\.(?:SF|RSA|DSA|EC)")
ANDROID_ASSET_METADATA = {"assets/menus/manifest.json", "assets/opponents/game/manifest.json",
                          "assets/skyboxes/manifest.json", "assets/music/README.md"}
ANDROID_RESOURCES = {"res/drawable/ic_launcher_foreground.xml", "res/drawable/tv_banner.xml",
                     "res/mipmap-anydpi-v26/ic_launcher.xml"}
ANDROID_METADATA = {"AndroidManifest.xml", "resources.arsc", "META-INF/MANIFEST.MF",
                    "META-INF/com/android/build/gradle/app-metadata.properties",
                    "META-INF/version-control-info.textproto"}
ELF_MAGIC = b"\x7fELF"
ELF_CLASS_32 = 1
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_CLASS_OFFSET = 4
ELF_DATA_OFFSET = 5
ELF_MACHINE_X86 = 3
ELF_MACHINE_X64 = 62
ELF_MACHINE_ARM = 40
ELF_MACHINE_AARCH64 = 183
ELF_MACHINE_OFFSET = 18
ELF_TYPE_OFFSET = 16
ELF_TYPE_SHARED = 3
ELF_HEADER_SIZES = {ELF_CLASS_32: 52, ELF_CLASS_64: 64}
ELF_ARCHITECTURES = {
    "x86": (ELF_CLASS_32, ELF_MACHINE_X86),
    "x64": (ELF_CLASS_64, ELF_MACHINE_X64),
    "armv7": (ELF_CLASS_32, ELF_MACHINE_ARM),
    "arm64": (ELF_CLASS_64, ELF_MACHINE_AARCH64),
}
ELF_FILES = {f"bin/{name}" for name in NATIVE_EXECUTABLES} | {"lib/libnuked-opl2.so"}
ORIGINAL_SUFFIXES = {
    ".res", ".pre", ".3sh", ".vsh", ".pvs", ".rpl", ".trk", ".vce", ".drv", ".fnt",
    ".cod", ".dif", ".hig", ".bin", ".bni", ".pdo", ".pdd",
}
NUKED_FILES = ("opl2.c", "opl2.h", "CMakeLists.txt", "LICENSE", "README.md")
SKYBOX_THEMES = ("desert", "tropical", "alpine", "city", "country")
SKYBOX_IMAGES = ("scen", "sce2", "sce3", "sce4")
OPPONENT_COUNT = 6
MENU_BACKGROUNDS = ("main", "showroom", "opponent")
MUSIC_TRACKS = ("titl", "slct", "over", "vict")
SDL_DECODER_LICENSES = ("stb-LICENSE", "libvpx-LICENSE", "libvpx-PATENTS", "nestegg-LICENSE")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
DOS_RUNTIME_NAME_PATTERN = re.compile(r"[A-Za-z0-9_-]{1,8}(?:\.[A-Za-z0-9_-]{1,3})?")
ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)
FILE_MODE = 0o644
EXECUTABLE_MODE = 0o755


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def archive_name(target):
    return f"restunts-{target}{TARGETS[target]}"


def checked_path(name):
    path = PurePosixPath(name)
    require(name and path.parts and not path.is_absolute() and ".." not in path.parts and
            ":" not in name and not any(ord(character) < 32 for character in name) and
            "\\" not in name and str(path) == name,
            f"Unsafe or noncanonical package path: {name!r}")
    require(path.suffix.lower() not in ORIGINAL_SUFFIXES,
            f"Original game content or regression output is forbidden: {name}")
    require("stunts" not in (part.lower() for part in path.parts),
            f"Original game directory is forbidden: {name}")
    return path


def optional_music_files(target):
    if target == "dos16" or target in ANDROID_TARGETS:
        return set()
    if target == "browser":
        prefix = "share/restunts/wasm-relink/data/assets"
    else:
        prefix = "bin"
    # Match CMake's named optional music. Present source files must survive
    # installation and archiving; an absent replacement keeps original playback.
    return {f"{prefix}/music/{track}.ogg" for track in MUSIC_TRACKS
            if (ROOT / "assets/music" / f"{track}.ogg").is_file()}


def nuked_source_files():
    prefix = "share/restunts/nuked-opl2-lite"
    required = {f"{prefix}/{name}" for name in NUKED_FILES}
    required |= {f"{prefix}/nuked-build-info.txt", f"{prefix}/build-context/CMakeLists.txt",
                 f"{prefix}/build-context/cmake/nuked-build-info.txt.in"}
    required |= {f"{prefix}/build-context/" + path.relative_to(ROOT).as_posix()
                 for path in (ROOT / "cmake/toolchains").glob("*.cmake")}
    return required


def android_minimum_sdk(target):
    key = ANDROID_MINIMUM_KEYS[target]
    settings = ANDROID_TOOLCHAIN_PROPERTIES.read_text(encoding="utf-8")
    match = re.search(r"^" + re.escape(key) + r"=(\d+)\s*$", settings, re.MULTILINE)
    require(match is not None, f"Missing or invalid Android toolchain property: {key}")
    minimum = int(match.group(1))
    require(minimum in ANDROID_VERSIONS, f"Unsupported Android package minimum API: {minimum}")
    return minimum


def android_required_resources(target):
    required = ANDROID_RESOURCES.copy()
    if android_minimum_sdk(target) < ANDROID_ADAPTIVE_ICON_API:
        required.add(ANDROID_FALLBACK_ICON)
    return required


def android_required_assets():
    required = {f"assets/licenses/{name}" for name in ANDROID_LICENSES}
    required |= {f"assets/skyboxes/{theme}-{image}.png"
                 for theme in SKYBOX_THEMES for image in SKYBOX_IMAGES}
    required |= {f"assets/menus/{background}.png" for background in MENU_BACKGROUNDS}
    required |= {f"assets/opponents/game/opp{opponent}.png"
                 for opponent in range(1, OPPONENT_COUNT + 1)}
    required |= {f"assets/music/{track}.ogg" for track in MUSIC_TRACKS
                 if (ROOT / "assets/music" / f"{track}.ogg").is_file()}
    return required


def validate_elf(target, name, content, elf_class, machine):
    require(len(content) >= ELF_HEADER_SIZES[elf_class] and content.startswith(ELF_MAGIC),
            f"Missing or truncated ELF header: {name}")
    require(content[ELF_CLASS_OFFSET] == elf_class and
            content[ELF_DATA_OFFSET] == ELF_DATA_LSB and
            struct.unpack_from("<H", content, ELF_MACHINE_OFFSET)[0] == machine,
            f"Wrong ELF architecture for {target}: {name}")


def validate_android_apk(target, content):
    abi = ANDROID_TARGETS[target]
    native = {f"lib/{abi}/{name}" for name in ANDROID_LIBRARIES}
    required = {"AndroidManifest.xml", "classes.dex", "resources.arsc"} | native
    required |= android_required_resources(target)
    required |= android_required_assets()
    allowed_assets = android_required_assets() | ANDROID_ASSET_METADATA
    allowed_assets |= {f"assets/skyboxes/{theme}.png" for theme in SKYBOX_THEMES}
    files = {}
    names = set()
    with zipfile.ZipFile(io.BytesIO(content)) as apk:
        for entry in apk.infolist():
            name = entry.filename
            checked_path(name[:-1] if entry.is_dir() else name)
            require(name not in names, f"Duplicate APK member: {name}")
            names.add(name)
            mode = entry.external_attr >> 16
            require(not stat.S_IFMT(mode) or stat.S_ISREG(mode) or
                    (entry.is_dir() and stat.S_ISDIR(mode)), f"Unsupported APK member: {name}")
            require(not entry.flag_bits & 1, f"Encrypted APK member: {name}")
            if entry.is_dir():
                continue
            if name.startswith("lib/"):
                require(name in native, f"Unexpected Android ABI or native library for {target}: {name}")
            elif name.startswith("assets/"):
                require(name in allowed_assets, f"Unexpected APK asset: {name}")
            else:
                require(name in ANDROID_METADATA or name in ANDROID_RESOURCES or
                        name == ANDROID_FALLBACK_ICON or
                        ANDROID_DEX_PATTERN.fullmatch(name) or
                        ANDROID_SIGNATURE_PATTERN.fullmatch(name), f"Unexpected APK member: {name}")
            files[name] = apk.read(entry)
            require(files[name], f"Empty APK member: {name}")
    missing = required - files.keys()
    require(not missing, f"{target}: missing APK members: {', '.join(sorted(missing))}")
    elf_class, machine = ELF_ARCHITECTURES[target.split("-")[1]]
    for name in native:
        validate_elf(target, name, files[name], elf_class, machine)
        require(struct.unpack_from("<H", files[name], ELF_TYPE_OFFSET)[0] == ELF_TYPE_SHARED,
                f"Expected an Android ELF shared library: {name}")


def required_files(target):
    required = {README} | optional_music_files(target)
    if target != "dos16":
        required.add("THIRD-PARTY-NOTICES.txt")
        required |= {f"share/licenses/restunts/{name}" for name in SDL_DECODER_LICENSES}
    if target in ANDROID_TARGETS:
        required |= {ANDROID_APK, "share/docs/restunts/android.md",
                     "share/licenses/restunts/SDL-LICENSE.txt",
                     "share/licenses/restunts/Nuked-OPL2-LICENSE"}
        return required | nuked_source_files()
    if target == "browser":
        required |= {"restunts.html", "wasm.md", "THIRD-PARTY-NOTICES.txt",
                     "share/licenses/restunts/SDL-LICENSE.txt",
                     "share/licenses/restunts/Nuked-OPL2-LICENSE"}
        relink = "share/restunts/wasm-relink"
        required |= {f"{relink}/{name}" for name in (
            "CMakeLists.txt", "wasm-link.cmake", "wasm-relink.md", "shell.html",
            "nuked-build-info.txt", "lib/librestunts_game.a", "lib/librestunts_webm.a",
            "lib/libSDL3.a", "objects/main.c.o")}
        required |= {f"{relink}/nuked-opl2-lite/{name}" for name in NUKED_FILES}
        required |= {f"{relink}/data/assets/skyboxes/{theme}-{image}.png"
                     for theme in SKYBOX_THEMES for image in SKYBOX_IMAGES}
        required |= {f"{relink}/data/assets/menus/{background}.png"
                     for background in MENU_BACKGROUNDS}
        required |= {f"{relink}/data/assets/opponents/game/opp{opponent}.png"
                     for opponent in range(1, OPPONENT_COUNT + 1)}
        return required
    names = DOS_EXECUTABLES if target == "dos16" else NATIVE_EXECUTABLES
    extension = ".exe" if target.startswith(("dos", "windows")) else ""
    prefix = "" if target == "dos16" else "bin/"
    required |= {f"{prefix}{name}{extension}" for name in names}
    if target == "dos16":
        return required
    required |= {"share/docs/restunts/sdl3.md", "share/licenses/restunts/SDL-LICENSE.txt"}
    if target in HAIKU_TARGETS:
        required.add("share/docs/restunts/haiku.md")
    required |= {f"bin/menus/{background}.png" for background in MENU_BACKGROUNDS}
    required |= {f"bin/skyboxes/sky{theme}-{image}.png"
                 for theme in range(len(SKYBOX_THEMES)) for image in range(len(SKYBOX_IMAGES))}
    opponent_directory = "opponent" if target == "dos32" else "opponents"
    required |= {f"bin/{opponent_directory}/game/opp{opponent}.png"
                 for opponent in range(1, OPPONENT_COUNT + 1)}
    if target == "dos32":
        return required | {"bin/CWSDPMI.EXE", "share/licenses/restunts/CWSDPMI.DOC"}
    required |= {"THIRD-PARTY-NOTICES.txt", "share/licenses/restunts/Nuked-OPL2-LICENSE"}
    required |= nuked_source_files()
    if target.startswith("windows"):
        required.add("bin/nuked-opl2.dll")
    elif target == "macos-universal":
        required |= {"lib/libnuked-opl2.dylib", "run-restunts.sh"}
    else:
        required.add("lib/libnuked-opl2.so")
    return required


def validate_contents(target, files, modes):
    required = required_files(target)
    missing = required - files.keys()
    require(not missing, f"{target}: missing packaged files: {', '.join(sorted(missing))}")
    for name, content in files.items():
        path = checked_path(name)
        require(content, f"Empty package file: {name}")
        if target == "dos32" and name.startswith("bin/"):
            require(all(DOS_RUNTIME_NAME_PATTERN.fullmatch(part) for part in path.parts),
                    f"DOS runtime path must use 8.3 names: {name}")
        require(name in required, f"Unexpected package file: {name}")
        if target.startswith("dos"):
            require("nuked" not in name.lower(), f"DOS package must omit Nuked: {name}")
    if target == "browser":
        require(any(name.startswith("share/restunts/wasm-relink/objects/") for name in files),
                "Browser package is missing application entry objects for relinking")
    if TARGETS[target] == ".tar.gz":
        executables = {f"bin/{name}" for name in NATIVE_EXECUTABLES}
        if target == "macos-universal":
            executables.add("run-restunts.sh")
        for name in executables:
            require(modes[name] & stat.S_IXUSR, f"Missing executable permission: {name}")
    if target in ELF_TARGETS:
        elf_class, machine = ELF_ARCHITECTURES[target.split("-")[1]]
        for name in ELF_FILES:
            validate_elf(target, name, files[name], elf_class, machine)
    if target in ANDROID_TARGETS:
        validate_android_apk(target, files[ANDROID_APK])


def readme(target, commit):
    lines = [f"Restunts package: {target}", f"Source commit: {commit}", "",
             "Supply your own original Broderbund Stunts 1.1 game data (12 Feb. 1991).",
             "Original game content is not included. Keep this package's files together.", ""]
    if target in ANDROID_TARGETS:
        minimum = android_minimum_sdk(target)
        version = ANDROID_VERSIONS[minimum]
        lines += [f"Requires Android {version} (API {minimum}) or newer; runs in landscape only.",
                  f"This APK contains only {ANDROID_TARGETS[target]} native libraries.",
                  "Install bin/restunts.apk, then use Choose game folder or Import Stunts ZIP.",
                  "Original resources and custom game files use one selected public folder.",
                  "ZIP import extracts into the exact destination folder you select.",
                  "The app manages its private working cache and copies completed game writes",
                  "back to your selected folder, including tracks, replays and screenshots.",
                  "Enhanced artwork/music and dependency notices are included in the APK.",
                  "Release workflow APKs use the project's persistent release signing key.",
                  "PR/local debug APKs use debug keys, which may differ between builds.",
                  "Android updates require the same signing key. A different key may require",
                  "uninstalling the old app. Folders selected through the system picker survive.",
                  "The app-owned TV fallback media folder, including game files, saves and source",
                  "ZIPs, is deleted on uninstall. Back up those files outside it beforehand.",
                  "Uninstalling removes private preferences, cache and unexported pending writes.",
                  "See share/docs/restunts/android.md for import, controls and rebuild/signing",
                  "details. Nuked source and build context are in share/restunts/nuked-opl2-lite."]
        if target == "android-armv7":
            lines += [f"Suitable for Samsung Galaxy S5 SM-G900F running Android {version} or newer."]
        else:
            lines += ["Requires a 64-bit ARM Android operating system."]
    elif target == "browser":
        lines += ["Open restunts.html in your browser and choose your game-data folder.",
                  "The HTML runs offline by itself. Keep the licenses and Nuked source/relink",
                  "kit with redistributed packages; see wasm.md for instructions."]
    elif target == "dos16":
        lines += ["Copy the executable files into your original game's directory.",
                  "Run restunts.exe in DOS or DOSBox with core=dynamic and cycles=max.",
                  "The other executables are original/rebuilt game and regression tools."]
    else:
        executable = "restunts.exe" if target.startswith(("windows", "dos")) else "restunts"
        lines += [f"Run bin/{executable} --data-dir PATH_TO_YOUR_STUNTS_FOLDER.",
                  "Enhanced skyboxes, opponent portraits, and menu backgrounds are included",
                  "beside the game.",
                  "See share/docs/restunts/sdl3.md for controls and platform details."]
    if target == "macos-universal":
        lines += ["macOS 11.0 or newer; Intel and Apple Silicon are in the same binary.",
                  "You can also use: bash run-restunts.sh --data-dir PATH_TO_YOUR_STUNTS_FOLDER"]
    if target.startswith("windows-x86"):
        lines += ["Requires Windows XP SP2 or newer (the pinned SDL system API minimum)."]
    if target in ELF_TARGETS:
        lines += [f"Built for {ELF_TARGETS[target]}."]
    if target in BSD_TARGETS:
        lines += ["Packages for different BSD operating systems are not interchangeable."]
    if target in HAIKU_TARGETS:
        lines += ["See share/docs/restunts/haiku.md for Haiku setup and VM testing."]
    if target == "dos32":
        lines += ["You can instead copy the entire contents of bin (including artwork folders)",
                  "into your original game's directory, then run restunts.exe there.",
                  "", "CWSDPMI release 7 is included unmodified in bin/CWSDPMI.EXE.",
                  "You have the right to receive its source code and binary updates:",
                  "https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7s.zip",
                  "https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip",
                  "Its redistribution terms are in share/licenses/restunts/CWSDPMI.DOC."]
    return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def create_package(args):
    require(COMMIT_PATTERN.fullmatch(args.commit), "Source commit must be a full Git SHA-1")
    head = subprocess.check_output(["git", "-c", f"safe.directory={ROOT}", "rev-parse", "HEAD"],
                                   cwd=ROOT, text=True).strip()
    require(head == args.commit, "Package source commit differs from the checked-out commit")
    files = {}
    modes = {}
    for source in sorted(args.runtime.rglob("*")):
        require(not source.is_symlink(), f"Package symlinks are not supported: {source}")
        if source.is_dir():
            continue
        require(source.is_file(), f"Unsupported package entry: {source}")
        name = source.relative_to(args.runtime).as_posix()
        require(name not in (README, MANIFEST), f"Generated package file already exists: {name}")
        files[name] = source.read_bytes()
        modes[name] = EXECUTABLE_MODE if source.stat().st_mode & stat.S_IXUSR else FILE_MODE
    files[README] = readme(args.target, args.commit)
    modes[README] = FILE_MODE
    validate_contents(args.target, files, modes)
    manifest = {
        "version": MANIFEST_VERSION,
        "target": args.target,
        "source_commit": args.commit,
        "source_ref": os.environ.get("GITHUB_REF", ""),
        "github_run_id": os.environ.get("GITHUB_RUN_ID", ""),
        "github_run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", ""),
        "files": [{"path": name, "sha256": sha256(data), "size": len(data), "mode": modes[name]}
                  for name, data in sorted(files.items())],
    }
    files[MANIFEST] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    modes[MANIFEST] = FILE_MODE
    args.directory.mkdir(parents=True, exist_ok=True)
    archive = args.directory / archive_name(args.target)
    require(not archive.exists(), f"Refusing to overwrite release archive: {archive}")
    if TARGETS[args.target] == ".zip":
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, ZIP_EPOCH)
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | modes[name]) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                output.writestr(info, data)
    else:
        with archive.open("wb") as raw, gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as output:
                for name, data in sorted(files.items()):
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    info.mode = modes[name]
                    output.addfile(info, io.BytesIO(data))
    checksum = f"{sha256(archive.read_bytes())}  {archive.name}\n"
    archive.with_name(archive.name + ".sha256").write_text(checksum, encoding="ascii")
    verify_package(archive, args.target, args.commit)
    print(f"Created and verified {archive}")


def archive_contents(archive):
    files = {}
    modes = {}

    def add(name, data, mode):
        checked_path(name)
        require(name not in files, f"Duplicate archive entry: {name}")
        files[name] = data
        modes[name] = stat.S_IMODE(mode)

    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as source:
            for entry in source.infolist():
                mode = entry.external_attr >> 16
                require(stat.S_ISREG(mode), f"Unsupported ZIP member: {entry.filename}")
                add(entry.filename, source.read(entry), mode)
    else:
        with tarfile.open(archive, "r:gz") as source:
            for entry in source:
                require(entry.isfile(), f"Unsupported tar member: {entry.name}")
                add(entry.name, source.extractfile(entry).read(), entry.mode)
    return files, modes


def verify_package(archive, target, commit, run_id=None, run_attempt=None):
    expected_checksum = f"{sha256(archive.read_bytes())}  {archive.name}\n"
    actual_checksum = archive.with_name(archive.name + ".sha256").read_text(encoding="ascii")
    require(actual_checksum == expected_checksum, f"SHA256 mismatch or malformed checksum: {archive.name}")
    files, modes = archive_contents(archive)
    require(MANIFEST in files, f"Missing {MANIFEST} in {archive.name}")
    manifest = json.loads(files.pop(MANIFEST))
    require(manifest.get("version") == MANIFEST_VERSION, "Unsupported package manifest version")
    require(manifest.get("target") == target, f"Wrong target in {archive.name}")
    require(manifest.get("source_commit") == commit, f"Wrong source commit in {archive.name}")
    if run_id is not None:
        require(manifest.get("github_run_id") == run_id, f"Wrong workflow run in {archive.name}")
    if run_attempt is not None:
        require(manifest.get("github_run_attempt") == run_attempt, f"Wrong run attempt in {archive.name}")
    inventory = manifest.get("files")
    require(isinstance(inventory, list), f"Missing file inventory in {archive.name}")
    names = []
    for entry in inventory:
        name = entry["path"]
        require(name in files, f"Manifest file is absent from archive: {name}")
        require(SHA256_PATTERN.fullmatch(entry["sha256"]), f"Invalid manifest hash: {name}")
        require(sha256(files[name]) == entry["sha256"] and len(files[name]) == entry["size"],
                f"File differs from manifest: {name}")
        require(modes[name] == entry["mode"], f"File permissions differ from manifest: {name}")
        names.append(name)
    require(len(names) == len(set(names)), "Duplicate file in package manifest")
    require(set(names) == files.keys(), f"Unlisted files in {archive.name}")
    validate_contents(target, files, modes)


def verify_directory(args):
    require(COMMIT_PATTERN.fullmatch(args.commit), "Source commit must be a full Git SHA-1")
    targets = TARGETS if args.all else (args.target,)
    if args.all:
        expected = {name for target in targets for name in
                    (archive_name(target), archive_name(target) + ".sha256")}
        actual = {path.name for path in args.directory.iterdir()}
        require(actual == expected, f"Release file set differs: missing={sorted(expected - actual)}, "
                                   f"unexpected={sorted(actual - expected)}")
    for target in targets:
        archive = args.directory / archive_name(target)
        require(archive.is_file() and not archive.is_symlink(), f"Missing regular archive: {archive}")
        checksum = archive.with_name(archive.name + ".sha256")
        require(checksum.is_file() and not checksum.is_symlink(), f"Missing regular checksum: {checksum}")
        verify_package(archive, target, args.commit, args.run_id, args.run_attempt)
    print(f"Verified {len(targets)} complete package(s) from {args.commit}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="Archive an installed Runtime component")
    create.add_argument("--target", required=True, choices=TARGETS)
    create.add_argument("--runtime", required=True, type=Path)
    create.add_argument("--directory", required=True, type=Path)
    create.add_argument("--commit", required=True)
    verify = commands.add_parser("verify", help="Verify downloaded release artifacts before publishing")
    group = verify.add_mutually_exclusive_group(required=True)
    group.add_argument("--target", choices=TARGETS)
    group.add_argument("--all", action="store_true")
    verify.add_argument("--directory", required=True, type=Path)
    verify.add_argument("--commit", required=True)
    verify.add_argument("--run-id")
    verify.add_argument("--run-attempt")
    args = parser.parse_args()
    try:
        if args.command == "create":
            create_package(args)
        else:
            verify_directory(args)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile, tarfile.TarError) as error:
        parser.exit(1, f"Package validation failed: {error}\n")


if __name__ == "__main__":
    main()
