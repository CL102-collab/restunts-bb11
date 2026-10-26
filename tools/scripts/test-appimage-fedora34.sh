#!/usr/bin/env bash
# Exercise the release archive with Fedora 34's own desktop core libraries.
set -euo pipefail

if (( $# > 2 )); then
    echo "Usage: $0 [APPIMAGE_ARCHIVE [STUNTS_BB11_DATA_DIRECTORY]]" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
archive=${1:-$repo_dir/dist/packages/restunts-linux-appimage-x64.tar.gz}
[[ -f "$archive" ]] || { echo "Missing AppImage archive: $archive" >&2; exit 1; }
archive=$(cd -- "$(dirname -- "$archive")" && printf '%s/%s' "$PWD" "$(basename -- "$archive")")
fedora_image=fedora:34@sha256:55a74d948c47bb002edff3c7a9a7e864152c686d7c2dc8e3df1ac4d611a20898
setup_container='' test_image=''
cleanup() {
    [[ -z "$setup_container" ]] || docker rm -f "$setup_container" >/dev/null 2>&1 || true
    [[ -z "$test_image" ]] || docker image rm "$test_image" >/dev/null 2>&1 || true
}
trap cleanup EXIT
setup_container=$(docker create --platform=linux/amd64 \
    --mount type=bind,src=/etc/ssl/certs/ca-certificates.crt,dst=/run/compat-ca.pem,readonly \
    "$fedora_image" sleep infinity)
docker start "$setup_container" >/dev/null
docker exec -i "$setup_container" bash -s <<'FEDORA_SETUP'
set -euo pipefail
cat > /etc/yum.repos.d/restunts-compat.repo <<'REPO'
[restunts-compat-base]
name=Original Fedora 34 packages
baseurl=https://archives.fedoraproject.org/pub/archive/fedora/linux/releases/34/Everything/x86_64/os/
enabled=1
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-34-x86_64
sslcacert=/run/compat-ca.pem
REPO
dnf -y --disablerepo='*' --enablerepo=restunts-compat-base install libX11 libX11-xcb libxcb
FEDORA_SETUP
test_image="restunts-fedora34-test:${setup_container:0:12}"
docker commit "$setup_container" "$test_image" >/dev/null
docker_options=(--rm --interactive --network=none --platform=linux/amd64
    --mount "type=bind,src=$archive,dst=/release.tar.gz,readonly")
if (( $# == 2 )); then
    [[ -d "$2" ]] || { echo "Missing Stunts BB 1.1 data directory: $2" >&2; exit 1; }
    data_dir=$(cd -- "$2" && pwd)
    docker_options+=(--mount "type=bind,src=$data_dir,dst=/stunts-data,readonly")
fi

# Only the archive and optional original game data enter the container. In
# particular, no source checkout, build tree, or host library is available.
docker run "${docker_options[@]}" "$test_image" bash -s <<'FEDORA_SMOKE'
set -euo pipefail
source /etc/os-release
[[ "$ID" == fedora && "$VERSION_ID" == 34 ]] || {
    echo "Expected Fedora 34, found $PRETTY_NAME" >&2
    exit 1
}
echo "Testing on $PRETTY_NAME ($(getconf GNU_LIBC_VERSION))"
sha256sum /release.tar.gz
unset LD_LIBRARY_PATH LD_PRELOAD LIBRARY_PATH
work_dir=$(mktemp -d /tmp/restunts-fedora34.XXXXXX)
trap 'rm -rf -- "$work_dir"' EXIT
mkdir "$work_dir/archive" "$work_dir/extraction" "$work_dir/unrelated"
tar --extract --gzip --file=/release.tar.gz --directory="$work_dir/archive"
appimage="$work_dir/archive/bin/restunts.AppImage"
[[ -x "$appimage" ]] || { echo "Missing executable bin/restunts.AppImage" >&2; exit 1; }
cd "$work_dir/extraction"
if ! timeout --kill-after=5s 60s "$appimage" --appimage-extract > "$work_dir/extract.log" 2>&1; then
    cat "$work_dir/extract.log" >&2
    echo "AppImage extraction failed on Fedora 34" >&2
    exit 1
fi
app_dir="$work_dir/extraction/squashfs-root"
[[ -x "$app_dir/AppRun" ]] || { echo "Missing extracted AppRun" >&2; exit 1; }
cd "$work_dir/unrelated"

check_info() {
    local option=$1 expected=$2 extracted_output image_output
    if ! extracted_output=$(timeout --kill-after=5s 60s "$app_dir/AppRun" "$option" 2> "$work_dir/apprun.log"); then
        cat "$work_dir/apprun.log" >&2
        echo "Extracted AppRun $option failed" >&2
        exit 1
    fi
    [[ "$extracted_output" == *"$expected"* ]] || {
        printf 'Unexpected AppRun %s output:\n%s\n' "$option" "$extracted_output" >&2
        exit 1
    }
    # Exercise the AppImage launcher too, without depending on FUSE in Docker.
    if ! image_output=$(APPIMAGE_EXTRACT_AND_RUN=1 timeout --kill-after=5s 60s "$appimage" "$option" 2> "$work_dir/appimage.log"); then
        cat "$work_dir/appimage.log" >&2
        echo "AppImage $option failed" >&2
        exit 1
    fi
    [[ "$image_output" == "$extracted_output" ]] || {
        printf 'AppImage and extracted AppRun differ for %s:\n%s\n' "$option" "$image_output" >&2
        exit 1
    }
    echo "AppImage and extracted AppRun $option passed"
}
check_info --help 'Usage: restunts'
check_info --version 'Version '
check_info --licenses 'Nuked OPL2 Lite'

shopt -s nullglob globstar
libraries=("$app_dir"/usr/lib/**/*.so "$app_dir"/usr/lib/**/*.so.*)
(( ${#libraries[@]} > 0 )) || { echo "No packaged shared libraries found" >&2; exit 1; }
executables=(
    "$app_dir"/usr/bin/{restunts,restunts-launcher,genghost-gui}
    "$app_dir"/usr/bin/{repldump,pixldump,genghost,restunts-gtk-modules}
)
for binary in "${executables[@]}" "${libraries[@]}"; do
    [[ -f "$binary" ]] || { echo "Missing packaged executable or library: $binary" >&2; exit 1; }
    if ! linkage=$(ldd "$binary" 2>&1); then
        printf 'Cannot load %s:\n%s\n' "$binary" "$linkage" >&2
        exit 1
    fi
    if [[ "$linkage" == *'not found'* ]]; then
        printf 'Unresolved dependency for %s:\n%s\n' "$binary" "$linkage" >&2
        exit 1
    fi
    echo "Fedora 34 linkage passed: ${binary#"$app_dir/"}"
done

if [[ -d /stunts-data ]]; then
    mkdir "$work_dir/game-data"
    cp -R /stunts-data/. "$work_dir/game-data/"
    chmod -R u+w "$work_dir/game-data"
    # Exercise interactive initialization and menu drawing as well as batch tools.
    # The dummy drivers need neither a display server nor an audio device.
    startup_result=0
    APPIMAGE_EXTRACT_AND_RUN=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 5s "$appimage" \
        --data-dir "$work_dir/game-data" --nointro > "$work_dir/startup.log" 2>&1 \
        || startup_result=$?
    if [[ "$startup_result" != 124 ]]; then
        cat "$work_dir/startup.log" >&2
        echo "AppImage game startup failed: expected the running menu, got exit $startup_result" >&2
        exit 1
    fi
    echo "Fedora 34 AppImage game startup passed (dummy video and audio)."
    bmp="$work_dir/game-data/DEFAULT.2.0.5.PDD.bmp"
    rm -f -- "$bmp"
    if ! SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 60s "$app_dir/usr/bin/pixldump" \
        --data-dir "$work_dir/game-data" DEFAULT.RPL 2 0 5 > "$work_dir/pixldump.log" 2>&1; then
        cat "$work_dir/pixldump.log" >&2
        echo "Packaged pixldump failed to render DEFAULT.RPL on Fedora 34" >&2
        exit 1
    fi
    [[ -f "$bmp" && "$(head -c 2 "$bmp")" == BM && "$(stat -c %s "$bmp")" == 65078 ]] || {
        cat "$work_dir/pixldump.log" >&2
        echo "Packaged pixldump did not create the expected 320x200 indexed BMP" >&2
        exit 1
    }
    echo "Fedora 34 replay rendering passed: DEFAULT.RPL, camera 2, player, frame 5"
else
    echo "Replay rendering skipped: pass a Stunts BB 1.1 data directory as the second argument."
fi
echo "Fedora 34 AppImage smoke test passed."
FEDORA_SMOKE
