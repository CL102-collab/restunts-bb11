#!/bin/sh
# Run as root in the target BSD userspace, before Bash is available.
set -eu

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
target=${1:?Usage: install-bsd-package-dependencies.sh TARGET}
host_system=$(uname -s)
host_architecture=$(uname -m)
netbsd_release=10.2

install_netbsd_x11() {
    if [ -f /usr/X11R7/include/X11/Xlib.h ] && [ -f /usr/X11R7/lib/libX11.so ]; then
        return
    fi
    if [ "$(uname -r)" != "$netbsd_release" ]; then
        echo "Install matching NetBSD xbase and xcomp sets before building SDL3 on this release." >&2
        exit 1
    fi
    case "$host_architecture" in
        amd64|x86_64) sets_architecture=amd64; sets_suffix=tar.xz; checksum_file=netbsd-x64-sets.sha512 ;;
        i386) sets_architecture=i386; sets_suffix=tgz; checksum_file=netbsd-x86-sets.sha512 ;;
    esac
    download_dir=$(mktemp -d /tmp/restunts-netbsd-x11.XXXXXX)
    trap 'rm -rf -- "$download_dir"' 0
    trap 'exit 1' 1 2 15
    sets_url="https://cdn.netbsd.org/pub/NetBSD/NetBSD-$netbsd_release/$sets_architecture/binary/sets"
    for set_name in xbase xcomp; do
        set_archive="$set_name.$sets_suffix"
        curl --fail --location --retry 3 --connect-timeout 20 \
            "$sets_url/$set_archive" --output "$download_dir/$set_archive"
        # The i386 manifest also contains the chroot's compiler and base sets.
        expected_checksum=$(sed -n "/^SHA512 ($set_archive) = /p" "$script_dir/$checksum_file")
        if [ -z "$expected_checksum" ]; then
            echo "Missing pinned checksum for $set_archive." >&2
            exit 1
        fi
        (cd "$download_dir" && printf '%s\n' "$expected_checksum" | cksum -c)
    done
    for set_name in xbase xcomp; do
        tar -xpf "$download_dir/$set_name.$sets_suffix" -C /
    done
    rm -rf -- "$download_dir"
    trap - 0 1 2 15
}

case "$target:$host_system:$host_architecture" in
    freebsd-x64:FreeBSD:amd64|freebsd-x64:FreeBSD:x86_64)
        pkg install -y bash cmake ninja git python313 pkgconf \
            gtk3 libX11 libXext libXrandr libXcursor libXi libXfixes libXScrnSaver libXtst
        ;;
    openbsd-x64:OpenBSD:amd64|openbsd-x64:OpenBSD:x86_64)
        # OpenBSD ships pkg-config in base; its Python 3 branch is lang/python/3.
        pkg_add -I bash cmake ninja git 'python%3' gtk+3
        if [ ! -f /usr/X11R6/include/X11/Xlib.h ]; then
            echo "Install the OpenBSD xbase and xshare sets before building SDL3." >&2
            exit 1
        fi
        ;;
    netbsd-x64:NetBSD:amd64|netbsd-x64:NetBSD:x86_64|\
    netbsd-x86:NetBSD:i386|netbsd-x86-no-sse2:NetBSD:i386)
        export PATH="/usr/pkg/sbin:/usr/pkg/bin:$PATH"
        # Keep the caller's PKG_PATH or the VM's configured quarterly mirrors.
        # Use a fallback only for unconfigured systems, such as fresh i386 base sets.
        package_path=$(pkg_admin config-var PKG_PATH)
        if [ -z "$package_path" ]; then
            # NetBSD packages use the major release's ABI and the base X11 sets.
            package_release=$(uname -r | cut -d . -f 1).0
            package_architecture=$host_architecture
            if [ "$package_architecture" = amd64 ]; then
                package_architecture=x86_64
            fi
            export PKG_PATH="https://cdn.netbsd.org/pub/pkgsrc/packages/NetBSD/$package_architecture/$package_release/All/"
        fi
        # The VM image may contain older dependencies than the package mirror.
        # Update those recursively when a requested package needs a newer one.
        pkg_add -u bash cmake ninja-build git-base python313 pkgconf curl \
            mozilla-rootcerts-openssl
        install_netbsd_x11
        pkg_add -u gtk3+
        ;;
    *)
        echo "$target requires its matching native BSD userspace; found $host_system $host_architecture." >&2
        exit 1
        ;;
esac
