cmake_minimum_required(VERSION 3.25)
project(restunts VERSION 1.0 LANGUAGES C)

option(RESTUNTS_AMIGA "Build the native Amiga 500+ port" OFF)
if(RESTUNTS_AMIGA)
    include(cmake/amiga.cmake)
    return()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    # SDL's native Haiku backend needs C++ link information in the parent scope.
    enable_language(CXX)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# SDL's DOS/POSIX backends need the platform declarations hidden by strict ANSI.
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Legacy 32-bit directory entries cannot hold every filesystem's d_off cookie.
# Enable the 64-bit file ABI before dependencies and all platform/test targets.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()
# Both build-tree and installed binaries must accept a user-rebuilt OPL library.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
if(CMAKE_SHARED_LIBRARY_RPATH_ORIGIN_TOKEN)
    # ELF platforms, including the BSDs, resolve this relative to the executable.
    set(CMAKE_INSTALL_RPATH "${CMAKE_SHARED_LIBRARY_RPATH_ORIGIN_TOKEN}/../lib")
elseif(APPLE)
    # Keep the installed package movable and its OPL dylib replaceable.
    set(CMAKE_INSTALL_RPATH "@loader_path/../lib")
endif()
if(CMAKE_SYSTEM_NAME MATCHES "^(OpenBSD|NetBSD)$")
    # These kernels skip oversized ELF note segments, including the OS identification.
    set(SDL_DLOPEN_NOTES OFF CACHE BOOL "Keep BSD ELF notes compatible with the kernel" FORCE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    # SDL loads desktop drivers by soname. NetBSD's loader only searches
    # /usr/lib by default, even when CMake found X11 or pkgsrc at build time.
    set(restunts_system_library_paths /usr/X11R7/lib /usr/pkg/lib)
    list(APPEND CMAKE_BUILD_RPATH ${restunts_system_library_paths})
    list(APPEND CMAKE_INSTALL_RPATH ${restunts_system_library_paths})
endif()

option(RESTUNTS_SYSTEM_SDL "Use an installed SDL3 instead of the pinned source" OFF)
option(RESTUNTS_GENGHOST_ONLY "Build only the headless ghost generator" OFF)
set(restunts_launcher_supported OFF)
if(CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux|Darwin|FreeBSD|OpenBSD|NetBSD|Haiku)$"
        AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT RESTUNTS_GENGHOST_ONLY)
    set(restunts_launcher_supported ON)
endif()
option(RESTUNTS_BUILD_LAUNCHER "Build the native desktop startup and ghost generator windows" ${restunts_launcher_supported})
if(RESTUNTS_BUILD_LAUNCHER AND NOT restunts_launcher_supported)
    message(FATAL_ERROR "The desktop windows require a desktop game build")
endif()
# Enable languages before dependency directories capture their compiler rules.
if(APPLE)
    # Keep SDL's .m sources as Objective-C when the launcher enables Objective-C++.
    enable_language(OBJC)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS" AND NOT RESTUNTS_GENGHOST_ONLY)
    # The MT-32 synth and ROM discovery layer use C++ on native, Android and Web.
    enable_language(CXX)
endif()
if(RESTUNTS_BUILD_LAUNCHER)
    enable_language(CXX)
    if(WIN32)
        enable_language(RC)
    elseif(APPLE)
        enable_language(OBJCXX)
    endif()
endif()
if(RESTUNTS_GENGHOST_ONLY AND (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "DOS"))
    message(FATAL_ERROR "The standalone ghost generator requires a native SDL3 target")
endif()
if(ANDROID AND RESTUNTS_GENGHOST_ONLY)
    # Static SDL contains C++ Android JNI stubs even with HID support disabled.
    enable_language(CXX)
endif()
if(ANDROID)
    # Keep installable debug APKs fast enough for older ARM phones, with debug symbols.
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    include(cmake/android-build-jobs.cmake)
endif()

if(EMSCRIPTEN OR ANDROID)
    set(restunts_tests_default OFF)
    if(EMSCRIPTEN)
        option(RESTUNTS_WASM_THREADS "Enable browser multi-core rendering (requires cross-origin isolation)" OFF)
        set(SDL_PTHREADS ${RESTUNTS_WASM_THREADS} CACHE BOOL "Browser thread support" FORCE)
        if(RESTUNTS_WASM_THREADS)
            # Every archive, including SDL and the decoders, must use the shared-memory ABI.
            add_compile_options(-pthread)
            add_link_options(-pthread)
        endif()
    endif()
    if(RESTUNTS_SYSTEM_SDL)
        message(FATAL_ERROR "Browser and Android builds require the bundled SDL3")
    endif()
else()
    set(restunts_tests_default ON)
endif()
option(RESTUNTS_BUILD_TESTS "Build native platform regressions" ${restunts_tests_default})
if((EMSCRIPTEN OR ANDROID) AND RESTUNTS_BUILD_TESTS)
    message(FATAL_ERROR "Native regression executables require a separate host build")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    set(restunts_sse2_default OFF)
else()
    set(restunts_sse2_default ON)
endif()
option(RESTUNTS_SSE2 "Allow SSE2 in 32-bit x86 builds (including bundled SDL)" ${restunts_sse2_default})

# Probe the compiler target instead of pointer width: ARM32 also has 32-bit pointers,
# and an x86 multilib compiler can report the host's x86_64 system processor.
include(CheckSymbolExists)
check_symbol_exists(__i386__ "" RESTUNTS_TARGET_X86)
check_symbol_exists(__x86_64__ "" RESTUNTS_TARGET_X64)
if(NOT EMSCRIPTEN AND RESTUNTS_TARGET_X86 AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(RESTUNTS_SSE2)
        add_compile_options(-msse2)
    else()
        if(RESTUNTS_SYSTEM_SDL)
            message(FATAL_ERROR "RESTUNTS_SSE2=OFF requires bundled SDL to control its instruction set")
        endif()
        add_compile_options(-mno-sse -mno-sse2 -mno-avx -mfpmath=387)
        set(SDL_ASSEMBLY OFF CACHE BOOL "No SIMD instructions" FORCE)
        foreach(feature MMX SSE SSE2 SSE3 SSE4_1 SSE4_2 AVX AVX2 AVX512F)
            set(SDL_${feature} OFF CACHE BOOL "No SIMD instructions" FORCE)
        endforeach()
    endif()
elseif(RESTUNTS_TARGET_X64 AND NOT RESTUNTS_SSE2)
    message(FATAL_ERROR "x64 requires SSE2")
endif()

if(WIN32)
    if(NOT MINGW)
        message(FATAL_ERROR "Windows builds require MinGW-w64")
    endif()
    set(restunts_windows_xp 0x0501)
    set(restunts_windows_7 0x0601)
    set(restunts_windows_10 0x0A00)
    if(RESTUNTS_TARGET_X86)
        set(restunts_windows_version ${restunts_windows_xp})
    elseif(RESTUNTS_TARGET_X64)
        set(restunts_windows_version ${restunts_windows_7})
    else()
        set(restunts_windows_version ${restunts_windows_10})
    endif()
    add_compile_definitions(_WIN32_WINNT=${restunts_windows_version} WINVER=${restunts_windows_version})
    add_link_options(-static)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_link_options(-static-libgcc -static-libstdc++)
    endif()
endif()

if(RESTUNTS_SYSTEM_SDL)
    find_package(SDL3 3.4 REQUIRED CONFIG)
else()
    include(FetchContent)
    if(ANDROID AND RESTUNTS_GENGHOST_ONLY)
        # The generator is a separate command-line process, with no Java/SDL activity.
        set(SDL_SHARED OFF CACHE BOOL "Bundle SDL in the Android generator" FORCE)
        set(SDL_STATIC ON CACHE BOOL "Bundle SDL in the Android generator" FORCE)
        set(SDL_ANDROID_JAR OFF CACHE BOOL "The generator has no SDL Java glue" FORCE)
        foreach(subsystem AUDIO VIDEO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI POWER
                SENSOR DIALOG TRAY NOTIFICATION)
            set(SDL_${subsystem} OFF CACHE BOOL "Headless Android generator" FORCE)
        endforeach()
    elseif(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "Android JNI loads shared SDL" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "Android JNI loads shared SDL" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "Bundle SDL statically")
        set(SDL_STATIC ON CACHE BOOL "Bundle SDL statically")
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL tests")
    set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests")
    set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples")
    set(SDL_INSTALL OFF CACHE BOOL "Only install the game")
    # Upstream DOS support and the gameport/Sound Blaster IRQ fix used by
    # stuntsengine's DOS port. Pin all platforms to the same SDL implementation.
    FetchContent_Declare(SDL3
        URL
        https://github.com/libsdl-org/SDL/archive/015489c672f24feed28c2aa2cdd6176df95329f3.tar.gz
        URL_HASH SHA256=50c373115702974928c760f6e752aae33b69dcc51b4c2d909c35b30f41283a52
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)
    if(SDL_X11 AND NOT SDL_X11_XRANDR AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # Pinned SDL leaves a fallback label outside its disabled XRandR block.
        foreach(restunts_sdl_target SDL3-static SDL3-shared)
            if(TARGET ${restunts_sdl_target})
                set_property(SOURCE "${SDL3_SOURCE_DIR}/src/video/x11/SDL_x11toolkit.c"
                    TARGET_DIRECTORY ${restunts_sdl_target} APPEND PROPERTY
                    COMPILE_OPTIONS -Wno-unused-label)
            endif()
        endforeach()
    endif()
    if(RESTUNTS_APPIMAGE_REQUIRE_PULSEAUDIO)
        # SDL's requested backend can still be disabled when SDK headers are missing.
        # Require the detected driver source so a silent ALSA-only build cannot ship.
        get_target_property(restunts_sdl_sources SDL3::SDL3 SOURCES)
        list(FILTER restunts_sdl_sources INCLUDE
            REGEX "/audio/pulseaudio/SDL_pulseaudio\\.c$")
        if(NOT restunts_sdl_sources)
            message(FATAL_ERROR "The AppImage SDK must build SDL's PulseAudio backend")
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku" AND CMAKE_HAIKU_SECONDARY_ARCH)
        # Hybrid Haiku's implicit paths prefer the primary GCC2 OpenGL kit.
        # Use the matching Mesa C++ headers alongside the secondary libraries.
        set(restunts_haiku_header_roots ${CMAKE_SYSTEM_INCLUDE_PATH})
        list(FILTER restunts_haiku_header_roots INCLUDE
            REGEX "/${CMAKE_HAIKU_SECONDARY_ARCH}$")
        find_path(RESTUNTS_HAIKU_OPENGL_HEADER_ROOT opengl/GLView.h
            HINTS ${restunts_haiku_header_roots} NO_DEFAULT_PATH REQUIRED)
        set(restunts_haiku_opengl_kit "${RESTUNTS_HAIKU_OPENGL_HEADER_ROOT}/opengl")
        if(NOT EXISTS "${restunts_haiku_opengl_kit}/OpenGLKit.h")
            message(FATAL_ERROR "Matching Haiku OpenGLKit.h and GLView.h are required")
        endif()
        target_include_directories(SDL3-static BEFORE PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${restunts_haiku_opengl_kit}>")
        # Qualified includes also need the secondary root first. An explicit
        # system option overrides its position in the compiler's implicit paths.
        target_compile_options(SDL3-static PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-isystem \"${RESTUNTS_HAIKU_OPENGL_HEADER_ROOT}\">")
    endif()
endif()

set(core src/restunts/c)
set(platform src/restunts/platform/sdl3)
set(common_sources
    fileio.c file_car.c file_decomp.c legacy.c
    resource.c math/math.c math/math_rectangles.c memmgr.c
    strlib.c numlib.c kevinrnd.c replinit.c
    gameinit.c gamestep.c simd.c car/carsetup.c
    trkutil.c track/trackres.c headless/headless_data.c headless/headless_trackdata.c
    state.c race/race_route.c physics/physics_grip.c opponent/opponent.c
    opponent/unit_opponent.c opponent/unit_launch.c opponent/unit_profiles.c
    opponent/unit_learning_cache.c opponent/unit_learning_key.c
    extended/extended_unit_route.c
    opponent/unit_planner.c opponent/unit_planner_workers.c
    opponent/unit_planner_math.c opponent/unit_planner_guidance.c
    opponent/unit_route_graph.c extended/extended_unit_route_graph.c extended/extended_unit_penalty.c
    extended/extended_unit_checkpoint.c extended/extended_physics_snapshot.c
    extended/extended_unit_shortcut.c opponent/unit_pilot.c opponent/unit_pilot_learning.c
    opponent/unit_pilot_banks.c
    opponent/unit_pilot_trace.c
    opponent/unit_pilot_candidates.c opponent/unit_pilot_controls.c opponent/unit_pilot_pulses.c
    extended/extended_barrel_search.c
    track/track_setup.c stateply/stateply.c stateply/stateply_motion.c stateply/stateply_contact.c
    physics/physics_collision.c physics/physics_suspension.c statecrs.c statecar.c
    track/trackobj.c track/trackobj_road.c track/trackobj_stunts.c track/trackobj_queries.c
    owoot/owoot.c owoot/owoot_wheels.c owoot/owoot_road.c owoot/owoot_route.c
    driving_rules.c replay/replay.c stateio.c extended/extended_cstrk.c
    extended/extended_cstrk_horizon.c
    extended/extended_csrpl.c extended/extended_cshig.c extended/extended_replay.c
    extended/extended_track.c extended/extended_checkpoint.c extended/extended_checkpoint_shape.c
    extended/extended_geometry.c extended/extended_opponent.c extended/extended_roll_ramp.c
    extended/extended_pine_trees.c extended/extended_wheat_field.c extended/extended_crop_fields.c
    extended/extended_trackobj.c
    extended/extended_trackobj_solids.c extended/extended_stateply.c
    extended/extended_physics_collision.c extended/extended_statecrs.c
    extended/extended_track_setup.c extended/extended_race_route.c extended/extended_gameinit.c
    extended/extended_gamestep.c
    extended/extended_trkutil.c extended/extended_fileio.c extended/extended_owoot_route.c
    extended/extended_owoot_gates.c
    extended/extended_owoot_jump.c extended/extended_owoot_road.c hires/track_geometry.c
    extended/extended_math.c)
set(game_sources
    restunts.c startup.c command_line.c fatal.c timing.c game_input.c game_input_shortcuts.c
    ui/ui_text.c ui/ui_input.c ui/ui_dialog.c ui/ui_file_dialog.c ui/ui_disk_dialog.c intro.c
    highscore/highscore.c highscore/highscore_results.c highscore/highscore_results_text.c
    replay/replay_record.c
    replay/replay_viewer.c replay/replay_toolbar.c replay/replay_menu.c replay/replay_scrub.c
    race/race.c race/race_rewind.c race/race_presentation.c ghost/ghost.c ghost/ghost_line.c
    race/race_unit.c opponent/unit_opponent_assets.c
    extended/extended_file.c extended/extended_replay_runtime.c extended/extended_editor.c
    extended/extended_race_stats.c extended/extended_ghost.c extended/extended_validation.c
    extended/extended_horizon.c extended/extended_horizon_skybox.c
    extended/extended_highscore.c extended/extended_frame.c extended/extended_frame_objects.c
    extended/extended_frame_terrain.c extended/extended_track_preview.c
    extended/extended_menu_runtime.c extended/extended_highscore_ui.c extended/extended_ui_dialog.c
    extended/extended_replay_ui.c extended/extended_ghost_runtime.c race/race_resources.c
    dashboard.c menu/menu_common.c menu/menu_main.c menu/menu_options.c menu/menu_track.c
    menu/menu_track_editor.c menu/menu_track_editor_map.c menu/menu_track_editor_render.c
    menu/menu_track_editor_input.c menu/menu_opponent.c opponent/opponent_portrait.c
    opponent/opponent_animation.c menu/menu_background.c menu/menu_car.c audio/audio_engine.c
    audio/audio_channels.c audio/audio_context.c audio/audio_resources.c audio/audio_car.c
    audio/audio_sequence.c shape3d/shape3d_native.c shape3d/shape3d.c shape3d/shape3d_queue.c shape3d/shape3d_primitives.c
    shape3d/shape3d_render.c shape3d/shape3d_resources.c shape3d/shape3d_car.c
    shape3d/shape3d_prerender.c shape3d/shape3d_polygon.c shape3d/shape3d_lines.c
    shape3d/shape3d_line_setup.c shape2d/shape2d.c shape2d/shape2d_image.c shape2d/shape2d_blit.c
    shape2d/shape2d_window.c video/video_pages.c hires/hires.c hires/hires_raster.c hires/hypervision.c
    hires/render_workers.c hires/frame_interpolation.c hires/shape3d_hires.c
    hires/shape3d_geometry.c
    hires/shape3d_car_ground.c hires/shape3d_hires_draw.c hires/shape3d_mesh.c
    hires/shape3d_hires_shadow_model.c hires/shape3d_hires_shadow.c
    shape2d/shape2d_resources.c heapsort.c frame/frame.c frame/frame_camera.c frame/frame_car.c
    frame/frame_tiles.c frame/frame_terrain.c frame/frame_objects.c hires/frame_adaptive.c
    skybox.c hires/skybox_hires.c frame/frame_overlay.c intro_render.c track/track_preview.c
    data/full_data.c data/full_strings.c data/full_tables.c
    hires/settings_startup.c hires/settings_input.c hires/race_view.c hires/race_presentation.c
    hires/ghost_render.c extended/extended_frame_tiles.c extended/extended_frame_hills.c
    extended/extended_frame_render_objects.c extended/extended_frame_render_terrain.c
    extended/extended_frame_render_ground.c extended/extended_frame_precise.c
    extended/extended_frame_cache.c
    extended/extended_track_preview_hires.c hires/track_preview_mode.c hires/track_menu_preview.c
    hires/frame_visibility.c hires/frame_snapshot.c hires/frame_integration.c
    hires/frame_car_render.c hires/frame_scene.c hires/frame_terrain.c hires/frame_particles.c
    hires/frame_overlay.c hires/intro_presentation.c hires/track_preview_scene.c
    hires/opponent_portrait_artwork.c hires/opponent_animation_artwork.c hires/menu_artwork.c
    hires/showroom.c hires/showroom_presentation.c hires/shape3d_pipeline.c hires/shape3d_queue.c hires/shape3d_render.c
    hires/highscore_animation.c hires/skybox_scene.c video/video_framebuffer.c hires/video_framebuffer.c frame/frame_fps.c frame/frame_mph.c
    frame/frame_context.c
    supersight/frame_scene.c supersight/frame_visibility.c supersight/frame_integration.c
    supersight/frame_terrain.c supersight/settings.c supersight/frame_overlay.c
    supersight/race_view.c supersight/track_geometry.c
    extended/extended_frame_layout.c)
list(TRANSFORM common_sources PREPEND "${core}/")
list(TRANSFORM game_sources PREPEND "${core}/")
set(platform_files ${platform}/file.c ${platform}/car_zip.c third_party/puff/puff.c)
set(platform_common ${platform}/memory.c ${platform_files} ${platform}/runtime.c
    ${core}/extended/extended_file_dialog.c)
set(platform_game ${platform}/asset_path.c ${platform}/video.c ${platform}/input.c
    ${platform}/video_mode.c ${platform}/video_mode_bitmaps.c ${platform}/video_mode_primitives.c
${platform}/timer.c
    ${platform}/audio.c ${platform}/music.c ${platform}/pc_speaker_driver.c
    ${platform}/pc_speaker_audio.c ${platform}/touch.c ${platform}/soft_keyboard.c)

execute_process(COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE RESTUNTS_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT RESTUNTS_GIT_HASH)
    set(RESTUNTS_GIT_HASH "source-archive")
endif()

if(EMSCRIPTEN)
    set(restunts_asset_directory "/assets")
else()
    set(restunts_asset_directory "${CMAKE_SOURCE_DIR}/assets")
endif()

add_library(restunts_options INTERFACE)
target_compile_definitions(restunts_options INTERFACE RESTUNTS_SDL3
    RESTUNTS_GIT_HASH="${RESTUNTS_GIT_HASH}"
    RESTUNTS_CAR_DIRECTORY="${restunts_asset_directory}/cars")
target_include_directories(restunts_options INTERFACE "${CMAKE_SOURCE_DIR}/${core}")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(restunts_options INTERFACE -fsigned-char -fno-strict-aliasing
        -Wall -Wextra "$<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>"
        -Wno-missing-field-initializers -Wno-missing-braces)
endif()
target_link_libraries(restunts_options INTERFACE SDL3::SDL3 m)

function(restunts_add_genghost)
    # The optimizer shares repldump's physics and owns its command-line entry point.
    add_executable(genghost ${common_sources} ${core}/headless/headless.c ${platform_common}
        ${core}/extended/extended_replay_runtime.c
        src/restunts/genghost/main.c src/restunts/genghost/extended_source.c
        src/restunts/genghost/simulation.c src/restunts/genghost/extended_simulation.c
        src/restunts/genghost/routes.c src/restunts/genghost/extended_routes.c
        src/restunts/genghost/extended_checkpoints.c
        src/restunts/genghost/search.c src/restunts/genghost/extended_search.c
        src/restunts/genghost/guide.c src/restunts/genghost/extended_guide.c
        src/restunts/genghost/parallel.c src/restunts/genghost/extended_parallel.c
        src/restunts/genghost/maneuvers.c src/restunts/genghost/extended_maneuvers.c
        src/restunts/genghost/state_search.c src/restunts/genghost/extended_state_search.c)
    target_compile_definitions(genghost PRIVATE RESTUNTS_HEADLESS RESTUNTS_EXTENDED_REPLAY)
    target_link_libraries(genghost PRIVATE restunts_options)
    if(ANDROID)
        # Android permits execution from an APK's extracted native-library directory.
        # This is still a PIE executable, named as a library solely for APK packaging.
        # AGP uses the executable's parent directory to identify its ABI.
        # Keep each ABI and variant in its own CMake build tree.
        set_target_properties(genghost PROPERTIES PREFIX "lib" OUTPUT_NAME genghost_exec
            SUFFIX ".so"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/genghost-bin/${ANDROID_ABI}")
    endif()
    install(TARGETS genghost RUNTIME DESTINATION bin COMPONENT Runtime)
    install(FILES docs/genghost.md docs/genghost-state-planner.md
        DESTINATION share/docs/restunts COMPONENT Runtime)
endfunction()

if(RESTUNTS_GENGHOST_ONLY)
    restunts_add_genghost()
    if(NOT RESTUNTS_SYSTEM_SDL)
        install(FILES "${SDL3_SOURCE_DIR}/LICENSE.txt"
            DESTINATION share/licenses/restunts RENAME SDL-LICENSE.txt COMPONENT Runtime)
    endif()
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    set(NUKED_OPL2_SSE2 ${RESTUNTS_SSE2} CACHE BOOL "Match the game's x86 instruction set" FORCE)
    add_subdirectory(third_party/nuked-opl2-lite)
    add_library(restunts_opl ALIAS nuked_opl2)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    include(cmake/mt32emu.cmake)
endif()

# The portable VP8/WebM decoder is a separate static archive for browser relinking.
include(cmake/webm.cmake)

# Keep the decoder objects in the application archive, including browser relink kits.
add_library(restunts_vorbis OBJECT third_party/stb/stb_vorbis.c)
target_compile_definitions(restunts_vorbis PRIVATE STB_VORBIS_NO_PUSHDATA_API)
add_library(restunts_game STATIC ${common_sources} ${game_sources} ${platform_common} ${platform_game}
    $<TARGET_OBJECTS:restunts_vorbis>)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    target_sources(restunts_game PRIVATE ${platform}/audio_worker.c
        ${platform}/audio_commands.c ${platform}/audio_resources_worker.c
        ${platform}/audio_completions.c)
    target_compile_definitions(restunts_game PUBLIC RESTUNTS_AUDIO_WORKER)
endif()
if(DJGPP)
    # PC Speaker PWM shares PIT0 with the clock used by SDL's DOS timer.
    target_link_options(restunts_game INTERFACE "-Wl,--wrap=uclock")
endif()
if(TARGET mt32emu)
    target_sources(restunts_game PRIVATE ${platform}/mt32_roms.cpp
        ${platform}/mt32_driver.c ${platform}/mt32_audio.c)
    target_link_libraries(restunts_game PUBLIC mt32emu)
    # Legacy AppImage runtimes predate the sized-delete ABI emitted in C++14+.
    # A minimum feature request would retain a newer host compiler default.
    set_target_properties(restunts_game PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    target_compile_definitions(restunts_game PUBLIC RESTUNTS_MT32)
endif()

target_include_directories(restunts_game PRIVATE "${CMAKE_SOURCE_DIR}/third_party/stb")
set(menu_backgrounds main showroom opponent)
set(music_tracks titl slct over vict)
set(restunts_horizon_files Antarctica.png)
include(cmake/car-archives.cmake)
target_compile_definitions(restunts_game PRIVATE RESTUNTS_FULL
    RESTUNTS_EDITOR_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
    RESTUNTS_MENU_DIRECTORY="${restunts_asset_directory}/menus"
    RESTUNTS_SKYBOX_DIRECTORY="${restunts_asset_directory}/skyboxes"
    RESTUNTS_HORIZON_DIRECTORY="${restunts_asset_directory}/horizons"
    RESTUNTS_OPPONENT_DIRECTORY="${restunts_asset_directory}/opponents"
    RESTUNTS_MUSIC_DIRECTORY="${restunts_asset_directory}/music")
target_link_libraries(restunts_game PUBLIC restunts_options PRIVATE restunts_webm)
if(TARGET restunts_opl)
    target_link_libraries(restunts_game PRIVATE restunts_opl)
endif()

if(EMSCRIPTEN)
    # Retain the entry objects for recipients relinking a modified LGPL library.
    add_library(restunts_entry OBJECT ${platform}/main.c)
    target_compile_definitions(restunts_entry PRIVATE RESTUNTS_FULL)
    target_link_libraries(restunts_entry PRIVATE restunts_game)
    add_executable(restunts $<TARGET_OBJECTS:restunts_entry>)
elseif(ANDROID)
    add_library(restunts SHARED ${platform}/main.c)
    target_compile_definitions(restunts PRIVATE RESTUNTS_FULL)
    set_target_properties(restunts PROPERTIES OUTPUT_NAME main)
else()
    add_executable(restunts ${platform}/main.c)
    target_compile_definitions(restunts PRIVATE RESTUNTS_FULL)
endif()
target_link_libraries(restunts PRIVATE restunts_game)
if(ANDROID AND TARGET mt32emu)
    # JNI exports have no static call sites; retain their object in libmain.
    target_sources(restunts PRIVATE ${platform}/mt32_roms_jni.cpp)
endif()

foreach(component Runtime Tests)
    set(exclude_from_default)
    if(component STREQUAL "Tests")
        set(exclude_from_default EXCLUDE_FROM_ALL)
    endif()
    install(FILES third_party/stb/LICENSE
        DESTINATION share/licenses/restunts RENAME stb-LICENSE
        COMPONENT ${component} ${exclude_from_default})
    install(FILES third_party/puff/LICENSE
        DESTINATION share/licenses/restunts RENAME puff-LICENSE
        COMPONENT ${component} ${exclude_from_default})
    install(FILES THIRD-PARTY-NOTICES.txt
        DESTINATION . COMPONENT ${component} ${exclude_from_default})
endforeach()

if(TARGET nuked_opl2 AND NOT EMSCRIPTEN)
    configure_file(cmake/nuked-build-info.txt.in "${CMAKE_CURRENT_BINARY_DIR}/nuked-build-info.txt" @ONLY)
    foreach(component Runtime Tests)
        set(exclude_from_default)
        if(component STREQUAL "Tests")
            set(exclude_from_default EXCLUDE_FROM_ALL)
        endif()
        install(FILES third_party/nuked-opl2-lite/LICENSE
            DESTINATION share/licenses/restunts RENAME Nuked-OPL2-LICENSE
            COMPONENT ${component} ${exclude_from_default})
        install(FILES third_party/nuked-opl2-lite/opl2.c third_party/nuked-opl2-lite/opl2.h
            third_party/nuked-opl2-lite/CMakeLists.txt third_party/nuked-opl2-lite/LICENSE
            third_party/nuked-opl2-lite/README.md "${CMAKE_CURRENT_BINARY_DIR}/nuked-build-info.txt"
            DESTINATION share/restunts/nuked-opl2-lite COMPONENT ${component} ${exclude_from_default})
        # Preserve the parent build controls as source context, alongside the
        # standalone library build that recipients can use without the game.
        install(FILES CMakeLists.txt
            DESTINATION share/restunts/nuked-opl2-lite/build-context
            COMPONENT ${component} ${exclude_from_default})
        install(FILES cmake/nuked-build-info.txt.in
            DESTINATION share/restunts/nuked-opl2-lite/build-context/cmake
            COMPONENT ${component} ${exclude_from_default})
        install(DIRECTORY cmake/toolchains/
            DESTINATION share/restunts/nuked-opl2-lite/build-context/cmake/toolchains
            COMPONENT ${component} ${exclude_from_default} FILES_MATCHING PATTERN "*.cmake" PATTERN "*.h")
    endforeach()
endif()
if(NOT RESTUNTS_SYSTEM_SDL AND NOT EMSCRIPTEN)
    foreach(component Runtime Tests)
        set(exclude_from_default)
        if(component STREQUAL "Tests")
            set(exclude_from_default EXCLUDE_FROM_ALL)
        endif()
        install(FILES "${SDL3_SOURCE_DIR}/LICENSE.txt"
            DESTINATION share/licenses/restunts RENAME SDL-LICENSE.txt
            COMPONENT ${component} ${exclude_from_default})
    endforeach()
endif()

include(cmake/track-editor.cmake)

if(ANDROID)
    # Gradle packages native libraries and app assets; install the editor, source and licenses.
    install(FILES docs/android.md DESTINATION share/docs/restunts COMPONENT Runtime)
    foreach(horizon_file IN LISTS restunts_horizon_files)
        install(FILES "assets/horizons/${horizon_file}" DESTINATION horizons COMPONENT Runtime)
    endforeach()
    return()
endif()

if(EMSCRIPTEN)
    include(cmake/wasm.cmake)
    # Dump tools, native tests and shared-library installation are host-only.
    return()
endif()

add_executable(repldump ${common_sources} ${core}/headless/headless.c ${platform_common}
    ${platform}/main.c src/restunts/repldump/repldump.c)
target_compile_definitions(repldump PRIVATE RESTUNTS_HEADLESS)
target_link_libraries(repldump PRIVATE restunts_options)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    restunts_add_genghost()
endif()

add_executable(pixldump ${platform}/main.c src/restunts/pixldump/pixldump.c
    src/restunts/pixldump/murmur3.c src/restunts/pixldump/legacy_context.c)
target_compile_definitions(pixldump PRIVATE RESTUNTS_PIXLDUMP)
target_link_libraries(pixldump PRIVATE restunts_game)

install(TARGETS restunts repldump pixldump RUNTIME DESTINATION bin COMPONENT Runtime)
foreach(car_archive IN LISTS restunts_car_archives)
    install(FILES "${restunts_car_archive_directory}/${car_archive}"
        DESTINATION bin/cars COMPONENT Runtime)
endforeach()
install(FILES assets/tracks/README.TXT DESTINATION bin/tracks COMPONENT Runtime)
foreach(horizon_file IN LISTS restunts_horizon_files)
    set(horizon_install_name "${horizon_file}")
    if(CMAKE_SYSTEM_NAME STREQUAL "DOS" AND horizon_file STREQUAL "Antarctica.png")
        set(horizon_install_name antarct.png)
    endif()
    install(FILES "assets/horizons/${horizon_file}" DESTINATION bin/horizons
        RENAME "${horizon_install_name}" COMPONENT Runtime)
endforeach()
foreach(music IN LISTS music_tracks)
    install(FILES "assets/music/${music}.ogg" DESTINATION bin/music COMPONENT Runtime OPTIONAL)
endforeach()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    install(PROGRAMS tools/scripts/run-macos.sh DESTINATION . RENAME run-restunts.sh COMPONENT Runtime)
endif()
install(FILES docs/sdl3.md DESTINATION share/docs/restunts COMPONENT Runtime)
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    install(FILES docs/haiku.md DESTINATION share/docs/restunts COMPONENT Runtime)
endif()
foreach(background IN LISTS menu_backgrounds)
    install(FILES "assets/menus/${background}.png" DESTINATION bin/menus COMPONENT Runtime)
endforeach()
if(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    set(restunts_opponent_install_directory opponent)
    set(restunts_animation_install_directory opponent/anim)
else()
    set(restunts_opponent_install_directory opponents)
    set(restunts_animation_install_directory opponents/animations)
endif()
foreach(opponent RANGE 1 6)
    install(FILES "assets/opponents/game/opp${opponent}.png"
        DESTINATION bin/${restunts_opponent_install_directory}/game COMPONENT Runtime)
    foreach(outcome win lose)
        install(FILES "assets/opponents/animations/opp${opponent}${outcome}.webm"
            DESTINATION bin/${restunts_animation_install_directory} COMPONENT Runtime OPTIONAL)
    endforeach()
endforeach()
set(skybox_themes desert tropical alpine city country)
install(FILES "assets/opponents/game/opp7.png"
    DESTINATION bin/${restunts_opponent_install_directory}/game COMPONENT Runtime)
set(skybox_images scen sce2 sce3 sce4)
foreach(theme RANGE 0 4)
    list(GET skybox_themes ${theme} theme_name)
    foreach(image RANGE 0 3)
        list(GET skybox_images ${image} image_name)
        install(FILES "assets/skyboxes/${theme_name}-${image_name}.png"
            DESTINATION bin/skyboxes RENAME "sky${theme}-${image}.png" COMPONENT Runtime)
    endforeach()
endforeach()
if(TARGET nuked_opl2)
    install(TARGETS nuked_opl2
        RUNTIME DESTINATION bin COMPONENT Runtime
        LIBRARY DESTINATION lib COMPONENT Runtime)
endif()

if(RESTUNTS_BUILD_LAUNCHER)
    include(cmake/launcher.cmake)
endif()

if(RESTUNTS_BUILD_TESTS)
    add_executable(test-unit-launch src/restunts/tests/test-unit-launch.c
        ${core}/opponent/unit_launch.c)
    target_compile_options(test-unit-launch PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-launch PRIVATE restunts_options)
    add_test(NAME unit-launch COMMAND test-unit-launch)
    add_executable(test-unit-collision src/restunts/tests/test-unit-collision.c
        ${core}/opponent/unit_collision.c)
    target_compile_definitions(test-unit-collision PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-unit-collision PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-collision PRIVATE m)
    add_test(NAME unit-collision COMMAND test-unit-collision)
    add_executable(test-unit-profiles src/restunts/tests/test-unit-profiles.c
        ${core}/opponent/unit_profiles.c)
    target_compile_definitions(test-unit-profiles PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-unit-profiles PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-profiles PRIVATE m)
    add_test(NAME unit-profiles COMMAND test-unit-profiles)
    add_executable(test-unit-learning-cache src/restunts/tests/test-unit-learning-cache.c
        ${core}/opponent/unit_learning_cache.c)
    target_compile_definitions(test-unit-learning-cache PRIVATE RESTUNTS_SDL3 RESTUNTS_HEADLESS)
    target_compile_options(test-unit-learning-cache PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-learning-cache PRIVATE m)
    add_test(NAME unit-learning-cache COMMAND test-unit-learning-cache)
    add_executable(test-unit-pilot-controls src/restunts/tests/test-unit-pilot-controls.c
        ${core}/opponent/unit_pilot_controls.c)
    target_compile_options(test-unit-pilot-controls PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-pilot-controls PRIVATE restunts_options)
    add_test(NAME unit-pilot-controls COMMAND test-unit-pilot-controls)
    add_executable(test-extended-barrel-search src/restunts/tests/test-extended-barrel-search.c
        ${core}/extended/extended_barrel_search.c)
    target_compile_options(test-extended-barrel-search PRIVATE -UNDEBUG)
    target_link_libraries(test-extended-barrel-search PRIVATE restunts_options)
    add_test(NAME extended-barrel-search COMMAND test-extended-barrel-search)
    add_executable(test-unit-pilot-pulses src/restunts/tests/test-unit-pilot-pulses.c
        ${core}/opponent/unit_pilot_pulses.c ${core}/extended/extended_barrel_search.c)
    target_compile_definitions(test-unit-pilot-pulses PRIVATE
        RESTUNTS_SDL3 RESTUNTS_HEADLESS RESTUNTS_EXTENDED_REPLAY)
    target_compile_options(test-unit-pilot-pulses PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-pilot-pulses PRIVATE m)
    add_test(NAME unit-pilot-pulses COMMAND test-unit-pilot-pulses)
    add_executable(test-unit-shortcut src/restunts/tests/test-unit-shortcut.c
        ${core}/extended/extended_unit_shortcut.c)
    target_compile_options(test-unit-shortcut PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-shortcut PRIVATE restunts_options)
    add_test(NAME unit-shortcut COMMAND test-unit-shortcut)
    add_executable(test-unit-route-graph src/restunts/tests/test-unit-route-graph.c
        ${core}/opponent/unit_route_graph.c)
    target_compile_options(test-unit-route-graph PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-route-graph PRIVATE restunts_options)
    add_test(NAME unit-route-graph COMMAND test-unit-route-graph)
    add_executable(test-unit-checkpoint src/restunts/tests/test-unit-checkpoint.c
        ${core}/extended/extended_unit_checkpoint.c ${core}/extended/extended_checkpoint.c
        ${core}/extended/extended_cstrk.c ${core}/extended/extended_cstrk_horizon.c)
    target_link_libraries(test-unit-checkpoint PRIVATE restunts_options)
    target_compile_options(test-unit-checkpoint PRIVATE -UNDEBUG)
    add_test(NAME unit-checkpoint COMMAND test-unit-checkpoint)
    add_executable(test-unit-pilot src/restunts/tests/test-unit-pilot.c
        ${core}/opponent/unit_pilot.c ${core}/opponent/unit_pilot_learning.c
        ${core}/opponent/unit_pilot_banks.c
        ${core}/opponent/unit_pilot_trace.c
        ${core}/opponent/unit_pilot_candidates.c ${core}/opponent/unit_pilot_pulses.c
        ${core}/extended/extended_barrel_search.c)
    target_compile_definitions(test-unit-pilot PRIVATE RESTUNTS_SDL3)
    target_include_directories(test-unit-pilot PRIVATE
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_options(test-unit-pilot PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-pilot PRIVATE m)
    add_test(NAME unit-pilot COMMAND test-unit-pilot)
    add_executable(test-unit-pilot-continuation src/restunts/tests/test-unit-pilot-continuation.c
        ${core}/opponent/unit_pilot.c ${core}/opponent/unit_pilot_learning.c
        ${core}/opponent/unit_pilot_banks.c
        ${core}/opponent/unit_pilot_trace.c
        ${core}/opponent/unit_pilot_candidates.c ${core}/opponent/unit_pilot_pulses.c
        ${core}/extended/extended_barrel_search.c)
    get_target_property(unit_pilot_continuation_sources test-unit-pilot-continuation SOURCES)
    add_executable(test-unit-pilot-continuation-slow-clock ${unit_pilot_continuation_sources})
    # Exercise instrumented CPU guards against a proportionally slower fixture clock.
    set(restunts_unit_slow_clock_multiplier 40)
    target_compile_definitions(test-unit-pilot-continuation-slow-clock PRIVATE
        RESTUNTS_UNIT_WORK_TIME_MULTIPLIER=${restunts_unit_slow_clock_multiplier})
    foreach(unit_continuation_target test-unit-pilot-continuation
            test-unit-pilot-continuation-slow-clock)
        target_compile_definitions(${unit_continuation_target} PRIVATE RESTUNTS_SDL3)
        target_include_directories(${unit_continuation_target} PRIVATE
            $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_options(${unit_continuation_target} PRIVATE -UNDEBUG)
        target_link_libraries(${unit_continuation_target} PRIVATE m)
        string(REGEX REPLACE "^test-" "" unit_continuation_test "${unit_continuation_target}")
        add_test(NAME ${unit_continuation_test} COMMAND ${unit_continuation_target})
    endforeach()
    get_target_property(unit_fixed_work_sources test-unit-pilot SOURCES)
    list(REMOVE_ITEM unit_fixed_work_sources src/restunts/tests/test-unit-pilot.c)
    add_executable(test-unit-fixed-work src/restunts/tests/test-unit-fixed-work.c
        ${unit_fixed_work_sources})
    target_compile_definitions(test-unit-fixed-work PRIVATE
        RESTUNTS_SDL3 RESTUNTS_UNIT_TEST_FIXED_WORK=1)
    target_include_directories(test-unit-fixed-work PRIVATE
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_options(test-unit-fixed-work PRIVATE -UNDEBUG)
    target_link_libraries(test-unit-fixed-work PRIVATE m)
    add_test(NAME unit-fixed-work COMMAND test-unit-fixed-work)
    foreach(unit_planner_variant threaded sync fixed-work)
        add_executable(test-unit-planner-${unit_planner_variant}
            src/restunts/tests/test-unit-planner.c
            ${core}/opponent/unit_planner.c ${core}/opponent/unit_planner_workers.c
            ${core}/opponent/unit_planner_math.c
            ${core}/opponent/unit_planner_guidance.c)
        target_link_libraries(test-unit-planner-${unit_planner_variant} PRIVATE restunts_options)
        target_compile_options(test-unit-planner-${unit_planner_variant} PRIVATE -UNDEBUG)
        if(unit_planner_variant STREQUAL "sync")
            target_compile_definitions(test-unit-planner-sync PRIVATE RESTUNTS_UNIT_SYNC_ONLY)
        elseif(unit_planner_variant STREQUAL "fixed-work")
            target_compile_definitions(test-unit-planner-fixed-work PRIVATE
                RESTUNTS_UNIT_TEST_FIXED_WORK=1)
        endif()
        add_test(NAME unit-planner-${unit_planner_variant}
            COMMAND test-unit-planner-${unit_planner_variant})
    endforeach()
    add_executable(test-unit-opponent-assets src/restunts/tests/test-unit-opponent-assets.c
        ${core}/opponent/unit_opponent_assets.c)
    target_link_libraries(test-unit-opponent-assets PRIVATE restunts_options)
    target_compile_options(test-unit-opponent-assets PRIVATE -UNDEBUG)
    add_test(NAME unit-opponent-assets COMMAND test-unit-opponent-assets)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
        add_executable(test-audio-resources-worker src/restunts/tests/test-audio-resources-worker.c
            ${platform}/memory.c ${platform}/audio_resources_worker.c)
        target_link_libraries(test-audio-resources-worker PRIVATE restunts_options)
        target_compile_definitions(test-audio-resources-worker PRIVATE RESTUNTS_AUDIO_WORKER)
        target_compile_options(test-audio-resources-worker PRIVATE -UNDEBUG)
        add_test(NAME audio-resources-worker COMMAND test-audio-resources-worker)
        add_executable(test-audio-completions src/restunts/tests/test-audio-completions.c
            ${platform}/audio_completions.c)
        target_link_libraries(test-audio-completions PRIVATE restunts_options)
        target_compile_definitions(test-audio-completions PRIVATE RESTUNTS_AUDIO_WORKER)
        target_compile_options(test-audio-completions PRIVATE -UNDEBUG)
        add_test(NAME audio-completions COMMAND test-audio-completions)
        add_executable(test-audio-worker src/restunts/tests/test-audio-worker.c)
        target_link_libraries(test-audio-worker PRIVATE restunts_game)
        target_compile_options(test-audio-worker PRIVATE -UNDEBUG)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-worker-test")
        add_test(NAME audio-worker COMMAND test-audio-worker "${CMAKE_SOURCE_DIR}/stunts")
        set_tests_properties(audio-worker PROPERTIES TIMEOUT 20
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-worker-test"
            ENVIRONMENT "RESTUNTS_AUDIO_TRACE=")
    endif()
    add_executable(test-pc-speaker-driver src/restunts/tests/test-pc-speaker-driver.c
        ${platform}/pc_speaker_driver.c)
    target_link_libraries(test-pc-speaker-driver PRIVATE restunts_options)
    target_compile_options(test-pc-speaker-driver PRIVATE -UNDEBUG)
    add_test(NAME pc-speaker-driver COMMAND test-pc-speaker-driver)
    add_executable(test-pc-speaker-audio src/restunts/tests/test-pc-speaker-audio.c
        ${platform}/pc_speaker_driver.c ${platform}/pc_speaker_audio.c)
    target_link_libraries(test-pc-speaker-audio PRIVATE restunts_options)
    target_compile_options(test-pc-speaker-audio PRIVATE -UNDEBUG)
    add_test(NAME pc-speaker-audio COMMAND test-pc-speaker-audio)
    add_executable(test-pc-speaker-sequence src/restunts/tests/test-pc-speaker-sequence.c)
    target_link_libraries(test-pc-speaker-sequence PRIVATE restunts_game)
    target_compile_options(test-pc-speaker-sequence PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pc-speaker-sequence-test")
    add_test(NAME pc-speaker-sequence COMMAND test-pc-speaker-sequence
        "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/music/tone-mono.ogg")
    set_tests_properties(pc-speaker-sequence PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pc-speaker-sequence-test"
        ENVIRONMENT "RESTUNTS_AUDIO_TRACE=")
    if(TARGET mt32emu)
        add_executable(test-mt32-roms src/restunts/tests/test-mt32-roms.cpp
            ${platform}/mt32_roms.cpp)
        target_link_libraries(test-mt32-roms PRIVATE restunts_options mt32emu)
        set_target_properties(test-mt32-roms PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
        target_compile_definitions(test-mt32-roms PRIVATE RESTUNTS_MT32_ROM_TESTS)
        target_compile_options(test-mt32-roms PRIVATE -UNDEBUG)
        add_test(NAME mt32-rom-discovery COMMAND test-mt32-roms)
        add_executable(test-mt32-driver src/restunts/tests/test-mt32-driver.c
            ${platform}/mt32_driver.c)
        target_link_libraries(test-mt32-driver PRIVATE restunts_options)
        target_compile_options(test-mt32-driver PRIVATE -UNDEBUG)
        add_test(NAME mt32-driver-protocol COMMAND test-mt32-driver)
        add_executable(test-mt32-sequence src/restunts/tests/test-mt32-sequence.c)
        target_link_libraries(test-mt32-sequence PRIVATE restunts_game)
        target_compile_options(test-mt32-sequence PRIVATE -UNDEBUG)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mt32-sequence-test")
        add_test(NAME mt32-sequence COMMAND test-mt32-sequence
            "${CMAKE_SOURCE_DIR}/stunts"
            "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/music/tone-mono.ogg")
        set_tests_properties(mt32-sequence PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mt32-sequence-test"
            ENVIRONMENT "RESTUNTS_AUDIO_TRACE=")
    endif()
    set(RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER 1 CACHE STRING
        "Timeout multiplier for native ghost, UNIT, and rendering regressions")
    if(NOT RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER must be a positive integer")
    endif()
    add_executable(test-cstrk ${core}/extended/extended_cstrk.c
        ${core}/extended/extended_cstrk_horizon.c src/restunts/tests/test-cstrk.c)
    target_compile_options(test-cstrk PRIVATE -UNDEBUG)
    add_test(NAME cstrk-format COMMAND test-cstrk)
    add_executable(test-extended-checkpoint ${core}/extended/extended_checkpoint.c
        ${core}/extended/extended_cstrk.c
        ${core}/extended/extended_cstrk_horizon.c src/restunts/tests/test-extended-checkpoint.c)
    target_compile_definitions(test-extended-checkpoint PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-checkpoint PRIVATE -UNDEBUG)
    add_test(NAME extended-checkpoint COMMAND test-extended-checkpoint)
    add_executable(test-checkpoint-shape ${core}/extended/extended_checkpoint_shape.c
        src/restunts/tests/test-checkpoint-shape.c)
    target_compile_options(test-checkpoint-shape PRIVATE -UNDEBUG)
    add_test(NAME checkpoint-shape COMMAND test-checkpoint-shape)
    add_executable(test-wheat-field ${core}/extended/extended_wheat_field.c
        src/restunts/tests/test-wheat-field.c)
    target_compile_options(test-wheat-field PRIVATE -UNDEBUG)
    add_test(NAME wheat-field-mesh COMMAND test-wheat-field)
    add_executable(test-crop-fields ${core}/extended/extended_crop_fields.c
        src/restunts/tests/test-crop-fields.c)
    target_compile_options(test-crop-fields PRIVATE -UNDEBUG)
    add_test(NAME crop-field-meshes COMMAND test-crop-fields)
    add_executable(test-cshig ${core}/extended/extended_cstrk.c
        ${core}/extended/extended_cstrk_horizon.c ${core}/extended/extended_cshig.c
        src/restunts/tests/test-cshig.c)
    target_compile_options(test-cshig PRIVATE -UNDEBUG)
    add_test(NAME cshig-format COMMAND test-cshig)
    add_executable(test-extended-highscore src/restunts/tests/test-extended-highscore.c)
    target_link_libraries(test-extended-highscore PRIVATE restunts_game)
    target_compile_options(test-extended-highscore PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extended-highscore-test")
    add_test(NAME extended-highscore COMMAND test-extended-highscore)
    set_tests_properties(extended-highscore PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extended-highscore-test")
    add_executable(test-extended-validation ${core}/extended/extended_validation.c
        src/restunts/tests/test-extended-validation.c)
    target_compile_options(test-extended-validation PRIVATE -UNDEBUG)
    add_test(NAME extended-validation COMMAND test-extended-validation)
    add_executable(test-csrpl ${core}/extended/extended_cstrk.c
        ${core}/extended/extended_cstrk_horizon.c ${core}/extended/extended_csrpl.c
        ${core}/extended/extended_replay.c
        src/restunts/tests/test-csrpl.c)
    target_compile_options(test-csrpl PRIVATE -UNDEBUG)
    add_test(NAME csrpl-format COMMAND test-csrpl)
    add_executable(test-extended-replay-runtime ${core}/extended/extended_replay.c
        ${core}/extended/extended_replay_runtime.c ${core}/extended/extended_replay_ui.c
        ${core}/ui/ui_text.c ${core}/legacy.c
        src/restunts/tests/test-extended-replay-runtime.c)
    target_compile_definitions(test-extended-replay-runtime PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-replay-runtime PRIVATE -UNDEBUG)
    add_test(NAME extended-replay-runtime COMMAND test-extended-replay-runtime)
    add_executable(test-extended-ghost ${core}/extended/extended_ghost.c
        ${core}/extended/extended_cstrk.c
        ${core}/extended/extended_cstrk_horizon.c
        ${core}/extended/extended_csrpl.c
        src/restunts/tests/test-extended-ghost.c)
    target_compile_definitions(test-extended-ghost PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-ghost PRIVATE -UNDEBUG)
    add_test(NAME extended-ghost COMMAND test-extended-ghost)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test-extended-editor src/restunts/tests/test-extended-editor.c)
        target_link_libraries(test-extended-editor PRIVATE restunts_options)
        target_compile_options(test-extended-editor PRIVATE -UNDEBUG)
        add_test(NAME extended-editor COMMAND test-extended-editor)
    endif()
    add_executable(test-video-mode-bitmaps src/restunts/tests/test-video-mode-bitmaps.c
        ${platform}/video_mode_bitmaps.c)
    target_link_libraries(test-video-mode-bitmaps PRIVATE restunts_options)
    target_compile_definitions(test-video-mode-bitmaps PRIVATE RESTUNTS_FULL)
    target_compile_options(test-video-mode-bitmaps PRIVATE -UNDEBUG)
    add_test(NAME video-mode-bitmaps COMMAND test-video-mode-bitmaps)
    add_executable(test-video-modes src/restunts/tests/test-video-modes.c
        ${platform}/video_mode.c ${platform}/video_mode_bitmaps.c)
    target_link_libraries(test-video-modes PRIVATE restunts_options)
    target_compile_definitions(test-video-modes PRIVATE RESTUNTS_FULL)
    target_compile_options(test-video-modes PRIVATE -UNDEBUG)
    add_test(NAME video-modes COMMAND test-video-modes)
    add_executable(test-video-mode-primitives src/restunts/tests/test-video-mode-primitives.c)
    target_link_libraries(test-video-mode-primitives PRIVATE restunts_game)
    target_compile_definitions(test-video-mode-primitives PRIVATE RESTUNTS_FULL)
    target_compile_options(test-video-mode-primitives PRIVATE -UNDEBUG)
    add_test(NAME video-mode-primitives COMMAND test-video-mode-primitives)
    add_executable(test-extended-track ${common_sources} ${core}/headless/headless.c ${platform_common}
        src/restunts/tests/test-extended-track.c)
    target_compile_definitions(test-extended-track PRIVATE RESTUNTS_HEADLESS)
    target_link_libraries(test-extended-track PRIVATE restunts_options)
    target_compile_options(test-extended-track PRIVATE -UNDEBUG)
    add_test(NAME extended-track COMMAND test-extended-track)
    set_tests_properties(extended-track PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/stunts"
        SKIP_RETURN_CODE 77)
    find_program(RESTUNTS_NODE_EXECUTABLE NAMES node nodejs)
    if(RESTUNTS_NODE_EXECUTABLE)
        add_test(NAME html-track-editor COMMAND "${RESTUNTS_NODE_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-track-editor.js")
        add_test(NAME html-track-validation COMMAND "${RESTUNTS_NODE_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-track-editor-validation.js")
        foreach(editor_feature scene preview 3d-resources)
            add_test(NAME html-track-${editor_feature} COMMAND "${RESTUNTS_NODE_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-track-editor-${editor_feature}.js")
        endforeach()
        if(NOT CMAKE_CROSSCOMPILING)
            add_executable(editor-route-oracle src/restunts/tests/editor-route-oracle.c)
            target_link_libraries(editor-route-oracle PRIVATE restunts_game)
            add_test(NAME html-track-validation-native COMMAND "${RESTUNTS_NODE_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-track-editor-validation.js"
                "$<TARGET_FILE:editor-route-oracle>")
        endif()
    endif()
    # Fixtures include selected implementation files; discard unrelated code
    # and its unresolved references using the target platform's linker syntax.
    set(restunts_test_section_options -ffunction-sections -fdata-sections)
    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        # Haiku links executables as shared objects, retaining visible symbols.
        list(APPEND restunts_test_section_options -fvisibility=hidden)
    endif()
    if(APPLE)
        set(restunts_test_gc_sections "LINKER:-dead_strip")
    else()
        set(restunts_test_gc_sections "LINKER:--gc-sections")
    endif()
    target_compile_options(test-extended-replay-runtime PRIVATE ${restunts_test_section_options})
    target_link_options(test-extended-replay-runtime PRIVATE ${restunts_test_gc_sections})
    enable_testing()
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(test-sdl3-file-replace src/restunts/tests/test-sdl3-file-replace.c
            ${platform_files} ${core}/extended/extended_file_dialog.c)
        target_compile_definitions(test-sdl3-file-replace PRIVATE RESTUNTS_SDL3 __EMSCRIPTEN__)
        target_include_directories(test-sdl3-file-replace PRIVATE src/restunts/tests/stubs)
        target_compile_options(test-sdl3-file-replace PRIVATE -UNDEBUG -Wno-pointer-sign)
        add_test(NAME browser-file-replace COMMAND test-sdl3-file-replace)
    endif()
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(test-car-zip src/restunts/tests/test-car-zip.c ${platform_files}
            ${platform}/memory.c ${core}/fileio.c ${core}/file_car.c ${core}/file_decomp.c
            ${core}/extended/extended_fileio.c ${core}/extended/extended_file_dialog.c
            ${core}/legacy.c ${core}/strlib.c ${core}/memmgr.c)
        target_compile_definitions(test-car-zip PRIVATE RESTUNTS_SDL3 RESTUNTS_HEADLESS
            RESTUNTS_CAR_DIRECTORY="../Bundled")
        target_compile_options(test-car-zip PRIVATE -UNDEBUG -Wno-pointer-sign
            ${restunts_test_section_options})
        target_link_options(test-car-zip PRIVATE ${restunts_test_gc_sections})
        add_test(NAME car-zip COMMAND test-car-zip)
    endif()
    add_executable(test-extended-hiscore-ui src/restunts/tests/test-extended-hiscore-ui.c
        ${core}/extended/extended_highscore_ui.c ${core}/extended/extended_menu_runtime.c
        ${core}/hires/track_preview_mode.c ${core}/hires/track_menu_preview.c
        ${core}/hires/highscore_animation.c)
    target_compile_definitions(test-extended-hiscore-ui PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-hiscore-ui PRIVATE -UNDEBUG
        ${restunts_test_section_options})
    target_link_options(test-extended-hiscore-ui PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-extended-hiscore-ui PRIVATE restunts_options)
    add_test(NAME extended-hiscore-ui COMMAND test-extended-hiscore-ui)
    add_executable(test-extended-race-start src/restunts/tests/test-extended-race-start.c
        ${core}/extended/extended_validation.c ${core}/extended/extended_menu_runtime.c
        ${core}/hires/track_preview_mode.c ${core}/hires/settings_startup.c)
    target_compile_definitions(test-extended-race-start PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-race-start PRIVATE -UNDEBUG
        ${restunts_test_section_options})
    target_link_options(test-extended-race-start PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-extended-race-start PRIVATE restunts_options)
    add_test(NAME extended-race-start COMMAND test-extended-race-start)
    add_executable(test-driving-rules src/restunts/tests/test-driving-rules.c
        ${core}/driving_rules.c ${core}/statecar.c ${core}/physics/physics_collision.c
        ${core}/physics/physics_suspension.c
        ${core}/extended/extended_physics_collision.c ${core}/extended/extended_statecrs.c
        ${core}/extended/extended_race_route.c ${core}/extended/extended_trackobj.c
        ${core}/extended/extended_trackobj_solids.c
        ${core}/physics/physics_grip.c ${core}/race/race_route.c ${core}/track/trackobj.c
        ${core}/track/trackobj_road.c ${core}/track/trackobj_stunts.c
        ${core}/track/trackobj_queries.c
        ${core}/owoot/owoot.c
        ${core}/strlib.c)
    target_compile_options(test-driving-rules PRIVATE -UNDEBUG -Wno-pointer-sign
        -Wno-sign-compare -Wno-missing-braces ${restunts_test_section_options})
    target_link_options(test-driving-rules PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-driving-rules PRIVATE restunts_options)
    add_test(NAME driving-rules COMMAND test-driving-rules)
    add_executable(test-replay-version-warning src/restunts/tests/test-replay-version-warning.c
        ${core}/fileio.c ${core}/file_car.c ${core}/file_decomp.c ${core}/extended/extended_fileio.c
        ${core}/strlib.c)
    target_compile_definitions(test-replay-version-warning PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-replay-version-warning PRIVATE -UNDEBUG -Wno-pointer-sign
        ${restunts_test_section_options})
    target_link_options(test-replay-version-warning PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-replay-version-warning PRIVATE restunts_options)
    add_test(NAME replay-version-warning COMMAND test-replay-version-warning)
    add_executable(test-extended-file-dialog src/restunts/tests/test-extended-file-dialog.c
        ${core}/ui/ui_dialog.c ${core}/ui/ui_file_dialog.c ${core}/ui/ui_disk_dialog.c
        ${core}/extended/extended_ui_dialog.c ${core}/ui/ui_input.c
        ${core}/legacy.c
        ${core}/strlib.c
        ${core}/resource.c ${platform}/runtime.c)
    target_compile_definitions(test-extended-file-dialog PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-file-dialog PRIVATE -UNDEBUG -Wno-pointer-sign
        ${restunts_test_section_options})
    target_link_options(test-extended-file-dialog PRIVATE ${restunts_test_gc_sections})
    add_test(NAME extended-file-dialog COMMAND test-extended-file-dialog)
    add_executable(test-extended-fileio src/restunts/tests/test-extended-fileio.c ${platform_files}
        ${core}/extended/extended_file_dialog.c)
    target_compile_definitions(test-extended-fileio PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-fileio PRIVATE -UNDEBUG -Wno-pointer-sign
        ${restunts_test_section_options})
    target_link_options(test-extended-fileio PRIVATE ${restunts_test_gc_sections})
    add_test(NAME extended-fileio COMMAND test-extended-fileio)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test-penalty-routing src/restunts/tests/test-penalty-routing.c
            ${core}/race/race_route.c ${core}/extended/extended_race_route.c ${core}/strlib.c)
        target_link_libraries(test-penalty-routing PRIVATE restunts_options)
        target_compile_options(test-penalty-routing PRIVATE -UNDEBUG ${restunts_test_section_options})
        target_link_options(test-penalty-routing PRIVATE ${restunts_test_gc_sections})
        add_test(NAME penalty-routing COMMAND test-penalty-routing)
        add_executable(test-unit-penalty src/restunts/tests/test-unit-penalty.c
            ${core}/race/race_route.c ${core}/extended/extended_race_route.c
            ${core}/extended/extended_unit_penalty.c ${core}/extended/extended_statecrs.c
            ${core}/math/math.c ${core}/legacy.c ${core}/strlib.c)
        target_link_libraries(test-unit-penalty PRIVATE restunts_options)
        target_compile_definitions(test-unit-penalty PRIVATE RESTUNTS_SDL3 RESTUNTS_HEADLESS
            RESTUNTS_EXTENDED_REPLAY)
        target_compile_options(test-unit-penalty PRIVATE -UNDEBUG ${restunts_test_section_options})
        target_link_options(test-unit-penalty PRIVATE ${restunts_test_gc_sections})
        add_test(NAME unit-penalty COMMAND test-unit-penalty)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        # Information switches must work without game files or a working data path.
        set(command_line_test_directory "${CMAKE_CURRENT_BINARY_DIR}/command-line-test")
        file(MAKE_DIRECTORY "${command_line_test_directory}")
        add_test(NAME command-line-help COMMAND restunts
            --data-dir "${command_line_test_directory}/missing" --help)
        add_test(NAME command-line-help-short COMMAND restunts
            --data-dir "${command_line_test_directory}/missing" -h)
        add_test(NAME command-line-version COMMAND restunts
            --data-dir "${command_line_test_directory}/missing" --version)
        add_test(NAME command-line-version-short COMMAND restunts
            --data-dir "${command_line_test_directory}/missing" -v)
        set_tests_properties(command-line-help command-line-help-short
            command-line-version command-line-version-short PROPERTIES
            WORKING_DIRECTORY "${command_line_test_directory}"
            ENVIRONMENT "SDL_VIDEODRIVER=restunts-invalid-driver;SDL_AUDIODRIVER=restunts-invalid-driver"
            TIMEOUT 10)
        if(TARGET genghost)
            add_test(NAME genghost-help COMMAND genghost
                --data-dir "${command_line_test_directory}/missing" --help)
            add_test(NAME genghost-version COMMAND genghost
                --data-dir "${command_line_test_directory}/missing" --version)
            set_tests_properties(genghost-help genghost-version PROPERTIES
                WORKING_DIRECTORY "${command_line_test_directory}"
                ENVIRONMENT "SDL_VIDEODRIVER=restunts-invalid-driver;SDL_AUDIODRIVER=restunts-invalid-driver"
                TIMEOUT 10)
        endif()
    endif()
    add_executable(test-sdl3-timer src/restunts/tests/test-sdl3-timer.c)
    target_link_libraries(test-sdl3-timer PRIVATE restunts_options)
    target_compile_options(test-sdl3-timer PRIVATE -UNDEBUG)
    add_test(NAME sdl3-timer COMMAND test-sdl3-timer)
    add_executable(test-sdl3-platform src/restunts/tests/test-sdl3-platform.c ${platform_common})
    target_compile_definitions(test-sdl3-platform PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-sdl3-platform PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/platform-test")
    add_test(NAME sdl3-platform COMMAND test-sdl3-platform)
    add_executable(test-sdl3-entry src/restunts/tests/test-sdl3-entry.c)
    target_link_libraries(test-sdl3-entry PRIVATE restunts_options)
    target_compile_options(test-sdl3-entry PRIVATE -UNDEBUG)
    add_test(NAME sdl3-entry COMMAND test-sdl3-entry)
    set_tests_properties(sdl3-platform PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/platform-test")
    if(WIN32 OR UNIX)
        add_executable(test-sdl3-asset-stream src/restunts/tests/test-sdl3-asset-stream.c)
        target_link_libraries(test-sdl3-asset-stream PRIVATE restunts_options)
        target_compile_options(test-sdl3-asset-stream PRIVATE -UNDEBUG)
        add_test(NAME sdl3-asset-stream COMMAND test-sdl3-asset-stream)
    endif()
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(test-sdl3-asset-path src/restunts/tests/test-sdl3-asset-path.c)
        target_link_libraries(test-sdl3-asset-path PRIVATE restunts_options)
        target_compile_options(test-sdl3-asset-path PRIVATE -UNDEBUG)
        add_test(NAME sdl3-asset-path COMMAND test-sdl3-asset-path)
        add_test(NAME host-regressions COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"
            bash ${CMAKE_SOURCE_DIR}/src/restunts/tests/run-host-tests.sh)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME html-track-route-data COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/generate-editor-route-data.py" --check)
            add_test(NAME html-track-scene-data COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/generate-editor-scene-data.py" --check)
            add_test(NAME opl-trace-renderer COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-render-opl-trace.py")
            add_test(NAME skybox-assets COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/check-skyboxes.py")
            add_test(NAME mcmurtry-hypervision COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-mcmurtry-hypervision.py")
            add_test(NAME mcmurtry-assets COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/generate-mcmurtry-speirling.py" --check)
        endif()
    endif()
endif()

if(RESTUNTS_BUILD_TESTS AND TARGET restunts_opl)
    add_executable(test-sdl3-audio-trace src/restunts/tests/test-sdl3-audio-trace.c)
    target_compile_options(test-sdl3-audio-trace PRIVATE -UNDEBUG)
    add_test(NAME sdl3-audio-trace COMMAND test-sdl3-audio-trace)
    add_executable(test-sdl3-music src/restunts/tests/test-sdl3-music.c
        ${platform}/asset_path.c $<TARGET_OBJECTS:restunts_vorbis>)
    target_link_libraries(test-sdl3-music PRIVATE restunts_options)
    target_include_directories(test-sdl3-music PRIVATE "${CMAKE_SOURCE_DIR}/third_party/stb")
    target_compile_options(test-sdl3-music PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/music-test")
    add_test(NAME sdl3-music COMMAND test-sdl3-music
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/music")
    set_tests_properties(sdl3-music PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/music-test")
    add_executable(test-sdl3-audio src/restunts/tests/test-sdl3-audio.c
        ${platform}/pc_speaker_driver.c ${platform}/pc_speaker_audio.c)
    target_link_libraries(test-sdl3-audio PRIVATE restunts_options restunts_opl)
    # Exercise real synthesis and SDL's dummy audio device, plus initialization
    # failure and batch-mode cleanup, without requiring host sound hardware.
    target_compile_options(test-sdl3-audio PRIVATE -UNDEBUG)
    add_test(NAME sdl3-audio COMMAND test-sdl3-audio "${CMAKE_SOURCE_DIR}/stunts/ADSKIDMS.VCE")
    # Regression runs must not overwrite a live capture inherited from the shell.
    set_tests_properties(sdl3-audio PROPERTIES ENVIRONMENT "RESTUNTS_AUDIO_TRACE=")
    add_executable(test-sdl3-audio-sequence src/restunts/tests/test-sdl3-audio-sequence.c)
    target_link_libraries(test-sdl3-audio-sequence PRIVATE restunts_game)
    target_compile_options(test-sdl3-audio-sequence PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-sequence-test")
    add_test(NAME sdl3-audio-sequence COMMAND test-sdl3-audio-sequence
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/music/tone-mono.ogg")
    set_tests_properties(sdl3-audio-sequence PROPERTIES ENVIRONMENT "RESTUNTS_AUDIO_TRACE="
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-sequence-test")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # This fixture includes the sequencer to reach its private marshaling
        # helper. ELF section GC isolates that helper from the rest of the game;
        # PE resolves unused references too, so the fixture is Linux-only.
        add_executable(test-sdl3-audio-definition src/restunts/tests/test-sdl3-audio-definition.c
            ${core}/resource.c ${platform}/memory.c ${platform}/runtime.c)
        target_link_libraries(test-sdl3-audio-definition PRIVATE restunts_options)
        target_compile_options(test-sdl3-audio-definition PRIVATE -UNDEBUG ${restunts_test_section_options})
        target_link_options(test-sdl3-audio-definition PRIVATE ${restunts_test_gc_sections})
        add_test(NAME sdl3-audio-definition COMMAND test-sdl3-audio-definition)
    endif()
endif()

if(RESTUNTS_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    math(EXPR restunts_unit_test_timeout
        "180 * ${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER}")
    math(EXPR restunts_unit_fixed_preparation_ms "${restunts_unit_test_timeout} * 1000 / 2")
    set(restunts_unit_fixed_preparation_steps 1024)
    set(unit_race_fixed_preparation_args --prepare-ms ${restunts_unit_fixed_preparation_ms}
        --prepare-steps ${restunts_unit_fixed_preparation_steps} --prepare-until-plan)
    add_executable(test-unit-race ${common_sources} ${core}/headless/headless.c ${platform_common}
        ${core}/extended/extended_replay_runtime.c ${platform}/main.c
        src/restunts/genghost/simulation.c src/restunts/genghost/extended_simulation.c
        src/restunts/tests/test-unit-race.c)
    get_target_property(unit_race_sources test-unit-race SOURCES)
    add_executable(test-unit-race-fixed-work ${unit_race_sources})
    foreach(unit_race_target test-unit-race test-unit-race-fixed-work)
        target_compile_definitions(${unit_race_target} PRIVATE
            RESTUNTS_HEADLESS RESTUNTS_EXTENDED_REPLAY)
        target_compile_options(${unit_race_target} PRIVATE -UNDEBUG)
        target_link_libraries(${unit_race_target} PRIVATE restunts_options)
    endforeach()
    target_compile_definitions(test-unit-race-fixed-work PRIVATE RESTUNTS_UNIT_TEST_FIXED_WORK=1)
    if(RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER GREATER 1)
        # Restore comparable native work per simulated frame under instrumentation.
        # Tick quotas and exact-state race-frame handoffs remain unchanged.
        target_compile_definitions(test-unit-race PRIVATE
            RESTUNTS_UNIT_WORK_TIME_MULTIPLIER=${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER})
    endif()
    set(unit_race_fixture "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/flat-rectangle.trk")
    math(EXPR restunts_unit_preparation_ms
        "10000 * ${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER}")
    set(unit_race_preparation_args --prepare-ms ${restunts_unit_preparation_ms})
    if(RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER GREATER 1)
        # Preserve useful planning work under instrumentation. Stop early once
        # a complete clean native lap has been validated.
        list(APPEND unit_race_preparation_args --prepare-until-plan)
    endif()
    set(unit_race_tests unit-fixed-work-budget
        unit-race-default unit-race-prepared unit-race-prepared-worker unit-race-owoot
        unit-race-ansx unit-race-lm02 unit-race-manual unit-race-10hz unit-race-checkpoints
        unit-race-glacier-sync unit-race-glacier-owoot-sync unit-race-native-grid
        unit-race-glacier-human unit-race-glacier-owoot-human)
    add_test(NAME unit-fixed-work-budget COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-preparation.py"
        "$<TARGET_FILE:test-unit-race-fixed-work>" "${CMAKE_SOURCE_DIR}/stunts"
        "${unit_race_fixture}")
    set_tests_properties(unit-fixed-work-budget PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-prepared COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/stunts/DEFAULT.TRK" 7
        ${unit_race_preparation_args} --planner-yield-ms 2
        --verify-replay --require-finish --require-clean)
    set_tests_properties(unit-race-prepared PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-glacier-sync COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/glacier.cstrk" 7
        ${unit_race_preparation_args} --planner-sync --frames 18000
        --verify-replay --require-finish --require-clean)
    set_tests_properties(unit-race-glacier-sync PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-glacier-owoot-sync COMMAND test-unit-race-fixed-work
        --data-dir "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/glacier.cstrk" 7
        ${unit_race_fixed_preparation_args} --planner-sync --frames 18000 --owoot:on
        --verify-replay --require-finish --require-clean)
    set_tests_properties(unit-race-glacier-owoot-sync PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-native-grid COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/glacier.cstrk" 7
        ${unit_race_preparation_args} --planner-yield-ms 2 --frames 60 --human grid --owoot:on
        --verify-replay --require-clean)
    set_tests_properties(unit-race-native-grid PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-glacier-human COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/glacier.cstrk" 7
        ${unit_race_preparation_args} --planner-yield-ms 2 --frames 18000 --human grid-then-park
        --verify-replay --require-finish --require-clean --require-worker)
    set_tests_properties(unit-race-glacier-human PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-glacier-owoot-human COMMAND test-unit-race-fixed-work
        --data-dir "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/glacier.cstrk" 7
        ${unit_race_fixed_preparation_args} --planner-yield-ms 2 --frames 18000
        --human grid-then-park --owoot:on
        --verify-replay --require-finish --require-clean --require-worker)
    set_tests_properties(unit-race-glacier-owoot-human PROPERTIES RUN_SERIAL TRUE)
    foreach(unit_regression_track branches keukenhof ninhbinh default)
        if(unit_regression_track STREQUAL "default")
            set(unit_regression_fixture "${CMAKE_SOURCE_DIR}/stunts/DEFAULT.TRK")
        else()
            set(unit_regression_fixture
                "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/${unit_regression_track}.cstrk")
        endif()
        list(APPEND unit_race_tests unit-race-${unit_regression_track}-no-stalls)
        add_test(NAME unit-race-${unit_regression_track}-no-stalls COMMAND test-unit-race-fixed-work
            --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_regression_fixture}" 7
            ${unit_race_fixed_preparation_args} --planner-yield-ms 2
            --verify-replay --require-finish --require-clean --require-no-stalls)
        set_tests_properties(unit-race-${unit_regression_track}-no-stalls PROPERTIES RUN_SERIAL TRUE)
    endforeach()
    foreach(unit_classic_track helens joes)
        list(APPEND unit_race_tests unit-race-${unit_classic_track}-manual)
        add_test(NAME unit-race-${unit_classic_track}-manual COMMAND test-unit-race
            --data-dir "${CMAKE_SOURCE_DIR}/stunts"
            "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/${unit_classic_track}.trk" 7
            ${unit_race_preparation_args} --planner-sync --manual
            --verify-replay --require-finish --require-clean --require-no-stalls)
        set_tests_properties(unit-race-${unit_classic_track}-manual PROPERTIES RUN_SERIAL TRUE)
    endforeach()
    list(APPEND unit_race_tests unit-race-skids-manual-faster)
    add_test(NAME unit-race-skids-manual-faster COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-opponent.py"
        "$<TARGET_FILE:test-unit-race>" "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/skids.trk"
        ${unit_race_preparation_args} --planner-sync --manual --require-faster)
    set_tests_properties(unit-race-skids-manual-faster PROPERTIES RUN_SERIAL TRUE)
    foreach(unit_skids_planner threaded sync)
        set(unit_skids_planner_args --planner-yield-ms 2)
        if(unit_skids_planner STREQUAL "sync")
            set(unit_skids_planner_args --planner-sync)
        endif()
        list(APPEND unit_race_tests unit-race-skids-short-launch-${unit_skids_planner})
        add_test(NAME unit-race-skids-short-launch-${unit_skids_planner} COMMAND test-unit-race
            --data-dir "${CMAKE_SOURCE_DIR}/stunts"
            "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/skids.trk" 7
            ${unit_race_preparation_args} --manual --frame-rate 20 --frames 6000 ${unit_skids_planner_args}
            --human grid-then-park --human-drive-frames 2
            --verify-replay --require-finish --require-clean)
        set_tests_properties(unit-race-skids-short-launch-${unit_skids_planner}
            PROPERTIES RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME unit-race-prepared-worker COMMAND test-unit-race-fixed-work
        --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_race_fixture}" 7
        ${unit_race_fixed_preparation_args} --planner-yield-ms 2 --frames 180
        --verify-replay --require-clean --require-worker-search)
    set_tests_properties(unit-race-prepared-worker PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME unit-race-checkpoints COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/blue-checkpoints.cstrk" 7
        --verify-replay --require-finish --require-clean)
    add_test(NAME unit-race-default COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/stunts/DEFAULT.TRK" 7
        --verify-replay --require-finish --require-clean)
    add_test(NAME unit-race-owoot COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_race_fixture}" 7
        --owoot:on --verify-replay --require-finish --require-clean)
    add_test(NAME unit-race-10hz COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_race_fixture}" 7
        --frame-rate 10 --owoot:on --verify-replay --require-finish --require-clean)
    foreach(unit_car ANSX LM02)
        string(TOLOWER "${unit_car}" unit_car_lower)
        add_test(NAME unit-race-${unit_car_lower} COMMAND test-unit-race
            --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_race_fixture}" 7
            --car "${unit_car}" --owoot:on --verify-replay --require-finish --require-clean)
    endforeach()
    add_test(NAME unit-race-manual COMMAND test-unit-race
        --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_race_fixture}" 7
        --car AUDI --manual --owoot:on --verify-replay --require-finish --require-clean)
    foreach(unit_human stationary accelerating crossing)
        list(APPEND unit_race_tests unit-race-human-${unit_human})
        add_test(NAME unit-race-human-${unit_human} COMMAND test-unit-race
            --data-dir "${CMAKE_SOURCE_DIR}/stunts" "${unit_race_fixture}" 7
            --frames 180 --human "${unit_human}" --verify-replay --require-clean)
    endforeach()
    list(APPEND unit_race_tests unit-learning-persistence)
    add_test(NAME unit-learning-persistence COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-learning.py"
        "$<TARGET_FILE:test-unit-race>" "${CMAKE_SOURCE_DIR}/stunts"
        "${unit_race_fixture}"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/branches.cstrk"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/skids.trk")
    set_tests_properties(unit-learning-persistence PROPERTIES RUN_SERIAL TRUE)
    list(APPEND unit_race_tests unit-bank-replay)
    add_test(NAME unit-bank-replay COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-bank-replay.py"
        "$<TARGET_FILE:test-unit-race>" "${CMAKE_SOURCE_DIR}/stunts"
        "${CMAKE_SOURCE_DIR}/assets/cars"
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/ninhbinh-banked.csrpl")
    set_tests_properties(unit-bank-replay PROPERTIES RUN_SERIAL TRUE)
    foreach(unit_human_case near long-range slow)
        set(unit_human_test "unit-human-independent-${unit_human_case}-sync")
        set(unit_human_mode blocked)
        set(unit_human_distance 350)
        if(unit_human_case STREQUAL "long-range")
            set(unit_human_distance 1400)
        elseif(unit_human_case STREQUAL "slow")
            set(unit_human_mode slow)
        endif()
        list(APPEND unit_race_tests "${unit_human_test}")
        add_test(NAME "${unit_human_test}" COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-human-independence.py"
            "$<TARGET_FILE:test-unit-race>" "${CMAKE_SOURCE_DIR}/stunts"
            "${unit_race_fixture}" --human-mode "${unit_human_mode}"
            --distance "${unit_human_distance}" --planner sync)
        set_tests_properties("${unit_human_test}" PROPERTIES RUN_SERIAL TRUE)
    endforeach()
    foreach(unit_human_planner sync threaded)
        set(unit_human_test "unit-human-independent-near-prepared-${unit_human_planner}")
        list(APPEND unit_race_tests "${unit_human_test}")
        add_test(NAME "${unit_human_test}" COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-human-independence.py"
            "$<TARGET_FILE:test-unit-race-fixed-work>" "${CMAKE_SOURCE_DIR}/stunts"
            "${unit_race_fixture}" --human-mode blocked --distance 350
            --planner "${unit_human_planner}" --prepared)
        set_tests_properties("${unit_human_test}" PROPERTIES RUN_SERIAL TRUE)
    endforeach()
    list(APPEND unit_race_tests unit-human-independent-near-owoot-sync)
    add_test(NAME unit-human-independent-near-owoot-sync COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/scripts/test-unit-human-independence.py"
        "$<TARGET_FILE:test-unit-race>" "${CMAKE_SOURCE_DIR}/stunts"
        "${unit_race_fixture}" --human-mode blocked --distance 350 --planner sync --owoot)
    set_tests_properties(unit-human-independent-near-owoot-sync PROPERTIES RUN_SERIAL TRUE)
    # Instrumentation slows the fixed native workload. Match the ghost tests'
    # budgets; CTest --timeout cannot override an explicit test TIMEOUT.
    set_tests_properties(${unit_race_tests} PROPERTIES TIMEOUT ${restunts_unit_test_timeout}
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    set_property(TEST ${unit_race_tests} APPEND PROPERTY ENVIRONMENT
        "RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER}")
    if(UNIX)
        add_executable(test-genghost-ipc src/restunts/tests/test-genghost-ipc.c)
        target_compile_options(test-genghost-ipc PRIVATE
            -UNDEBUG -fvisibility=hidden -ffunction-sections -fdata-sections)
        target_link_libraries(test-genghost-ipc PRIVATE restunts_options)
        # The test includes the private packet reader; hide and discard unrelated
        # process/planner functions, including on Haiku where executables export globals.
        if(APPLE)
            target_link_options(test-genghost-ipc PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(test-genghost-ipc PRIVATE -Wl,--gc-sections)
        endif()
        add_test(NAME genghost-ipc COMMAND test-genghost-ipc)
    endif()
    add_executable(test-genghost-simulation ${common_sources} ${core}/headless/headless.c ${platform_common}
        ${core}/extended/extended_replay_runtime.c
        ${platform}/main.c src/restunts/genghost/simulation.c
        src/restunts/genghost/extended_simulation.c
        src/restunts/genghost/guide.c src/restunts/genghost/extended_guide.c
        src/restunts/genghost/routes.c src/restunts/genghost/extended_routes.c
        src/restunts/genghost/extended_checkpoints.c
        src/restunts/tests/test-genghost-simulation.c)
    target_compile_definitions(test-genghost-simulation PRIVATE RESTUNTS_HEADLESS RESTUNTS_EXTENDED_REPLAY)
    target_compile_options(test-genghost-simulation PRIVATE -UNDEBUG)
    target_link_libraries(test-genghost-simulation PRIVATE restunts_options)
    add_executable(test-genghost-state ${common_sources} ${core}/headless/headless.c ${platform_common}
        ${core}/extended/extended_replay_runtime.c
        ${platform}/main.c src/restunts/genghost/simulation.c
        src/restunts/genghost/extended_simulation.c
        src/restunts/genghost/guide.c src/restunts/genghost/extended_guide.c
        src/restunts/genghost/routes.c src/restunts/genghost/extended_routes.c
        src/restunts/genghost/extended_checkpoints.c
        src/restunts/genghost/search.c src/restunts/genghost/extended_search.c
        src/restunts/genghost/parallel.c src/restunts/genghost/extended_parallel.c
        src/restunts/genghost/maneuvers.c src/restunts/genghost/extended_maneuvers.c
        src/restunts/genghost/state_search.c src/restunts/genghost/extended_state_search.c
        src/restunts/tests/test-genghost-state.c)
    target_compile_definitions(test-genghost-state PRIVATE RESTUNTS_HEADLESS RESTUNTS_EXTENDED_REPLAY)
    target_compile_options(test-genghost-state PRIVATE -UNDEBUG)
    target_link_libraries(test-genghost-state PRIVATE restunts_options)
    add_executable(test-genghost-extended ${common_sources} ${core}/headless/headless.c ${platform_common}
        ${core}/extended/extended_replay_runtime.c ${platform}/main.c
        src/restunts/genghost/simulation.c src/restunts/genghost/extended_simulation.c
        src/restunts/genghost/routes.c src/restunts/genghost/extended_routes.c
        src/restunts/genghost/extended_checkpoints.c
        src/restunts/genghost/guide.c src/restunts/genghost/extended_guide.c
        src/restunts/genghost/search.c src/restunts/genghost/extended_search.c
        src/restunts/tests/test-genghost-extended.c)
    target_compile_definitions(test-genghost-extended PRIVATE
        RESTUNTS_HEADLESS RESTUNTS_EXTENDED_REPLAY)
    target_compile_options(test-genghost-extended PRIVATE -UNDEBUG)
    target_link_libraries(test-genghost-extended PRIVATE restunts_options)
    foreach(extended_feature geometry maneuvers)
        add_executable(test-genghost-extended-${extended_feature}
            ${common_sources} ${core}/headless/headless.c ${platform_common}
            src/restunts/genghost/routes.c src/restunts/genghost/extended_routes.c
            src/restunts/genghost/extended_checkpoints.c
            src/restunts/genghost/guide.c src/restunts/genghost/extended_guide.c
            src/restunts/genghost/maneuvers.c src/restunts/genghost/extended_maneuvers.c
            src/restunts/tests/test-genghost-extended-${extended_feature}.c)
        target_compile_definitions(test-genghost-extended-${extended_feature} PRIVATE RESTUNTS_HEADLESS)
        target_compile_options(test-genghost-extended-${extended_feature} PRIVATE -UNDEBUG)
        target_link_libraries(test-genghost-extended-${extended_feature} PRIVATE restunts_options)
        add_test(NAME genghost-extended-${extended_feature}
            COMMAND test-genghost-extended-${extended_feature})
    endforeach()
    add_test(NAME genghost-regions COMMAND test-genghost-simulation --geometry-only)
    if(NOT CMAKE_CROSSCOMPILING)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME repldump-time COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-repldump-time.py"
                "$<TARGET_FILE:repldump>" "${CMAKE_SOURCE_DIR}/stunts"
                "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(repldump-time PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
            add_test(NAME genghost-analysis COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/tests/test_genghost_analysis.py")
            add_test(NAME genghost-android-request COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-android-request.py")
            set_tests_properties(genghost-android-request PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
            add_test(NAME genghost-stunt-analysis COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-stunts.py")
            add_test(NAME regression-timeouts COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/tests/test_regression_timeouts.py")
            set(restunts_genghost_shard_tests "")
            foreach(restunts_extended_part 1 2 3 4)
                set(restunts_extended_shard_count 2)
                if(restunts_extended_part EQUAL 4)
                    set(restunts_extended_shard_count 3)
                endif()
                foreach(restunts_extended_shard RANGE 1 ${restunts_extended_shard_count})
                    set(restunts_extended_test
                        genghost-extended-${restunts_extended_part}-${restunts_extended_shard})
                    add_test(NAME ${restunts_extended_test} COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-extended.py"
                        "$<TARGET_FILE:genghost>" "$<TARGET_FILE:test-genghost-extended>"
                        "${CMAKE_SOURCE_DIR}/stunts" --part "${restunts_extended_part}"
                        --shard "${restunts_extended_shard}")
                    set_tests_properties(${restunts_extended_test} PROPERTIES
                        SKIP_RETURN_CODE 77 TIMEOUT 300)
                    list(APPEND restunts_genghost_shard_tests ${restunts_extended_test})
                endforeach()
            endforeach()
            add_test(NAME genghost-simulation COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-simulation.py"
                "$<TARGET_FILE:test-genghost-simulation>" "$<TARGET_FILE:repldump>"
                "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(genghost-simulation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
            foreach(restunts_search_part 1 2)
                set(restunts_search_test genghost-search-${restunts_search_part})
                add_test(NAME ${restunts_search_test} COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-search.py"
                    "$<TARGET_FILE:genghost>" "$<TARGET_FILE:repldump>"
                    "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip"
                    --part "${restunts_search_part}")
                set_tests_properties(${restunts_search_test} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
                list(APPEND restunts_genghost_shard_tests ${restunts_search_test})
            endforeach()
            add_test(NAME genghost-fine-tune COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-fine-tune.py"
                "$<TARGET_FILE:genghost>" "$<TARGET_FILE:repldump>"
                "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip"
                "$<TARGET_FILE:test-genghost-state>")
            set_tests_properties(genghost-fine-tune PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240)
            add_test(NAME genghost-stop-file COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-stop.py"
                "$<TARGET_FILE:genghost>" "${CMAKE_SOURCE_DIR}/stunts"
                "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(genghost-stop-file PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
            add_test(NAME genghost-parallel COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-parallel.py"
                "$<TARGET_FILE:genghost>" "$<TARGET_FILE:repldump>"
                "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(genghost-parallel PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
            add_test(NAME genghost-state COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-state.py"
                "$<TARGET_FILE:genghost>" "$<TARGET_FILE:repldump>"
                "$<TARGET_FILE:test-genghost-state>"
                "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(genghost-state PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240)
            # CTest's --timeout does not override an explicit test TIMEOUT.
            # Propagate the same multiplier to the Python subprocess deadlines.
            foreach(restunts_genghost_test ${restunts_genghost_shard_tests} genghost-simulation
                    genghost-fine-tune genghost-stop-file genghost-parallel genghost-state)
                get_test_property(${restunts_genghost_test} TIMEOUT restunts_genghost_timeout)
                math(EXPR restunts_genghost_timeout
                    "${restunts_genghost_timeout} * ${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER}")
                set_tests_properties(${restunts_genghost_test} PROPERTIES
                    TIMEOUT ${restunts_genghost_timeout})
                set_property(TEST ${restunts_genghost_test} APPEND PROPERTY ENVIRONMENT
                    "RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER}")
            endforeach()
        endif()
    endif()
    add_executable(test-render-workers src/restunts/tests/test-render-workers.c)
    target_link_libraries(test-render-workers PRIVATE restunts_options)
    target_compile_options(test-render-workers PRIVATE -UNDEBUG)
    add_test(NAME render-workers COMMAND test-render-workers)
    add_executable(test-sdl3-touch src/restunts/tests/test-sdl3-touch.c ${platform}/touch.c)
    target_compile_definitions(test-sdl3-touch PRIVATE RESTUNTS_FULL)
    target_link_libraries(test-sdl3-touch PRIVATE restunts_options)
    target_compile_options(test-sdl3-touch PRIVATE -UNDEBUG)
    add_test(NAME sdl3-touch COMMAND test-sdl3-touch)
    add_executable(test-sdl3-soft-keyboard src/restunts/tests/test-sdl3-soft-keyboard.c
        ${platform}/soft_keyboard.c)
    target_link_libraries(test-sdl3-soft-keyboard PRIVATE restunts_options)
    target_compile_options(test-sdl3-soft-keyboard PRIVATE -UNDEBUG)
    add_test(NAME sdl3-soft-keyboard COMMAND test-sdl3-soft-keyboard)
    add_executable(test-sdl3-input src/restunts/tests/test-sdl3-input.c
        ${platform}/input.c ${platform}/video.c ${platform}/timer.c ${platform}/touch.c
        ${platform}/soft_keyboard.c ${core}/hires/frame_adaptive.c
        ${platform}/video_mode.c ${platform}/video_mode_bitmaps.c)
    target_link_libraries(test-sdl3-input PRIVATE restunts_options)
    target_compile_options(test-sdl3-input PRIVATE -UNDEBUG)
    add_test(NAME sdl3-input COMMAND test-sdl3-input)
endif()

if(RESTUNTS_BUILD_TESTS)
    add_executable(test-frame-adaptive src/restunts/tests/test-frame-adaptive.c
        ${core}/hires/frame_adaptive.c)
    target_link_libraries(test-frame-adaptive PRIVATE restunts_options)
    target_compile_options(test-frame-adaptive PRIVATE -UNDEBUG)
    add_test(NAME frame-adaptive COMMAND test-frame-adaptive)
    add_executable(test-frame-interpolation src/restunts/tests/test-frame-interpolation.c
        ${core}/hires/frame_interpolation.c)
    target_link_libraries(test-frame-interpolation PRIVATE restunts_options)
    target_compile_options(test-frame-interpolation PRIVATE -UNDEBUG)
    add_test(NAME frame-interpolation COMMAND test-frame-interpolation)
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(test-sdl3-race-frames src/restunts/tests/test-race-frames.c
            ${core}/extended/extended_replay_ui.c
            ${core}/legacy.c ${core}/data/full_data.c ${core}/headless/headless_data.c
            ${core}/hires/frame_interpolation.c
        ${core}/hires/race_view.c)
        target_link_libraries(test-sdl3-race-frames PRIVATE restunts_options)
        target_compile_options(test-sdl3-race-frames PRIVATE -UNDEBUG ${restunts_test_section_options})
        target_link_options(test-sdl3-race-frames PRIVATE ${restunts_test_gc_sections})
        add_test(NAME sdl3-race-frames COMMAND test-sdl3-race-frames)
    endif()
    if(UNIX AND NOT CMAKE_CROSSCOMPILING AND Python3_Interpreter_FOUND)
        math(EXPR restunts_render_test_timeout
            "120 * ${RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER}")
        add_executable(test-extended-render ${platform}/main.c
            src/restunts/tests/test-extended-render.c)
        target_compile_definitions(test-extended-render PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(test-extended-render PRIVATE restunts_game)
        target_compile_options(test-extended-render PRIVATE -UNDEBUG)
        if(NOT APPLE)
            target_compile_definitions(test-extended-render PRIVATE RESTUNTS_WRAP_HILL_SHAPES)
            target_link_options(test-extended-render PRIVATE
                "LINKER:--wrap=shape3d_transform_and_queue"
                "LINKER:--wrap=shape3d_hires_transform_and_queue")
        endif()
        add_test(NAME extended-render COMMAND test-extended-render
            --data-dir "${CMAKE_SOURCE_DIR}/stunts")
        set_tests_properties(extended-render PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;RESTUNTS_AUDIO_TRACE="
            TIMEOUT ${restunts_render_test_timeout})
        add_executable(test-extended-replay-engine ${platform}/main.c
            src/restunts/tests/test-extended-replay-engine.c)
        target_compile_definitions(test-extended-replay-engine PRIVATE
            RESTUNTS_PIXLDUMP
            RESTUNTS_HALF_PIPE_REPLAY_FIXTURE="${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/slopes/half-pipe-uphill.csrpl"
            RESTUNTS_FAN_SLOPE_REPLAY_FIXTURE="${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/slopes/fan-adhesion-slopes.csrpl"
            RESTUNTS_ZERO_SPEED_REPLAY_FIXTURE="${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/slopes/zero-speed-offset.csrpl"
            RESTUNTS_NINHBINH_TRACK_FIXTURE="${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/slopes/ninhbinh.cstrk"
            RESTUNTS_GLACIER_TRACK_FIXTURE="${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/unit/glacier.cstrk")
        target_link_libraries(test-extended-replay-engine PRIVATE restunts_game)
        target_compile_options(test-extended-replay-engine PRIVATE -UNDEBUG)
        if(NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_sources(test-extended-replay-engine PRIVATE
                src/restunts/tests/test-unit-pilot-desktop.c)
            target_compile_definitions(test-extended-replay-engine PRIVATE
                RESTUNTS_TEST_UNIT_PILOT_DESKTOP)
            target_link_options(test-extended-replay-engine PRIVATE
                "LINKER:--wrap=unit_opponent_pilot_step"
                "LINKER:--wrap=unit_opponent_pilot_wait_step"
                "LINKER:--wrap=audio_play_car_events"
                "LINKER:--wrap=audio_play_crash_and_stop_engine"
                "LINKER:--wrap=audio_carstate")
        endif()
        add_test(NAME extended-replay-engine COMMAND test-extended-replay-engine
            --data-dir "${CMAKE_SOURCE_DIR}/stunts")
        set_tests_properties(extended-replay-engine PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;RESTUNTS_AUDIO_TRACE="
            TIMEOUT ${restunts_render_test_timeout})
        add_executable(test-render-replay ${platform}/main.c
            src/restunts/tests/test-render-replay.c)
        target_compile_definitions(test-render-replay PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(test-render-replay PRIVATE restunts_game)
        target_compile_options(test-render-replay PRIVATE -UNDEBUG)
        add_test(NAME render-replay COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-render-replay.py"
            "$<TARGET_FILE:test-render-replay>" "${CMAKE_SOURCE_DIR}/stunts")
        add_test(NAME video-mode-replay COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-video-mode-replay.py"
            "$<TARGET_FILE:test-render-replay>" "${CMAKE_SOURCE_DIR}/stunts")
        add_executable(test-ghost-racing-line ${platform}/main.c
            src/restunts/tests/test-ghost-racing-line.c)
        target_compile_definitions(test-ghost-racing-line PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(test-ghost-racing-line PRIVATE restunts_game)
        target_compile_options(test-ghost-racing-line PRIVATE -UNDEBUG)
        add_test(NAME ghost-racing-line COMMAND test-ghost-racing-line
            --data-dir "${CMAKE_SOURCE_DIR}/stunts" DEFAULT)
        set_tests_properties(ghost-racing-line PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;RESTUNTS_AUDIO_TRACE=")
        add_executable(benchmark-hypervision EXCLUDE_FROM_ALL ${platform}/main.c
            src/restunts/benchmarks/benchmark-hypervision.c)
        target_compile_definitions(benchmark-hypervision PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(benchmark-hypervision PRIVATE restunts_game)
        target_compile_options(benchmark-hypervision PRIVATE -UNDEBUG)
    endif()
    add_executable(test-hypervision src/restunts/tests/test-hypervision.c)
    target_link_libraries(test-hypervision PRIVATE restunts_game)
    target_compile_options(test-hypervision PRIVATE -UNDEBUG)
    add_test(NAME hypervision COMMAND test-hypervision)
    add_executable(test-shape3d-hires src/restunts/tests/test-shape3d-hires.c)
    target_link_libraries(test-shape3d-hires PRIVATE restunts_game)
    target_compile_options(test-shape3d-hires PRIVATE -UNDEBUG)
    add_test(NAME shape3d-hires COMMAND test-shape3d-hires)
    add_executable(test-hires-mesh src/restunts/tests/test-hires-mesh.c)
    target_link_libraries(test-hires-mesh PRIVATE restunts_game)
    target_compile_definitions(test-hires-mesh PRIVATE
        TEST_HIRES_MESH_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    target_compile_options(test-hires-mesh PRIVATE -UNDEBUG)
    add_test(NAME hires-mesh COMMAND test-hires-mesh)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hires-mesh-test")
    set_tests_properties(hires-mesh PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hires-mesh-test")
    add_executable(test-skybox-hires src/restunts/tests/test-skybox-hires.c
        ${core}/extended/extended_horizon.c ${core}/extended/extended_cstrk_horizon.c
        ${core}/hires/skybox_hires.c ${core}/hires/hires.c ${core}/hires/hires_raster.c
        ${core}/hires/render_workers.c
        ${platform}/memory.c ${platform}/asset_path.c
        ${core}/video/video_framebuffer.c ${core}/hires/video_framebuffer.c)
    target_link_libraries(test-skybox-hires PRIVATE restunts_options)
    target_compile_options(test-skybox-hires PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/skybox-test")
    add_test(NAME skybox-hires COMMAND test-skybox-hires)
    set_tests_properties(skybox-hires PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/skybox-test")
    add_executable(test-extended-horizon src/restunts/tests/test-extended-horizon.c
        ${core}/extended/extended_horizon.c ${core}/extended/extended_horizon_skybox.c
        ${core}/extended/extended_cstrk_horizon.c ${core}/race/race_resources.c
        ${core}/hires/skybox_hires.c ${core}/hires/hires.c ${core}/hires/hires_raster.c
        ${core}/hires/render_workers.c ${platform}/memory.c
        ${core}/video/video_framebuffer.c ${core}/hires/video_framebuffer.c)
    target_link_libraries(test-extended-horizon PRIVATE restunts_options)
    target_compile_definitions(test-extended-horizon PRIVATE
        RESTUNTS_HORIZON_DIRECTORY="${restunts_asset_directory}/horizons")
    target_compile_options(test-extended-horizon PRIVATE -UNDEBUG ${restunts_test_section_options})
    target_link_options(test-extended-horizon PRIVATE ${restunts_test_gc_sections})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extended-horizon-test")
    add_test(NAME extended-horizon COMMAND test-extended-horizon)
    set_tests_properties(extended-horizon PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extended-horizon-test")
    add_executable(test-track-preview-skybox src/restunts/tests/test-track-preview-skybox.c)
    target_link_libraries(test-track-preview-skybox PRIVATE restunts_game)
    target_compile_options(test-track-preview-skybox PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/track-preview-test")
    add_test(NAME track-preview-skybox COMMAND test-track-preview-skybox)
    set_tests_properties(track-preview-skybox PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/track-preview-test")
    add_executable(test-opponent-portrait src/restunts/tests/test-opponent-portrait.c
        ${core}/opponent/opponent_portrait.c ${core}/hires/hires.c ${core}/hires/hires_raster.c
        ${core}/hires/render_workers.c
        ${platform}/memory.c ${platform}/asset_path.c
        ${core}/hires/opponent_portrait_artwork.c
        ${core}/video/video_framebuffer.c ${core}/hires/video_framebuffer.c)
    target_link_libraries(test-opponent-portrait PRIVATE restunts_options)
    target_compile_options(test-opponent-portrait PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/opponent-test")
    add_test(NAME opponent-portrait COMMAND test-opponent-portrait)
    set_tests_properties(opponent-portrait PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/opponent-test")
    add_executable(test-opponent-animation src/restunts/tests/test-opponent-animation.c
        ${platform}/asset_path.c)
    target_link_libraries(test-opponent-animation PRIVATE restunts_options restunts_webm)
    target_compile_options(test-opponent-animation PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/opponent-animation-test")
    add_test(NAME opponent-animation COMMAND test-opponent-animation
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/fixtures/opponent-animation")
    set_tests_properties(opponent-animation PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/opponent-animation-test")
    add_executable(test-menu-background src/restunts/tests/test-menu-background.c
        ${core}/menu/menu_background.c ${core}/hires/hires.c ${core}/hires/hires_raster.c
        ${core}/hires/render_workers.c
        ${platform}/memory.c ${platform}/asset_path.c
        ${core}/hires/menu_artwork.c
        ${core}/video/video_framebuffer.c ${core}/hires/video_framebuffer.c)
    target_link_libraries(test-menu-background PRIVATE restunts_options)
    target_compile_options(test-menu-background PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/menu-background-test")
    add_test(NAME menu-background COMMAND test-menu-background)
    set_tests_properties(menu-background PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/menu-background-test")
    add_executable(test-sdl3-hires src/restunts/tests/test-sdl3-hires.c ${core}/hires/hires.c
        ${core}/hires/hires_raster.c
        ${core}/hires/render_workers.c ${platform}/memory.c
        ${core}/video/video_framebuffer.c ${core}/hires/video_framebuffer.c)
    target_link_libraries(test-sdl3-hires PRIVATE restunts_options)
    target_compile_options(test-sdl3-hires PRIVATE -UNDEBUG)
    add_test(NAME sdl3-hires COMMAND test-sdl3-hires)
    add_executable(test-sdl3-fileio src/restunts/tests/test-sdl3-fileio.c)
    target_link_libraries(test-sdl3-fileio PRIVATE restunts_game)
    target_compile_options(test-sdl3-fileio PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fileio-test")
    add_test(NAME sdl3-fileio COMMAND test-sdl3-fileio)
    set_tests_properties(sdl3-fileio PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fileio-test")
endif()

if(RESTUNTS_BUILD_TESTS AND TARGET nuked_opl2)
    install(TARGETS nuked_opl2
        RUNTIME DESTINATION bin COMPONENT Tests EXCLUDE_FROM_ALL
        LIBRARY DESTINATION lib COMPONENT Tests EXCLUDE_FROM_ALL)
    install(TARGETS test-shape3d-hires test-hires-mesh test-skybox-hires test-extended-horizon test-track-preview-skybox test-render-workers
        RUNTIME DESTINATION bin COMPONENT Tests EXCLUDE_FROM_ALL)
    foreach(test platform audio audio-trace audio-definition audio-sequence input fileio hires timer)
        if(TARGET test-sdl3-${test})
            install(TARGETS test-sdl3-${test}
                RUNTIME DESTINATION bin COMPONENT Tests EXCLUDE_FROM_ALL)
        endif()
    endforeach()
endif()
