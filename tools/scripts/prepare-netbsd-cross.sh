#!/bin/sh
# Prepare a target-only SDK on a matching-release NetBSD amd64 build VM.
set -eu

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
target=${1:?Usage: prepare-netbsd-cross.sh TARGET SYSROOT}
sysroot=${2:?Usage: prepare-netbsd-cross.sh TARGET SYSROOT}
case "$target" in
    netbsd-armv7)
        release=10.2; architecture=earmv7hf; machine=evbarm-earmv7hf
        suffix=tgz; package_release=10.0
        ;;
    netbsd-arm64)
        release=10.2; architecture=aarch64; machine=evbarm-aarch64
        suffix=tar.xz; package_release=10.0
        ;;
    netbsd-riscv64)
        release=11.0; architecture=riscv64; machine=riscv-riscv64
        suffix=tgz; package_release=11.0
        ;;
    *) echo "Unsupported NetBSD cross target: $target" >&2; exit 1 ;;
esac
compiler_target="$architecture-unknown-netbsd$release"
set --
if [ "$target" = netbsd-armv7 ]; then
    # pkgsrc's MACHINE_ARCH name is not an LLVM architecture. Keep the EABI
    # hard-float environment explicit so target libraries use the VFP ABI.
    compiler_target="armv7-unknown-netbsd$release-eabihf"
    # Clang selects a NetBSD-specific GNU ld emulation that LLD does not
    # implement. Its generic ARM emulation preserves the input EABI flags.
    set -- -Wl,-m,armelf
fi
case "$(uname -s):$(uname -m):$(uname -r)" in
    NetBSD:amd64:"$release"|NetBSD:x86_64:"$release") ;;
    *) echo "$target requires a NetBSD $release amd64 build host." >&2; exit 1 ;;
esac
mkdir -p "$sysroot"
if [ -n "$(ls -A "$sysroot")" ]; then
    echo "Cross SDK directory must be empty: $sysroot" >&2
    exit 1
fi
sysroot=$(cd -- "$sysroot" && pwd)
export PATH="/usr/pkg/sbin:/usr/pkg/bin:$PATH"
pkg_add -u bash cmake ninja-build git-base python313 pkgconf curl \
    mozilla-rootcerts-openssl clang lld

downloads=$(mktemp -d /var/tmp/restunts-netbsd-cross.XXXXXX)
trap 'rm -rf -- "$downloads"' 0
trap 'exit 1' 1 2 15
sets_url="https://cdn.netbsd.org/pub/NetBSD/NetBSD-$release/$machine/binary/sets"
for set_name in base comp xbase xcomp; do
    curl --fail --location --retry 3 --connect-timeout 20 \
        "$sets_url/$set_name.$suffix" --output "$downloads/$set_name.$suffix"
done
(cd "$downloads" && cksum -c "$script_dir/$target-cross-sets.sha512"
    cksum -c "$script_dir/$target-sets.sha512")
for set_name in base comp xbase xcomp; do
    tar -xf "$downloads/$set_name.$suffix" -C "$sysroot"
done
python3.13 "$script_dir/netbsd-cross-packages.py" \
    "https://cdn.netbsd.org/pub/pkgsrc/packages/NetBSD/$architecture/$package_release/All" \
    "$architecture" "$sysroot" "$downloads"
# Exercise both compiler backends and the target C++ runtime before marking
# the SDK ready. This produces a target executable but never executes it.
printf '#include <signal.h>\n#include <string>\nint main() { return std::string("SDK").empty(); }\n' \
    > "$downloads/compiler-check.cpp"
if [ "$target" = netbsd-armv7 ]; then
    # LLVM 21 first stopped treating NetBSD ARM as an EHABI unwinding target;
    # earlier versions emit cleanup calls absent from its DWARF C++ runtime.
    printf '%s\n' '#define RESTUNTS_NETBSD_ARMV7_MINIMUM_CLANG_MAJOR 21' \
        '#if __clang_major__ < RESTUNTS_NETBSD_ARMV7_MINIMUM_CLANG_MAJOR' \
        '#error NetBSD ARMv7 cross builds require Clang 21 or newer' '#endif' \
        '#if !defined(__ARM_ARCH_7A__) || !defined(__ARM_PCS_VFP)' \
        '#error NetBSD ARMv7 SDK requires the ARMv7-A hard-float ABI' '#endif' \
        >> "$downloads/compiler-check.cpp"
fi
/usr/pkg/bin/clang++ --target="$compiler_target" \
    --sysroot="$sysroot" -fuse-ld=/usr/pkg/bin/ld.lld "$@" -stdlib=libstdc++ \
    -include "$script_dir/../../cmake/toolchains/netbsd-clang-compat.h" \
    "$downloads/compiler-check.cpp" -lgcc_s -o "$downloads/compiler-check"
printf '%s\n' "$target" > "$sysroot/restunts-target"
