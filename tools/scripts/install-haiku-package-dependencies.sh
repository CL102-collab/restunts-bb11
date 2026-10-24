#!/bin/sh
# Run inside the native Haiku userspace before configuring the pinned SDL3.
set -eu

target=${1:?Usage: install-haiku-package-dependencies.sh TARGET}
host_system=$(uname -s)
host_architecture=$(uname -m)

case "$target:$host_system:$host_architecture" in
    haiku-x64:Haiku:x86_64) package_suffix= ;;
    haiku-x86:Haiku:BePC|haiku-x86:Haiku:x86) package_suffix=_x86 ;;
    *)
        echo "$target requires its matching native Haiku userspace; found $host_system $host_architecture." >&2
        exit 1
        ;;
esac

# SDL's Haiku backend uses native C++ kits and the system OpenGL library.
# The 32-bit hybrid system needs the modern GCC secondary architecture packages.
# Build tools publish unsuffixed command providers on both architectures.
pkgman install -y cmd:cmake cmd:ninja cmd:git cmd:python3 "cmd:gcc$package_suffix" \
    "cmd:g++$package_suffix" "cmd:ld$package_suffix" "cmd:pkg_config$package_suffix" \
    "cmd:readelf$package_suffix" "haiku${package_suffix}_devel" "devel:libgl$package_suffix" \
    "wxqt${package_suffix}_devel" "devel:libQt5Widgets$package_suffix" "qthaikuplugins$package_suffix"
