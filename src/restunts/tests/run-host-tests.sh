#!/bin/bash

set -euo pipefail

test_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
test_source_dir="$test_script_dir/../c"
test_build_dir="$(mktemp -d)"
test_compiler="${CC:-cc}"

cleanup_test_build_dir() {
    rm -rf -- "$test_build_dir"
}
trap cleanup_test_build_dir EXIT

test_compile_flags=(
    -std=c99
    -O2
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
)
case "$(uname -s)" in
    Darwin) test_link_flags=(-Wl,-dead_strip) ;;
    Haiku)
        # Haiku links executables as shared objects; hide unused fixture symbols.
        test_compile_flags+=(-fvisibility=hidden)
        test_link_flags=(-Wl,--gc-sections)
        ;;
    *) test_link_flags=(-Wl,--gc-sections) ;;
esac
# Match CMake's environment flags, including sanitizer instrumentation. Split
# on whitespace without evaluating shell syntax or expanding file patterns.
read -r -a test_extra_compile_flags <<< "${CFLAGS:-}"
read -r -a test_extra_link_flags <<< "${LDFLAGS:-}"
# Bash 3.2 (macOS) treats an empty array expansion as unset with nounset.
if [[ ${#test_extra_compile_flags[@]} -gt 0 ]]; then
    test_compile_flags+=("${test_extra_compile_flags[@]}")
fi
if [[ ${#test_extra_link_flags[@]} -gt 0 ]]; then
    test_link_flags+=("${test_extra_link_flags[@]}")
fi
# Clang (including Apple's compiler) has no GCC maybe-uninitialized warning.
test_uninitialized_flag=-Wno-uninitialized
if "$test_compiler" -Werror -Wno-maybe-uninitialized -x c -fsyntax-only /dev/null 2>/dev/null; then
    test_uninitialized_flag=-Wno-maybe-uninitialized
fi

run_host_test() {
    local test_name="$1"
    local source_file="$2"
    shift 2

    echo "Building $test_name"
    "$test_compiler" "${test_compile_flags[@]}" "$@" \
        "$test_script_dir/$test_name.c" "$test_source_dir/$source_file" \
        "${test_link_flags[@]}" -o "$test_build_dir/$test_name"

    echo "Running $test_name"
    "$test_build_dir/$test_name"
}

run_host_test test-audio-engine legacy.c -Wno-unused-variable -Wno-sign-compare
run_host_test test-audio-car legacy.c \
    "$test_source_dir/math.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/headless_data.c" -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-audio-sequence legacy.c -Wno-sign-compare
run_host_test test-audio-resources legacy.c "$test_source_dir/resource.c" -Wno-pointer-sign
run_host_test test-race-flow legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-race-rewind legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-race-frames legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-car-menu menu_car.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-car-menu menu_car.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-car-speed statecar.c \
    "$test_source_dir/math.c" "$test_source_dir/legacy.c" "$test_source_dir/strlib.c"
run_host_test test-crash-state statecrs.c "$test_source_dir/legacy.c" -DRESTUNTS_HEADLESS
run_host_test test-collision-interpolation physics_collision.c \
    "$test_source_dir/math.c" "$test_source_dir/legacy.c" "$test_source_dir/strlib.c"
run_host_test test-command-line command_line.c \
    -DRESTUNTS_GIT_HASH='"0123456789abcdef0123456789abcdef01234567"' \
    -DRESTUNTS_BUILD_DATE='"Feb 12 1991"'
run_host_test test-command-line command_line.c -DRESTUNTS_SDL3 \
    -DRESTUNTS_GIT_HASH='"0123456789abcdef0123456789abcdef01234567"' \
    -DRESTUNTS_BUILD_DATE='"Feb 12 1991"'
run_host_test test-command-line command_line.c -DRESTUNTS_SDL3 -D__DJGPP__ \
    -DRESTUNTS_GIT_HASH='"0123456789abcdef0123456789abcdef01234567"' \
    -DRESTUNTS_BUILD_DATE='"Feb 12 1991"'
run_host_test test-command-line command_line.c \
    -DRESTUNTS_BUILD_DATE='"Feb 12 1991"' \
    -DCOMMAND_LINE_TEST_VERSION='"Version source-archive (Feb 12 1991)"'
run_host_test test-startup math.c "$test_source_dir/owoot.c" \
    "$test_source_dir/statecar.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/physics_collision.c" "$test_source_dir/physics_grip.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-startup math.c -DRESTUNTS_SDL3 \
    "$test_source_dir/frame_adaptive.c" "$test_source_dir/owoot.c" \
    "$test_source_dir/statecar.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/physics_collision.c" "$test_source_dir/physics_grip.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-startup math.c -DRESTUNTS_SDL3 -D__ANDROID__ \
    "$test_source_dir/frame_adaptive.c" "$test_source_dir/owoot.c" \
    "$test_source_dir/statecar.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/physics_collision.c" "$test_source_dir/physics_grip.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-credits legacy.c \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-modal-dialogs menu_options.c \
    "$test_source_dir/ui_dialog.c" "$test_source_dir/legacy.c" \
    -DRESTUNTS_GIT_HASH='"0123456"' -Wno-pointer-sign
run_host_test test-dashboard dashboard.c \
    "$test_source_dir/simd.c" "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-frame-render math.c \
    "$test_source_dir/full_data.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/heapsort.c" "$test_source_dir/trkutil.c" \
    -Wno-pointer-sign -Wno-sign-compare -Wno-missing-field-initializers "$test_uninitialized_flag" \
    -Wno-missing-braces
run_host_test test-frame-render math.c -DRESTUNTS_SDL3 \
    "$test_source_dir/frame_adaptive.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/heapsort.c" "$test_source_dir/trkutil.c" \
    -Wno-pointer-sign -Wno-sign-compare -Wno-missing-field-initializers "$test_uninitialized_flag" \
    -Wno-missing-braces
run_host_test test-frame-overlay frame_overlay.c \
    "$test_source_dir/strlib.c" \
    "$test_source_dir/shape3d.c" "$test_source_dir/shape3d_prerender.c" \
    "$test_source_dir/shape3d_lines.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/headless_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-frame-overlay frame_overlay.c -DRESTUNTS_SDL3 \
    "$test_source_dir/strlib.c" \
    "$test_source_dir/shape3d.c" "$test_source_dir/shape3d_prerender.c" \
    "$test_source_dir/shape3d_lines.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/headless_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-frame-adaptive frame_adaptive.c
run_host_test test-frame-interpolation frame_interpolation.c
run_host_test test-intro-preview intro_render.c \
    "$test_source_dir/track_preview.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/math.c" "$test_source_dir/legacy.c" "$test_source_dir/trkutil.c" \
    -DRESTUNTS_FULL -Wno-pointer-sign -Wno-missing-braces -Wno-missing-field-initializers
run_host_test test-intro-preview intro_render.c \
    "$test_source_dir/track_preview.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/math.c" "$test_source_dir/legacy.c" "$test_source_dir/trkutil.c" \
    -DRESTUNTS_FULL -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-braces -Wno-missing-field-initializers
run_host_test test-end-hiscore highscore.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-end-hiscore highscore.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-file-decompression fileio.c "$test_source_dir/resource.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" \
    -DRESTUNTS_HEADLESS -Wno-pointer-sign
run_host_test test-file-write fileio.c \
    -DRESTUNTS_SDL3 -DRESTUNTS_HEADLESS -Wno-pointer-sign
run_host_test test-file-write fileio.c \
    -DRESTUNTS_SDL3 -DRESTUNTS_HEADLESS -D__ANDROID__ -Wno-pointer-sign
run_host_test test-fatal-format legacy.c \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-gamestate-serialization stateio.c
run_host_test test-ghost ghost.c \
    "$test_source_dir/replay.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/legacy.c" -Wno-pointer-sign
run_host_test test-ghost ghost.c \
    "$test_source_dir/replay.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/frame_interpolation.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign
run_host_test test-grip physics_grip.c \
    "$test_source_dir/physics_collision.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/math.c" "$test_source_dir/legacy.c" -Wno-sign-compare
run_host_test test-legacy-semantics legacy.c
run_host_test test-line-prepare shape3d_lines.c \
    "$test_source_dir/full_data.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-matrix-semantics math.c
run_host_test test-math-boundaries math.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/legacy.c"
run_host_test test-simulation-setup gameinit.c "$test_source_dir/owoot.c" \
    "$test_source_dir/gamestep.c" "$test_source_dir/carsetup.c" \
    "$test_source_dir/math.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/legacy.c" -Wno-pointer-sign -Wno-sign-compare
run_host_test test-penalty-routing physics_grip.c -Wno-sign-compare
run_host_test test-wheel-suspension physics_collision.c
run_host_test test-phantom-motion legacy.c \
    "$test_source_dir/physics_grip.c" "$test_source_dir/physics_collision.c" \
    "$test_source_dir/strlib.c" -Wno-sign-compare
run_host_test test-route-points opponent.c "$test_source_dir/trkutil.c" -Wno-missing-braces
run_host_test test-memmgr-cache memmgr.c \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-braces -Wno-missing-field-initializers
run_host_test test-opponent-route carsetup.c \
    "$test_source_dir/resource.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/legacy.c" -Wno-pointer-sign
run_host_test test-opponent-tick opponent.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/trkutil.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-missing-braces
run_host_test test-pixldump-murmur3 ../pixldump/murmur3.c
run_host_test test-pixldump-playback legacy.c -I"$test_source_dir" \
    -Wno-pointer-sign -Wno-sign-compare
run_host_test test-polygon-edges shape3d_prerender.c \
    "$test_source_dir/full_data.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-input-record legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers "$test_uninitialized_flag"
run_host_test test-input-record legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/frame_adaptive.c" -DRESTUNTS_SDL3 \
    -Wno-pointer-sign -Wno-missing-field-initializers "$test_uninitialized_flag"
run_host_test test-replay-menu replay.c \
    "$test_source_dir/legacy.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-replay-menu replay.c \
    "$test_source_dir/legacy.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-replay-controls legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" "$test_source_dir/math.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-replay-controls legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/full_data.c" "$test_source_dir/math.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-replay-serialization replay.c
run_host_test test-resource-lookup resource.c
run_host_test test-shape2d-memory shape2d_resources.c \
    "$test_source_dir/memmgr.c" "$test_source_dir/resource.c" "$test_source_dir/shape2d.c" \
    "$test_source_dir/headless_data.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" \
    -Wno-pointer-sign -Wno-missing-braces -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-shape2d-render shape2d.c \
    "$test_source_dir/shape2d_blit.c" "$test_source_dir/shape2d_resources.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/resource.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-missing-braces -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-video-pages video_pages.c -DRESTUNTS_VGA_TEST
run_host_test test-shape2d-planar shape2d.c \
    "$test_source_dir/shape2d_blit.c" "$test_source_dir/shape2d_resources.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/resource.c" "$test_source_dir/legacy.c" \
    -DRESTUNTS_VGA_TEST -Wno-pointer-sign -Wno-missing-braces \
    -Wno-missing-field-initializers -Wno-unused-variable
run_host_test test-shape3d-raster shape3d_prerender.c \
    "$test_source_dir/shape3d_lines.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_tables.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-shape3d-queue shape3d.c \
    "$test_source_dir/full_data.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-shape3d-materials shape3d.c \
    "$test_source_dir/full_data.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-shape3d-vertices shape3d.c \
    -Wno-pointer-sign -Wno-unused-variable
run_host_test test-car-grounding legacy.c -DRESTUNTS_SDL3 -Wno-pointer-sign
run_host_test test-car-shape-lifetime shape3d_car.c \
    "$test_source_dir/owoot.c" "$test_source_dir/owoot_wheels.c" \
    "$test_source_dir/shape3d_resources.c" "$test_source_dir/shape3d.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-skybox-render skybox.c \
    "$test_source_dir/shape3d.c" "$test_source_dir/shape3d_lines.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/physics_collision.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-unused-variable -Wno-missing-field-initializers
run_host_test test-simd-decoding simd.c
run_host_test test-editor-boundaries legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-missing-braces
run_host_test test-menu-main menu_main.c "$test_source_dir/legacy.c" -Wno-pointer-sign
run_host_test test-menu-main menu_main.c "$test_source_dir/legacy.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign
run_host_test test-menu-options menu_options.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -DRESTUNTS_GIT_HASH='"0123456789abcdef0123456789abcdef01234567"' \
    -DRESTUNTS_BUILD_DATE='"Feb 12 1991"' \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-menu-options menu_options.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -DRESTUNTS_SDL3 \
    -DRESTUNTS_GIT_HASH='"0123456789abcdef0123456789abcdef01234567"' \
    -DRESTUNTS_BUILD_DATE='"Feb 12 1991"' \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-menu-navigation menu_opponent.c \
    "$test_source_dir/menu_track.c" "$test_source_dir/legacy.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-menu-navigation menu_opponent.c \
    "$test_source_dir/menu_track.c" "$test_source_dir/legacy.c" \
    "$test_source_dir/full_data.c" "$test_source_dir/full_strings.c" \
    "$test_source_dir/headless_data.c" -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-opponent-menu menu_opponent.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-opponent-menu menu_opponent.c \
    "$test_source_dir/legacy.c" "$test_source_dir/full_data.c" \
    "$test_source_dir/full_strings.c" "$test_source_dir/headless_data.c" \
    -DRESTUNTS_SDL3 -Wno-pointer-sign -Wno-missing-field-initializers
run_host_test test-track-editor legacy.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/full_data.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers -Wno-missing-braces
run_host_test test-track-object trackobj.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/math.c" "$test_source_dir/legacy.c" "$test_source_dir/trkutil.c" \
    -Wno-missing-braces
run_host_test test-body-plane-collision physics_collision.c \
    "$test_source_dir/trackobj.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/headless_trackdata.c" "$test_source_dir/math.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" "$test_source_dir/trkutil.c" \
    -Wno-missing-braces
run_host_test test-underside-collision physics_collision.c \
    "$test_source_dir/trackobj.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/headless_trackdata.c" "$test_source_dir/math.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" "$test_source_dir/trkutil.c" \
    -Wno-missing-braces
run_host_test test-wall-span-collision physics_collision.c \
    "$test_source_dir/trackobj.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/headless_trackdata.c" "$test_source_dir/math.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" "$test_source_dir/trkutil.c" \
    -Wno-missing-braces
run_host_test test-solid-obstacle-collision physics_collision.c \
    "$test_source_dir/trackobj.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/headless_trackdata.c" "$test_source_dir/math.c" \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" "$test_source_dir/trkutil.c" \
    -Wno-missing-braces
run_host_test test-track-collision-boundaries physics_collision.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/trkutil.c" -Wno-missing-braces
run_host_test test-track-setup track_setup.c \
    "$test_source_dir/headless_data.c" "$test_source_dir/headless_trackdata.c" \
    "$test_source_dir/trkutil.c" "$test_source_dir/opponent.c" "$test_source_dir/legacy.c" \
    -Wno-missing-braces -Wno-type-limits -Wno-pointer-sign -Wno-unused-variable \
    "$test_uninitialized_flag"
run_host_test test-track-resource-decoding trackres.c
run_host_test test-ui-file-input ui_dialog.c \
    "$test_source_dir/ui_input.c" "$test_source_dir/legacy.c" \
    "$test_source_dir/strlib.c" "$test_source_dir/resource.c" -Wno-pointer-sign
run_host_test test-ui-dialog ui_dialog.c "$test_source_dir/legacy.c" -Wno-pointer-sign
run_host_test test-ui-dialog ui_dialog.c "$test_source_dir/legacy.c" \
    -D__ANDROID__ -Wno-pointer-sign

run_host_test test-gameplay-residue gamestep.c \
    "$test_source_dir/state.c" "$test_source_dir/physics_grip.c" \
    "$test_source_dir/physics_collision.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/headless_trackdata.c" "$test_source_dir/math.c" \
    "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-sign-compare -Wno-missing-braces

run_host_test test-pixldump-legacy-context ../pixldump/legacy_context.c
run_host_test test-pixldump-native-context legacy.c

run_host_test test-shape3d-render-residue shape3d.c \
    "$test_source_dir/full_data.c" "$test_source_dir/headless_data.c" \
    "$test_source_dir/physics_collision.c" "$test_source_dir/math.c" "$test_source_dir/legacy.c" \
    -Wno-pointer-sign -Wno-missing-field-initializers

run_host_test test-owoot math.c \
    "$test_source_dir/legacy.c" "$test_source_dir/strlib.c" \
    "$test_source_dir/headless_data.c" -Wno-pointer-sign -Wno-missing-field-initializers

run_host_test test-owoot-road legacy.c
run_host_test test-owoot-route owoot_route.c "$test_source_dir/owoot_road.c"

run_host_test test-repldump-owoot legacy.c "$test_source_dir/strlib.c" \
    -I"$test_source_dir" -Wno-pointer-sign -Wno-sign-compare

echo "All host regression tests passed."
