#!/usr/bin/env bash
# Run inside the distribution being tested, with no build tree or host libraries.
set -euo pipefail

if (( $# != 4 )); then
    echo "Usage: $0 TARGET ARCHIVE GAME_DATA DISTRO" >&2
    exit 2
fi
target=$1 archive=$2 data_dir=$3 distro=$4
case "$target" in
    linux-appimage-x64) elf_class=2 ;;
    linux-appimage-x86|linux-appimage-x86-no-sse2) elf_class=1 ;;
    *) echo "Unknown AppImage target: $target" >&2; exit 2 ;;
esac
case "$distro" in
    ubuntu*)
        if [[ -f /etc/os-release ]]; then
            # shellcheck source=/dev/null
            source /etc/os-release
            [[ "$ID" == ubuntu && "$VERSION_ID" == "${distro#ubuntu}" ]]
        else
            # shellcheck source=/dev/null
            source /etc/lsb-release
            [[ "$DISTRIB_ID" == Ubuntu && "$DISTRIB_RELEASE" == "${distro#ubuntu}" ]]
        fi
        ;;
    fedora*)
        # shellcheck source=/dev/null
        source /etc/os-release
        [[ "$ID" == fedora && "$VERSION_ID" == "${distro#fedora}" ]]
        ;;
    *) echo "Unknown distribution: $distro" >&2; exit 2 ;;
esac
echo "Testing $target on $distro ($(getconf GNU_LIBC_VERSION)); kernel $(uname -r)"
sha256sum "$archive"
if [[ ${RESTUNTS_REQUIRE_NO_SSE2:-0} == 1 ]]; then
    [[ "$target" == linux-appimage-x86-no-sse2 ]]
    if sed -n 's/^flags[[:space:]]*: / /p' /proc/cpuinfo | grep -qw sse2; then
        echo 'The no-SSE2 test requires an emulated CPU without SSE2.' >&2
        exit 1
    fi
    echo 'Verified guest CPU does not expose SSE2.'
fi
unset LD_LIBRARY_PATH LD_PRELOAD LIBRARY_PATH APPDIR APPIMAGE_EXTRACT_AND_RUN
work_dir=$(mktemp -d /tmp/restunts-compat.XXXXXX)
trap 'rm -rf -- "$work_dir"' EXIT
# Lucid's coreutils 7.4 predates the timeout program. Use a process group so
# the deadline also terminates the AppImage's extracted child process.
run_with_timeout() {
    local duration=$1 command_pid timer_pid status=0 marker
    shift
    marker=$(mktemp "$work_dir/deadline.XXXXXX")
    rm "$marker"
    setsid "$@" &
    command_pid=$!
    (
        sleep "$duration"
        if kill -0 "$command_pid" 2>/dev/null; then
            : > "$marker"
            kill -TERM -- "-$command_pid" 2>/dev/null || true
            sleep 5
            kill -KILL -- "-$command_pid" 2>/dev/null || true
        fi
    ) > /dev/null 2>&1 &
    timer_pid=$!
    wait "$command_pid" 2>/dev/null || status=$?
    if [[ -e "$marker" ]]; then
        # The launcher can exit before its game child handles SIGTERM. Keep
        # the watchdog alive until it also finishes the process group cleanup.
        wait "$timer_pid" 2>/dev/null || true
        status=124
        rm "$marker"
    else
        kill "$timer_pid" 2>/dev/null || true
        wait "$timer_pid" 2>/dev/null || true
    fi
    return "$status"
}
mkdir "$work_dir/archive" "$work_dir/extraction" "$work_dir/unrelated" "$work_dir/data"
tar -xzf "$archive" -C "$work_dir/archive"
cp -R "$data_dir/." "$work_dir/data/"
chmod -R u+w "$work_dir/data"
appimage="$work_dir/archive/bin/restunts.AppImage"
[[ -x "$appimage" ]]
[[ "$(od -An -tu1 -j4 -N1 "$appimage" | tr -d ' ')" == "$elf_class" ]] || {
    echo 'The AppImage runtime has the wrong ELF architecture.' >&2
    exit 1
}
cd "$work_dir/extraction"
run_with_timeout 120 "$appimage" --appimage-extract > "$work_dir/extract.log" 2>&1 || {
    cat "$work_dir/extract.log" >&2
    exit 1
}
app_dir="$work_dir/extraction/squashfs-root"
[[ -x "$app_dir/AppRun" ]]
cd "$work_dir/unrelated"
for specification in '--help|Usage: restunts' '--version|Version ' '--licenses|Nuked OPL2 Lite'; do
    option=${specification%%|*} expected=${specification#*|}
    extracted_output=$(run_with_timeout 60 "$app_dir/AppRun" "$option")
    [[ "$extracted_output" == *"$expected"* ]]
    image_output=$(APPIMAGE_EXTRACT_AND_RUN=1 run_with_timeout 120 "$appimage" "$option")
    [[ "$image_output" == "$extracted_output" ]]
    if [[ ${RESTUNTS_EXPECT_FUSE:-0} == 1 ]]; then
        fuse_output=$(run_with_timeout 120 "$appimage" "$option")
        [[ "$fuse_output" == "$extracted_output" ]]
    fi
    echo "AppImage $option passed."
done
export LD_LIBRARY_PATH="$app_dir/usr/lib"
shopt -s nullglob globstar
libraries=("$app_dir"/usr/lib/**/*.so "$app_dir"/usr/lib/**/*.so.*)
(( ${#libraries[@]} > 0 ))
for binary in "$app_dir"/usr/bin/{restunts,repldump,pixldump,genghost} "${libraries[@]}"; do
    [[ -f "$binary" ]]
    [[ "$(od -An -tu1 -j4 -N1 "$binary" | tr -d ' ')" == "$elf_class" ]]
    if ! linkage=$(ldd "$binary" 2>&1) || [[ "$linkage" == *'not found'* ]]; then
        printf 'Unresolved dependency for %s:\n%s\n' "$binary" "$linkage" >&2
        exit 1
    fi
done
run_with_timeout 60 "$app_dir/usr/bin/genghost" --help > "$work_dir/genghost.log" 2>&1
unset LD_LIBRARY_PATH
video_driver=${SDL_VIDEODRIVER:-dummy}
startup_result=0
APPIMAGE_EXTRACT_AND_RUN=1 SDL_VIDEODRIVER="$video_driver" SDL_AUDIODRIVER=dummy \
    run_with_timeout 12 "$appimage" --data-dir "$work_dir/data" --nointro \
    > "$work_dir/startup.log" 2>&1 || startup_result=$?
if [[ "$startup_result" != 124 ]]; then
    cat "$work_dir/startup.log" >&2
    echo "Game startup failed: expected the running menu, got exit $startup_result" >&2
    exit 1
fi
echo "AppImage game startup passed ($video_driver video)."
if [[ ${RESTUNTS_EXPECT_FUSE:-0} == 1 ]]; then
    SDL_VIDEODRIVER="$video_driver" SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        "$appimage" --data-dir "$work_dir/data" --nointro > "$work_dir/fuse-game.log" 2>&1 &
    game_pid=$!
    sleep 12
    if ! kill -0 "$game_pid" 2>/dev/null; then
        cat "$work_dir/fuse-game.log" >&2
        echo 'The FUSE-mounted AppImage exited before drawing the menu.' >&2
        exit 1
    fi
    if [[ "$video_driver" == x11 ]]; then
        # The VM host checks the actual window tree and saves the framebuffer.
        echo 'RESTUNTS_FUSE_MENU_READY'
        sleep 3
    fi
    kill "$game_pid"
    wait "$game_pid" || [[ $? == 143 ]]
    echo 'FUSE-mounted AppImage game startup passed.'
fi
bmp="$work_dir/data/DEFAULT.2.0.5.PDD.bmp"
rm -f -- "$bmp"
LD_LIBRARY_PATH="$app_dir/usr/lib" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    run_with_timeout 120 "$app_dir/usr/bin/pixldump" \
    --data-dir "$work_dir/data" DEFAULT.RPL 2 0 5 > "$work_dir/render.log" 2>&1 || {
        cat "$work_dir/render.log" >&2
        exit 1
    }
[[ -f "$bmp" && "$(head -c 2 "$bmp")" == BM && "$(stat -c %s "$bmp")" == 65078 ]]
sha256sum "$bmp"
printf '%s  %s\n' 808e1a11ac05d5757379d425a6a857a5584912cc62f7ec567662a7dbcefa74fa "$bmp" | sha256sum --check

# Exercise the bundled ALSA backend without a physical sound card. Keep one
# MiB of PCM and drain the rest, because the null sink runs without pacing.
audio_fifo="$work_dir/audio.pipe" audio_raw="$work_dir/audio.raw" audio_ready="$work_dir/audio.ready"
mkfifo "$audio_fifo"
cat > "$work_dir/alsa.conf" <<EOF
pcm.!default {
    type file
    slave.pcm { type null }
    file "$audio_fifo"
    format "raw"
}
EOF
# Keep the FIFO open across any ALSA device probes/reopens. The reader and
# game close this extra descriptor; it belongs only to this coordinating shell.
exec 3<> "$audio_fifo"
(
    trap 'printf "PCM reader exit=%s\n" "$?" >&2' EXIT
    blocks=0
    # SDL starts its audio thread before loading the game's music. Discard
    # complete silent buffers until music starts, then retain one bounded block.
    while head -c 1048576 > "$audio_raw"; do
        bytes=$(stat -c %s "$audio_raw")
        [[ "$bytes" == 1048576 ]] || { echo "Short PCM block: $bytes bytes" >&2; break; }
        blocks=$((blocks + 1))
        if [[ -n "$(od -An -tu4 "$audio_raw" | tr -d '[:space:]0*')" ]]; then
            echo "Captured nonzero PCM in block $blocks" >&2
            : > "$audio_ready"
            cat > /dev/null
            break
        fi
    done
) < "$audio_fifo" 3>&- 2> "$work_dir/audio-reader.log" &
audio_reader_pid=$!
setsid env APPIMAGE_EXTRACT_AND_RUN=1 ALSA_CONFIG_PATH="$work_dir/alsa.conf" \
    SDL_VIDEODRIVER="$video_driver" SDL_AUDIODRIVER=alsa SDL_RENDER_DRIVER=software \
    "$appimage" --data-dir "$work_dir/data" --nointro 3>&- > "$work_dir/audio.log" 2>&1 &
audio_pid=$!
audio_captured=0
# TCG and the unpaced null sink can delay music initialization. Finish as soon
# as real PCM arrives, while retaining a bounded 60-second failure deadline.
for ((attempt = 0; attempt < 120; attempt++)); do
    kill -0 "$audio_pid" 2>/dev/null || break
    if [[ -f "$audio_ready" ]]; then audio_captured=1; break; fi
    kill -0 "$audio_reader_pid" 2>/dev/null || break
    sleep 0.5
done
# Stop every game process before closing the FIFO, so teardown cannot turn a
# successful capture into an ALSA EPIPE/assertion in a still-running child.
kill -TERM -- "-$audio_pid" 2>/dev/null || true
for ((attempt = 0; attempt < 50; attempt++)); do
    kill -0 -- "-$audio_pid" 2>/dev/null || break
    sleep 0.1
done
kill -KILL -- "-$audio_pid" 2>/dev/null || true
audio_status=0 audio_reader_status=0
wait "$audio_pid" 2>/dev/null || audio_status=$?
exec 3>&-
wait "$audio_reader_pid" 2>/dev/null || audio_reader_status=$?
audio_bytes=0
[[ ! -f "$audio_raw" ]] || audio_bytes=$(stat -c %s "$audio_raw")
if [[ "$audio_captured" != 1 || "$audio_bytes" != 1048576 || "$audio_reader_status" != 0 || \
    ( "$audio_status" != 0 && "$audio_status" != 143 && "$audio_status" != 137 ) ]]; then
    printf 'PCM capture: ready=%s bytes=%s producer_exit=%s reader_exit=%s\n' \
        "$audio_captured" "$audio_bytes" "$audio_status" "$audio_reader_status" >&2
    cat "$work_dir/audio-reader.log" >&2
    cat "$work_dir/audio.log" >&2
    echo 'Bundled ALSA backend did not produce the expected PCM capture.' >&2
    exit 1
fi
[[ -n "$(od -An -v -tu1 "$audio_raw" | tr -d '[:space:]0')" ]] || {
    cat "$work_dir/audio-reader.log" >&2
    cat "$work_dir/audio.log" >&2
    echo 'Bundled ALSA backend produced only silence.' >&2
    exit 1
}
echo 'Bundled ALSA backend passed: 1,048,576 bytes of non-silent PCM captured.'
echo "RESTUNTS_COMPATIBILITY_PASSED target=$target distro=$distro"
