#!/usr/bin/env bash
# Test a completed package on each promised userspace, not in the build image.
set -euo pipefail
if (( $# != 4 )); then
    echo "Usage: $0 TARGET ARCHIVE GAME_DATA DISTRO" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
target=$1 archive=$(realpath -- "$2") data_dir=$(realpath -- "$3") distro=$4
[[ -f "$archive" && -d "$data_dir" ]]
case "$target" in
    linux-appimage-x64|linux-appimage-x86|linux-appimage-x86-no-sse2) ;;
    *) echo "Unknown AppImage target: $target" >&2; exit 2 ;;
esac
if [[ "$distro" == ubuntu10.04 ]]; then
    exec bash "$script_dir/test-appimage-ubuntu10-vm.sh" "$target" "$archive" "$data_dir"
fi
case "$distro" in
    fedora20) image=fedora:20@sha256:5d5a02b873d298da9bca4b84440c5cd698b0832560c850d92cf389cef58bc549 ;;
    ubuntu26.10) image=ubuntu:26.10@sha256:1b798ba0017f24244ca02af25e3cfb1160db35e270e7b3beef708910b468bd92 ;;
    fedora44) image=fedora:44@sha256:be9d65e2344d805cc11114319c685ecaa96b6d9b4350a0a6460cdb931babbd19 ;;
    *) echo "Unsupported test distribution: $distro" >&2; exit 2 ;;
esac
work_dir=$(mktemp -d)
setup_container='' test_image=''
cleanup() {
    [[ -z "$setup_container" ]] || docker rm -f "$setup_container" >/dev/null 2>&1 || true
    [[ -z "$test_image" ]] || docker image rm "$test_image" >/dev/null 2>&1 || true
    rm -rf -- "$work_dir"
}
trap cleanup EXIT
# The setup container may install the distribution's own 32-bit libc. The
# subsequent test container has no network and receives only package and data.
if [[ "$target" != linux-appimage-x64 ]]; then
    ca_bundle=/etc/ssl/certs/ca-certificates.crt
    setup_container=$(docker create --platform=linux/amd64 \
        --mount "type=bind,src=$ca_bundle,dst=/run/compat-ca.pem,readonly" \
        "$image" sleep infinity)
    docker start "$setup_container" >/dev/null
    docker exec -i "$setup_container" bash -s -- "$distro" <<'SETUP'
set -euo pipefail
case "$1" in
    ubuntu*)
        export DEBIAN_FRONTEND=noninteractive
        apt-get -o Acquire::https::CaInfo=/run/compat-ca.pem update
        apt-get -o Acquire::https::CaInfo=/run/compat-ca.pem install -y --no-install-recommends libc6-i386
        ;;
    fedora44)
        dnf -y --setopt=sslcacert=/run/compat-ca.pem install glibc.i686
        ;;
    fedora20)
        cat > /etc/yum.repos.d/restunts-compat.repo <<'REPO'
[restunts-compat-base]
name=Original Fedora 20 packages
baseurl=https://archives.fedoraproject.org/pub/archive/fedora/linux/releases/20/Everything/x86_64/os/
enabled=1
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-20-x86_64
sslcacert=/run/compat-ca.pem
[restunts-compat-updates]
name=Final Fedora 20 updates
baseurl=https://archives.fedoraproject.org/pub/archive/fedora/linux/updates/20/x86_64/
enabled=1
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-20-x86_64
sslcacert=/run/compat-ca.pem
REPO
        yum -y --disablerepo='*' --enablerepo='restunts-compat-*' install glibc.i686
        ;;
esac
test -x /lib/ld-linux.so.2
SETUP
    test_image="restunts-appimage-test:$(basename -- "$work_dir" | tr '[:upper:]' '[:lower:]')"
    docker commit "$setup_container" "$test_image" >/dev/null
    image=$test_image
fi
docker run --rm --network=none --platform=linux/amd64 \
    --mount "type=bind,src=$archive,dst=/release.tar.gz,readonly" \
    --mount "type=bind,src=$data_dir,dst=/stunts-data,readonly" \
    --mount "type=bind,src=$script_dir/test-appimage-guest.sh,dst=/test-appimage-guest.sh,readonly" \
    "$image" bash /test-appimage-guest.sh "$target" /release.tar.gz /stunts-data "$distro"
