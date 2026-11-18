#!/bin/sh
# Export active target libraries and development files for a Linux cross SDK.
set -eu

target=${1:?Usage: export-haiku-cross-sysroot.sh TARGET ARCHIVE}
archive=${2:?Usage: export-haiku-cross-sysroot.sh TARGET ARCHIVE}
case "$target:$(uname -s):$(uname -m)" in
    haiku-x64:Haiku:x86_64) secondary= ;;
    haiku-x86:Haiku:BePC|haiku-x86:Haiku:x86) secondary=x86/ ;;
    *) echo "$target requires its matching native Haiku userspace." >&2; exit 1 ;;
esac
wx_config="/boot/system/bin/${secondary}wx-config"
if [ ! -f "$wx_config" ]; then
    echo "Install the Haiku build dependencies before exporting the SDK." >&2
    exit 1
fi
test -d /boot/system/develop/headers
test -d /boot/system/develop/lib
test -d /boot/system/lib
archive_directory=$(dirname -- "$archive")
mkdir -p "$archive_directory"
archive="$(cd -- "$archive_directory" && pwd)/$(basename -- "$archive")"
metadata=$(mktemp -d /tmp/restunts-haiku-export.XXXXXX)
trap 'rm -rf -- "$metadata"' 0
trap 'exit 1' 1 2 15
printf '%s\n' "$target" > "$metadata/restunts-target"
uname -a > "$metadata/restunts-release"
for installed_package in /boot/system/packages/*.hpkg; do
    if [ -f "$installed_package" ]; then
        basename -- "$installed_package"
    fi
done > "$metadata/restunts-installed-packages.txt"
if ! grep -q 'r1~beta6' "$metadata/restunts-installed-packages.txt"; then
    echo "The cross SDK requires Haiku R1/beta6 packages." >&2
    exit 1
fi

# Preserve the active packagefs view, including development-library symlinks.
# Linux preparation relocates absolute links into its own sysroot. Export only
# the wx shell scripts from bin; native compilers remain in the target VM.
set -- boot/system/develop boot/system/lib
for wx_script in /boot/system/bin/wx-config* /boot/system/bin/x86/wx-config*; do
    if [ -f "$wx_script" ] || [ -L "$wx_script" ]; then
        set -- "$@" "${wx_script#/}"
    fi
done
for qt_tool in qmake moc uic rcc; do
    for tool_path in "/boot/system/bin/$qt_tool" "/boot/system/bin/x86/$qt_tool"; do
        if [ -f "$tool_path" ] || [ -L "$tool_path" ]; then
            set -- "$@" "${tool_path#/}"
        fi
    done
done
for qt_plugins in /boot/system/add-ons/Qt5 /boot/system/add-ons/x86/Qt5; do
    if [ -d "$qt_plugins" ]; then
        set -- "$@" "${qt_plugins#/}"
    fi
done
for qt_data in /boot/system/data/Qt5 /boot/system/data/x86/Qt5; do
    if [ -d "$qt_data" ]; then
        set -- "$@" "${qt_data#/}"
    fi
done
tar -I 'gzip -1' -cf "$archive" -C / "$@" -C "$metadata" \
    restunts-target restunts-release restunts-installed-packages.txt
echo "Exported $target development sysroot to $archive"
