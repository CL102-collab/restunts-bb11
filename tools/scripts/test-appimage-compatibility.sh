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
# Graphics clients must use the distribution's matching X11/XCB core libraries.
# Modern x64 guests also exercise Mesa OpenGL through a private Xvfb server.
# The subsequent test container has no network and receives only package and data.
test_opengl=0
if [[ "$target" == linux-appimage-x64 && "$distro" != fedora20 ]]; then
    test_opengl=1
fi
ca_bundle=/etc/ssl/certs/ca-certificates.crt
setup_container=$(docker create --platform=linux/amd64 \
    --mount "type=bind,src=$ca_bundle,dst=/run/compat-ca.pem,readonly" \
    "$image" sleep infinity)
docker start "$setup_container" >/dev/null
docker exec -i "$setup_container" bash -s -- "$distro" "$target" "$test_opengl" <<'SETUP'
set -euo pipefail
architecture=x86_64
if [[ "$2" != linux-appimage-x64 ]]; then architecture=i686; fi
case "$1" in
    ubuntu*)
        export DEBIAN_FRONTEND=noninteractive
        debian_architecture=amd64
        if [[ "$architecture" == i686 ]]; then
            debian_architecture=i386
            dpkg --add-architecture "$debian_architecture"
        fi
        apt-get -o Acquire::https::CaInfo=/run/compat-ca.pem update
        packages=("libx11-6:$debian_architecture" "libx11-xcb1:$debian_architecture"
            "libxcb1:$debian_architecture")
        if [[ "$3" == 1 ]]; then
            packages+=(python3 xvfb libgl1 libgl1-mesa-dri libglx-mesa0)
        fi
        apt-get -o Acquire::https::CaInfo=/run/compat-ca.pem install -y --no-install-recommends \
            "${packages[@]}"
        ;;
    fedora44)
        packages=("libX11.$architecture" "libX11-xcb.$architecture" "libxcb.$architecture")
        if [[ "$3" == 1 ]]; then
            packages+=(python3 xorg-x11-server-Xvfb mesa-dri-drivers mesa-libGL)
        fi
        dnf -y --setopt=sslcacert=/run/compat-ca.pem install "${packages[@]}"
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
        # The final Docker image contains NSS 3.19.2, while the archived
        # i686 packages stop at 3.19.1. Downgrade the native NSS family together
        # to the signed archive packages before adding the matching 32-bit ABI.
        # Keep Yum's protected_multilib check enabled.
        if [[ "$architecture" == i686 ]]; then
            yum -y --disablerepo='*' --enablerepo='restunts-compat-*' downgrade 'nss*.x86_64'
        fi
        yum -y --disablerepo='*' --enablerepo='restunts-compat-*' install \
            "libX11.$architecture" "libX11-xcb.$architecture" "libxcb.$architecture"
        if [[ "$architecture" == i686 ]]; then
            rpm -q glibc.x86_64 glibc.i686 nss-softokn-freebl.x86_64 nss-softokn-freebl.i686
        fi
        ;;
esac
if [[ "$architecture" == i686 ]]; then test -x /lib/ld-linux.so.2; fi
SETUP
test_image="restunts-appimage-test:$(basename -- "$work_dir" | tr '[:upper:]' '[:lower:]')"
docker commit "$setup_container" "$test_image" >/dev/null
image=$test_image
docker run --rm --network=none --platform=linux/amd64 \
    --mount "type=bind,src=$archive,dst=/release.tar.gz,readonly" \
    --mount "type=bind,src=$data_dir,dst=/stunts-data,readonly" \
    --mount "type=bind,src=$script_dir/test-appimage-guest.sh,dst=/test-appimage-guest.sh,readonly" \
    --mount "type=bind,src=$script_dir/test-appimage-opengl.py,dst=/test-appimage-opengl.py,readonly" \
    "$image" bash -c '
set -euo pipefail
bash /test-appimage-guest.sh "$1" /release.tar.gz /stunts-data "$2"
if [[ "$3" == 1 ]]; then
    work_dir=$(mktemp -d)
    trap '\''rm -rf -- "$work_dir"'\'' EXIT
    tar -xzf /release.tar.gz -C "$work_dir" bin/restunts.AppImage
    python3 /test-appimage-opengl.py "$work_dir/bin/restunts.AppImage" /stunts-data
fi
' bash "$target" "$distro" "$test_opengl"
