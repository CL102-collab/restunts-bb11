#!/bin/sh
# Stage binary dependencies before the package manager modifies the system.
set -eu

export PATH="/usr/pkg/sbin:/usr/pkg/bin:$PATH"

# Caller-supplied caches and repositories retain pkg_add's search semantics.
if [ -n "${PKG_PATH:-}" ]; then
    exec pkg_add -u "$@"
fi
package_admin=$(command -v pkg_admin)
package_path=$("$package_admin" config-var PKG_PATH)
# The VM action configures base pkg_install. Bootstrapping pkgin can add
# pkgsrc tools that use a separate configuration directory.
if [ -z "$package_path" ] && [ "$package_admin" = /usr/pkg/sbin/pkg_admin ]; then
    package_admin=/usr/sbin/pkg_admin
    package_path=$("$package_admin" config-var PKG_PATH)
fi
export PKG_INSTALL_DIR="${package_admin%/*}"
if [ -z "$package_path" ]; then
    architecture=$(uname -p)
    case "$architecture" in
        amd64) architecture=x86_64 ;;
        arm64) architecture=aarch64 ;;
        armv7) architecture=earmv7hf ;;
    esac
    release=$(uname -r)
    package_path="https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/$architecture/${release%%.*}.0/All"
fi

mirror_host() {
    host=${1#*://}
    printf '%s' "${host%%/*}" | tr '[:upper:]' '[:lower:]'
}

is_netbsd_mirror() {
    case "$1" in
        http://*/pub/pkgsrc/packages/NetBSD/*/All|https://*/pub/pkgsrc/packages/NetBSD/*/All) ;;
        *) return 1 ;;
    esac
    case "$(mirror_host "$1")" in
        cdn.netbsd.org|ftp.netbsd.org|ftp.fr.netbsd.org|ftp.jaist.ac.jp|ftp.allbsd.org) return 0 ;;
        *) return 1 ;;
    esac
}

# Leave configured custom sources alone, including mixed lists and local caches.
paths=$package_path
while [ -n "$paths" ]; do
    repository=${paths%%;*}
    if ! is_netbsd_mirror "${repository%/}"; then
        exec "$PKG_INSTALL_DIR/pkg_add" -u "$@"
    fi
    case "$paths" in
        *';'*) paths=${paths#*;} ;;
        *) break ;;
    esac
done

# Keep the preferred ABI and quarter together for each dependency set. The
# NetBSD FTP hostname aliases the CDN, so use JAIST for independent failover.
repository=${package_path%%;*}
repository=${repository%/}
package_tree=${repository#*/pub/pkgsrc/packages/NetBSD/}
cdn_repository="https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/$package_tree"
jaist_repository="https://ftp.jaist.ac.jp/pub/pkgsrc/packages/NetBSD/$package_tree"
case "$(mirror_host "$repository")" in
    ftp.jaist.ac.jp) mirrors="$jaist_repository $cdn_repository" ;;
    cdn.netbsd.org|ftp.netbsd.org) mirrors="$cdn_repository $jaist_repository" ;;
    *) mirrors="https://${repository#*://} $cdn_repository $jaist_repository" ;;
esac
download_idle_timeout=20
bootstrap_timeout=2m
download_timeout=10m

bootstrap_pkgin() (
    staging=$(mktemp -d /var/tmp/restunts-pkgin.XXXXXX) || exit 1
    trap 'rm -rf -- "$staging"' 0
    trap 'exit 1' 1 2 15
    download() {
        timeout "$bootstrap_timeout" ftp -o "$staging/$1" -q "$download_idle_timeout" "$repository/$1"
    }
    download pkg_summary.gz || exit 1
    gzip -dc "$staging/pkg_summary.gz" > "$staging/summary" || exit 1
    # Base ftp, awk and pkg_add are available even in the fresh i386 chroot.
    # Resolve the small pkgin bootstrap set through the same package summary.
    awk -F '\n' 'BEGIN { RS = "" }
        /(^|\n)PKGNAME=(pkgin|pkg_install|sqlite3)-[0-9]/ {
            file = size = ""
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^FILE_NAME=/) file = substr($i, length("FILE_NAME=") + 1)
                if ($i ~ /^FILE_SIZE=/) size = substr($i, length("FILE_SIZE=") + 1)
            }
            if (file ~ /^(pkgin|pkg_install|sqlite3)-[0-9][a-zA-Z0-9._-]*\.tgz$/ && size ~ /^[0-9]+$/) {
                split(file, name, "-")
                files[name[1]]++
                print file, size
            }
        }
        END { exit !(files["pkgin"] == 1 && files["pkg_install"] == 1 && files["sqlite3"] == 1) }
        ' "$staging/summary" > "$staging/files" || {
        echo "No unique pkgin bootstrap set in $repository" >&2
        exit 1
    }
    pkgin_file=
    while read -r filename size; do
        download "$filename" || exit 1
        if [ "$(wc -c < "$staging/$filename")" -ne "$size" ]; then
            echo "Incomplete NetBSD bootstrap package: $filename" >&2
            exit 1
        fi
        case "$filename" in pkgin-*) pkgin_file=$filename ;; esac
    done < "$staging/files"
    [ -n "$pkgin_file" ] || exit 1
    PKG_PATH="$staging" "$PKG_INSTALL_DIR/pkg_add" -u "$staging/$pkgin_file"
)

for repository in $mirrors; do
    echo "Fetching NetBSD packages from $repository" >&2
    if ! command -v pkgin >/dev/null && ! bootstrap_pkgin; then
        continue
    fi
    # pkgin checks complete archives against the summary before caching.
    # Gate installation on a successful download of the whole dependency set.
    if PKG_REPOS="$repository" timeout "$download_timeout" pkgin -yd install "$@"; then
        PKG_REPOS="$repository" exec pkgin -y install "$@"
    fi
done
echo "Could not download NetBSD packages: $*" >&2
exit 1
