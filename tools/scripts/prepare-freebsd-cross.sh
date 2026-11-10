#!/bin/sh
# Prepare a target SDK with native tools in an accelerated amd64 FreeBSD VM.
set -eu

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
target=${1:?Usage: prepare-freebsd-cross.sh TARGET SYSROOT}
sysroot=${2:?Usage: prepare-freebsd-cross.sh TARGET SYSROOT}

case "$target" in
    freebsd-arm64)
        release=14.4
        machine=arm64
        architecture=aarch64
        abi=FreeBSD:14:aarch64
        gtk_package=gtk3
        ;;
    freebsd-riscv64)
        release=15.0
        machine=riscv
        architecture=riscv64
        abi=FreeBSD:15:riscv64
        # This signed repository carries GTK2 while GTK3 still needs Rust.
        gtk_package=gtk2
        ;;
    *) echo "Unsupported FreeBSD cross target: $target" >&2; exit 1 ;;
esac

case "$(uname -s):$(uname -m):$(uname -r)" in
    FreeBSD:amd64:"$release"-RELEASE*|FreeBSD:x86_64:"$release"-RELEASE*) ;;
    *)
        echo "$target requires a FreeBSD $release amd64 build host." >&2
        exit 1
        ;;
esac
if [ "$(id -u)" -ne 0 ]; then
    echo "Run FreeBSD SDK preparation as root." >&2
    exit 1
fi
mkdir -p "$sysroot"
sysroot=$(cd -- "$sysroot" && pwd -P)
if [ -n "$(ls -A "$sysroot")" ]; then
    echo "FreeBSD SDK directory must be empty: $sysroot" >&2
    exit 1
fi

# Only host executables run during configuration and compilation. In particular,
# target package installation must never execute the foreign /bin/sh.
env IGNORE_OSVERSION=yes ASSUME_ALWAYS_YES=yes pkg install -y \
    bash cmake-core ninja git python313 pkgconf
if ! clang --print-targets | grep -q "^[[:space:]]*${architecture}[[:space:]]"; then
    echo "The host Clang does not support $architecture." >&2
    exit 1
fi
command -v ld.lld >/dev/null

download_dir=$(mktemp -d /tmp/restunts-freebsd-cross.XXXXXX)
trap 'rm -rf -- "$download_dir"' 0
trap 'exit 1' 1 2 15

download() {
    download_url=$1
    download_path=$2
    download_attempt=0
    download_attempt_limit=3
    while [ "$download_attempt" -lt "$download_attempt_limit" ]; do
        download_attempt=$((download_attempt + 1))
        if fetch -q -o "$download_path" "$download_url"; then
            return 0
        fi
        rm -f -- "$download_path"
    done
    return 1
}

archive_name="$target-base.txz"
archive_path="$download_dir/$archive_name"
sets_path="$machine/$architecture/$release-RELEASE"
if ! download "https://download.freebsd.org/releases/$sets_path/base.txz" "$archive_path"; then
    download "https://archive.freebsd.org/old-releases/$sets_path/base.txz" "$archive_path"
fi
expected_checksum=$(awk -v archive="$archive_name" '$2 == archive { print $1 }' \
    "$script_dir/freebsd-cross-sets.sha256")
case "$expected_checksum" in
    ''|*[!0-9a-f]*) echo "Missing pinned SHA256 for $archive_name." >&2; exit 1 ;;
esac
if [ "${#expected_checksum}" -ne 64 ] || \
        [ "$(sha256 -q "$archive_path")" != "$expected_checksum" ]; then
    echo "FreeBSD base archive checksum mismatch: $archive_name" >&2
    exit 1
fi
# Base sets include immutable filesystem flags that are unnecessary in an SDK.
tar -xpf "$archive_path" --no-same-owner --no-fflags -C "$sysroot"

repository_dir="$download_dir/repos"
mkdir -p "$repository_dir"
if [ "$target" = freebsd-riscv64 ]; then
    repository_index=https://github.com/anyvm-org/freebsd-pkg-repo/releases/download/idx-FreeBSD-15-riscv64
    repository_key_sha256=a9e2f84083b916f0f9f2bda18ebf9cc581cfb28aaadb6827836f1ddc672a3040
    repository_key="$download_dir/anyvm.pub"
    download "$repository_index/repo.pub" "$repository_key"
    if [ "$(sha256 -q "$repository_key")" != "$repository_key_sha256" ]; then
        echo "FreeBSD RISC-V package repository key checksum mismatch." >&2
        exit 1
    fi
    download "$repository_index/anyvm.conf" "$download_dir/anyvm.conf"
    # Keep the downloaded shard list, but generate every trust setting locally.
    # No downloaded configuration can select a different key or unsigned mode.
    python3.13 - "$download_dir/anyvm.conf" "$repository_dir/anyvm.conf" "$repository_key" <<'PY'
import pathlib
import re
import sys

source, destination, key = map(pathlib.Path, sys.argv[1:])
shards = sorted(set(re.findall(
    r'^\s*url:\s*"(https://github\.com/anyvm-org/freebsd-pkg-repo/releases/'
    r'download/pkg-FreeBSD-15-riscv64-[0-9]{3})"\s*,?\s*$',
    source.read_text(), re.MULTILINE)))
if not shards:
    raise SystemExit("The signed FreeBSD RISC-V repository has no shard URLs.")
with destination.open("w") as output:
    for number, url in enumerate(shards):
        output.write(f'anyvm-{number:03d}: {{\n'
                     f'  url: "{url}",\n'
                     '  enabled: yes,\n'
                     '  signature_type: "pubkey",\n'
                     f'  pubkey: "{key}"\n'
                     '}\n')
PY
else
    cat > "$repository_dir/FreeBSD.conf" <<EOF
FreeBSD-cross: {
  url: "https://pkg.freebsd.org/$abi/quarterly",
  enabled: yes,
  signature_type: "fingerprints",
  fingerprints: "/usr/share/keys/pkg"
}
EOF
fi

# -r relocates installation without chrooting. pkg verifies signed catalogues
# and package hashes, while disabled scripts also suppress foreign triggers.
# AnyVM's 15.x packages record 15.1 OSVERSION even on a 15.0-compatible ABI.
env IGNORE_OSVERSION=yes pkg -r "$sysroot" -R "$repository_dir" \
    -o ABI="$abi" -o RUN_SCRIPTS=false \
    install -y "$gtk_package" \
    libX11 libXext libXrandr libXcursor libXi libXfixes libXScrnSaver libXtst

# An absolute symlink must resolve into the SDK rather than the host's /lib.
# Check relative symlinks as well so library discovery cannot escape the SDK.
python3.13 - "$sysroot" <<'PY'
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
links = []
for directory, subdirectories, filenames in os.walk(root, followlinks=False):
    for name in subdirectories + filenames:
        path = pathlib.Path(directory, name)
        if path.is_symlink():
            links.append(path)
            target = os.readlink(path)
            if os.path.isabs(target):
                destination = root / target.lstrip("/")
                path.unlink()
                path.symlink_to(os.path.relpath(destination, path.parent))
for path in links:
    try:
        path.resolve().relative_to(root)
    except (ValueError, RuntimeError) as error:
        raise SystemExit(f"FreeBSD SDK symlink escapes its root: {path}") from error
PY

printf '%s\n' "$target" > "$sysroot/restunts-target"
