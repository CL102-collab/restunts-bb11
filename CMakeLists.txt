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
    # Gradle invokes Ninja for each ABI; bound native memory use on modest hosts.
    set(RESTUNTS_ANDROID_BUILD_JOBS 1 CACHE STRING "Concurrent Android native compilations")
    set_property(GLOBAL PROPERTY JOB_POOLS
        restunts_android_compile=${RESTUNTS_ANDROID_BUILD_JOBS} restunts_android_link=1)
    set(CMAKE_JOB_POOL_COMPILE restunts_android_compile)
    set(CMAKE_JOB_POOL_LINK restunts_android_link)
endif()

if(EMSCRIPTEN OR ANDROID)
    set(restunts_tests_default OFF)
    # file:// cannot supply the isolation headers required by browser threads.
    if(EMSCRIPTEN)
        set(SDL_PTHREADS OFF CACHE BOOL "The standalone browser build uses one thread" FORCE)
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
        URL https://github.com/libsdl-org/SDL/archive/015489c672f24feed28c2aa2cdd6176df95329f3.tar.gz
        URL_HASH SHA256=50c373115702974928c760f6e752aae33b69dcc51b4c2d909c35b30f41283a52
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)
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
    fileio.c legacy.c resource.c math.c memmgr.c strlib.c numlib.c kevinrnd.c
    replinit.c gameinit.c gamestep.c simd.c carsetup.c trkutil.c trackres.c
    headless_data.c headless_trackdata.c state.c race_route.c physics_grip.c opponent.c
    track_setup.c stateply.c physics_collision.c statecrs.c statecar.c trackobj.c
    owoot.c owoot_wheels.c owoot_road.c owoot_route.c driving_rules.c replay.c stateio.c
    cstrk.c csrpl.c cshig.c extended_replay.c extended_track.c extended_geometry.c extended_opponent.c)
set(game_sources
    restunts.c command_line.c fatal.c timing.c game_input.c ui_text.c ui_input.c ui_dialog.c
    intro.c highscore.c replay_record.c replay_viewer.c race.c ghost.c
    extended_file.c extended_replay_runtime.c extended_editor.c extended_race_stats.c extended_ghost.c
    extended_validation.c extended_highscore.c extended_frame.c
    race_resources.c dashboard.c menu_common.c menu_main.c menu_options.c
    menu_track.c menu_track_editor.c menu_opponent.c opponent_portrait.c opponent_animation.c menu_background.c
    menu_car.c audio_engine.c
    audio_resources.c audio_car.c audio_sequence.c shape3d.c shape3d_resources.c
    shape3d_car.c shape3d_prerender.c shape3d_lines.c shape2d.c shape2d_blit.c
    video_pages.c hires.c hypervision.c render_workers.c frame_interpolation.c shape3d_hires.c shape2d_resources.c heapsort.c frame.c frame_adaptive.c skybox.c skybox_hires.c frame_overlay.c
    intro_render.c track_preview.c full_data.c full_strings.c full_tables.c)
list(TRANSFORM common_sources PREPEND "${core}/")
list(TRANSFORM game_sources PREPEND "${core}/")
set(platform_common ${platform}/memory.c ${platform}/file.c ${platform}/runtime.c)
set(platform_game ${platform}/asset_path.c ${platform}/video.c ${platform}/input.c ${platform}/timer.c
    ${platform}/audio.c ${platform}/music.c ${platform}/touch.c ${platform}/soft_keyboard.c)

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
        -Wall -Wextra -Wno-pointer-sign -Wno-missing-field-initializers -Wno-missing-braces)
endif()
target_link_libraries(restunts_options INTERFACE SDL3::SDL3 m)

function(restunts_add_genghost)
    # The optimizer shares repldump's physics and owns its command-line entry point.
    add_executable(genghost ${common_sources} ${core}/headless.c ${platform_common}
        src/restunts/genghost/main.c src/restunts/genghost/simulation.c
        src/restunts/genghost/routes.c src/restunts/genghost/search.c
        src/restunts/genghost/guide.c src/restunts/genghost/parallel.c
        src/restunts/genghost/maneuvers.c src/restunts/genghost/state_search.c)
    target_compile_definitions(genghost PRIVATE RESTUNTS_HEADLESS)
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

# The portable VP8/WebM decoder is a separate static archive for browser relinking.
include(cmake/webm.cmake)

# Keep the decoder objects in the application archive, including browser relink kits.
add_library(restunts_vorbis OBJECT third_party/stb/stb_vorbis.c)
target_compile_definitions(restunts_vorbis PRIVATE STB_VORBIS_NO_PUSHDATA_API)
add_library(restunts_game STATIC ${common_sources} ${game_sources} ${platform_common} ${platform_game}
    $<TARGET_OBJECTS:restunts_vorbis>)
target_include_directories(restunts_game PRIVATE "${CMAKE_SOURCE_DIR}/third_party/stb")
set(menu_backgrounds main showroom opponent)
set(music_tracks titl slct over vict)
set(restunts_car_files CARMCMU.RES CARTERR.RES STMCMU.3SH STTERR.3SH
    STDAMCMU.VSH STDATERR.VSH STDBMCMU.VSH STDBTERR.VSH)
target_compile_definitions(restunts_game PRIVATE RESTUNTS_FULL
    RESTUNTS_EDITOR_DIRECTORY="${CMAKE_SOURCE_DIR}/src/restunts/platform/wasm"
    RESTUNTS_MENU_DIRECTORY="${restunts_asset_directory}/menus"
    RESTUNTS_SKYBOX_DIRECTORY="${restunts_asset_directory}/skyboxes"
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

foreach(component Runtime Tests)
    set(exclude_from_default)
    if(component STREQUAL "Tests")
        set(exclude_from_default EXCLUDE_FROM_ALL)
    endif()
    install(FILES third_party/stb/LICENSE
        DESTINATION share/licenses/restunts RENAME stb-LICENSE
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
            COMPONENT ${component} ${exclude_from_default} FILES_MATCHING PATTERN "*.cmake")
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

if(ANDROID)
    # Gradle packages native libraries and app assets; install source and licenses only.
    install(FILES docs/android.md DESTINATION share/docs/restunts COMPONENT Runtime)
    return()
endif()

if(EMSCRIPTEN)
    include(cmake/wasm.cmake)
    # Dump tools, native tests and shared-library installation are host-only.
    return()
endif()

add_executable(repldump ${common_sources} ${core}/headless.c ${platform_common}
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
foreach(car_file IN LISTS restunts_car_files)
    install(FILES "assets/cars/${car_file}" DESTINATION bin/cars COMPONENT Runtime)
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

install(FILES src/restunts/platform/wasm/track-editor.html
    src/restunts/platform/wasm/track-editor.css src/restunts/platform/wasm/track-editor.js
    src/restunts/platform/wasm/track-editor-model.js src/restunts/platform/wasm/track-editor-resources.js
    src/restunts/platform/wasm/track-editor-route-data.js
    src/restunts/platform/wasm/track-editor-validation.js
    DESTINATION share/restunts/editor COMPONENT Runtime)
install(FILES docs/cstrk-format.md docs/csrpl-format.md docs/cshig-format.md docs/track-editor.md
    DESTINATION share/docs/restunts COMPONENT Runtime)

if(RESTUNTS_BUILD_TESTS)
    add_executable(test-cstrk ${core}/cstrk.c src/restunts/tests/test-cstrk.c)
    target_compile_options(test-cstrk PRIVATE -UNDEBUG)
    add_test(NAME cstrk-format COMMAND test-cstrk)
    add_executable(test-cshig ${core}/cstrk.c ${core}/cshig.c src/restunts/tests/test-cshig.c)
    target_compile_options(test-cshig PRIVATE -UNDEBUG)
    add_test(NAME cshig-format COMMAND test-cshig)
    add_executable(test-extended-highscore src/restunts/tests/test-extended-highscore.c)
    target_link_libraries(test-extended-highscore PRIVATE restunts_game)
    target_compile_options(test-extended-highscore PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extended-highscore-test")
    add_test(NAME extended-highscore COMMAND test-extended-highscore)
    set_tests_properties(extended-highscore PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extended-highscore-test")
    add_executable(test-extended-validation ${core}/extended_validation.c
        src/restunts/tests/test-extended-validation.c)
    target_compile_options(test-extended-validation PRIVATE -UNDEBUG)
    add_test(NAME extended-validation COMMAND test-extended-validation)
    add_executable(test-csrpl ${core}/cstrk.c ${core}/csrpl.c ${core}/extended_replay.c
        src/restunts/tests/test-csrpl.c)
    target_compile_options(test-csrpl PRIVATE -UNDEBUG)
    add_test(NAME csrpl-format COMMAND test-csrpl)
    add_executable(test-extended-replay-runtime ${core}/extended_replay.c
        ${core}/extended_replay_runtime.c src/restunts/tests/test-extended-replay-runtime.c)
    target_compile_definitions(test-extended-replay-runtime PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-replay-runtime PRIVATE -UNDEBUG)
    add_test(NAME extended-replay-runtime COMMAND test-extended-replay-runtime)
    add_executable(test-extended-ghost ${core}/extended_ghost.c ${core}/cstrk.c ${core}/csrpl.c
        src/restunts/tests/test-extended-ghost.c)
    target_compile_definitions(test-extended-ghost PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-ghost PRIVATE -UNDEBUG)
    add_test(NAME extended-ghost COMMAND test-extended-ghost)
    add_executable(test-extended-track ${common_sources} ${core}/headless.c ${platform_common}
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
    enable_testing()
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(test-sdl3-file-replace src/restunts/tests/test-sdl3-file-replace.c
            ${platform}/file.c)
        target_compile_definitions(test-sdl3-file-replace PRIVATE RESTUNTS_SDL3 __EMSCRIPTEN__)
        target_include_directories(test-sdl3-file-replace PRIVATE src/restunts/tests/stubs)
        target_compile_options(test-sdl3-file-replace PRIVATE -UNDEBUG -Wno-pointer-sign)
        add_test(NAME browser-file-replace COMMAND test-sdl3-file-replace)
    endif()
    add_executable(test-extended-hiscore-ui src/restunts/tests/test-extended-hiscore-ui.c)
    target_compile_definitions(test-extended-hiscore-ui PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-hiscore-ui PRIVATE -UNDEBUG
        ${restunts_test_section_options})
    target_link_options(test-extended-hiscore-ui PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-extended-hiscore-ui PRIVATE restunts_options)
    add_test(NAME extended-hiscore-ui COMMAND test-extended-hiscore-ui)
    add_executable(test-extended-race-start src/restunts/tests/test-extended-race-start.c
        ${core}/extended_validation.c)
    target_compile_definitions(test-extended-race-start PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-race-start PRIVATE -UNDEBUG
        ${restunts_test_section_options})
    target_link_options(test-extended-race-start PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-extended-race-start PRIVATE restunts_options)
    add_test(NAME extended-race-start COMMAND test-extended-race-start)
    add_executable(test-driving-rules src/restunts/tests/test-driving-rules.c
        ${core}/driving_rules.c ${core}/statecar.c ${core}/physics_collision.c
        ${core}/physics_grip.c ${core}/race_route.c ${core}/trackobj.c ${core}/owoot.c
        ${core}/strlib.c)
    target_compile_options(test-driving-rules PRIVATE -UNDEBUG -Wno-pointer-sign
        -Wno-sign-compare -Wno-missing-braces ${restunts_test_section_options})
    target_link_options(test-driving-rules PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-driving-rules PRIVATE restunts_options)
    add_test(NAME driving-rules COMMAND test-driving-rules)
    add_executable(test-replay-version-warning src/restunts/tests/test-replay-version-warning.c
        ${core}/fileio.c ${core}/strlib.c)
    target_compile_definitions(test-replay-version-warning PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-replay-version-warning PRIVATE -UNDEBUG -Wno-pointer-sign
        ${restunts_test_section_options})
    target_link_options(test-replay-version-warning PRIVATE ${restunts_test_gc_sections})
    target_link_libraries(test-replay-version-warning PRIVATE restunts_options)
    add_test(NAME replay-version-warning COMMAND test-replay-version-warning)
    add_executable(test-extended-file-dialog src/restunts/tests/test-extended-file-dialog.c
        ${core}/ui_dialog.c ${core}/ui_input.c ${core}/legacy.c ${core}/strlib.c
        ${core}/resource.c ${platform}/runtime.c)
    target_compile_definitions(test-extended-file-dialog PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-file-dialog PRIVATE -UNDEBUG -Wno-pointer-sign
        ${restunts_test_section_options})
    target_link_options(test-extended-file-dialog PRIVATE ${restunts_test_gc_sections})
    add_test(NAME extended-file-dialog COMMAND test-extended-file-dialog)
    add_executable(test-extended-fileio src/restunts/tests/test-extended-fileio.c ${platform}/file.c)
    target_compile_definitions(test-extended-fileio PRIVATE RESTUNTS_SDL3)
    target_compile_options(test-extended-fileio PRIVATE -UNDEBUG -Wno-pointer-sign
        ${restunts_test_section_options})
    target_link_options(test-extended-fileio PRIVATE ${restunts_test_gc_sections})
    add_test(NAME extended-fileio COMMAND test-extended-fileio)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test-penalty-routing src/restunts/tests/test-penalty-routing.c
            ${core}/race_route.c ${core}/strlib.c)
        target_link_libraries(test-penalty-routing PRIVATE restunts_options)
        target_compile_options(test-penalty-routing PRIVATE -UNDEBUG ${restunts_test_section_options})
        target_link_options(test-penalty-routing PRIVATE ${restunts_test_gc_sections})
        add_test(NAME penalty-routing COMMAND test-penalty-routing)
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
            add_test(NAME opl-trace-renderer COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-render-opl-trace.py")
            add_test(NAME skybox-assets COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/check-skyboxes.py")
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
    add_executable(test-sdl3-audio src/restunts/tests/test-sdl3-audio.c)
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
    add_executable(test-genghost-simulation ${common_sources} ${core}/headless.c ${platform_common}
        ${platform}/main.c src/restunts/genghost/simulation.c
        src/restunts/genghost/guide.c src/restunts/genghost/routes.c
        src/restunts/tests/test-genghost-simulation.c)
    target_compile_definitions(test-genghost-simulation PRIVATE RESTUNTS_HEADLESS)
    target_compile_options(test-genghost-simulation PRIVATE -UNDEBUG)
    target_link_libraries(test-genghost-simulation PRIVATE restunts_options)
    add_executable(test-genghost-state ${common_sources} ${core}/headless.c ${platform_common}
        ${platform}/main.c src/restunts/genghost/simulation.c
        src/restunts/genghost/guide.c src/restunts/genghost/routes.c
        src/restunts/genghost/search.c src/restunts/genghost/parallel.c
        src/restunts/genghost/maneuvers.c src/restunts/genghost/state_search.c
        src/restunts/tests/test-genghost-state.c)
    target_compile_definitions(test-genghost-state PRIVATE RESTUNTS_HEADLESS)
    target_compile_options(test-genghost-state PRIVATE -UNDEBUG)
    target_link_libraries(test-genghost-state PRIVATE restunts_options)
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
            add_test(NAME genghost-simulation COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-simulation.py"
                "$<TARGET_FILE:test-genghost-simulation>" "$<TARGET_FILE:repldump>"
                "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(genghost-simulation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
            add_test(NAME genghost-search COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/scripts/test-genghost-search.py"
                "$<TARGET_FILE:genghost>" "$<TARGET_FILE:repldump>"
                "${CMAKE_SOURCE_DIR}/stunts" "${CMAKE_SOURCE_DIR}/tools/scripts/rpls_golden/replays.zip")
            set_tests_properties(genghost-search PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
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
            set(RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER 1 CACHE STRING
                "Timeout multiplier for native ghost regressions")
            if(NOT RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER MATCHES "^[1-9][0-9]*$")
                message(FATAL_ERROR
                    "RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER must be a positive integer")
            endif()
            foreach(restunts_genghost_test genghost-simulation genghost-search
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
        ${platform}/soft_keyboard.c ${core}/frame_adaptive.c)
    target_link_libraries(test-sdl3-input PRIVATE restunts_options)
    target_compile_options(test-sdl3-input PRIVATE -UNDEBUG)
    add_test(NAME sdl3-input COMMAND test-sdl3-input)
endif()

if(RESTUNTS_BUILD_TESTS)
    add_executable(test-frame-adaptive src/restunts/tests/test-frame-adaptive.c ${core}/frame_adaptive.c)
    target_link_libraries(test-frame-adaptive PRIVATE restunts_options)
    target_compile_options(test-frame-adaptive PRIVATE -UNDEBUG)
    add_test(NAME frame-adaptive COMMAND test-frame-adaptive)
    add_executable(test-frame-interpolation src/restunts/tests/test-frame-interpolation.c ${core}/frame_interpolation.c)
    target_link_libraries(test-frame-interpolation PRIVATE restunts_options)
    target_compile_options(test-frame-interpolation PRIVATE -UNDEBUG)
    add_test(NAME frame-interpolation COMMAND test-frame-interpolation)
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(test-sdl3-race-frames src/restunts/tests/test-race-frames.c
            ${core}/legacy.c ${core}/full_data.c ${core}/headless_data.c ${core}/frame_interpolation.c)
        target_link_libraries(test-sdl3-race-frames PRIVATE restunts_options)
        target_compile_options(test-sdl3-race-frames PRIVATE -UNDEBUG ${restunts_test_section_options})
        target_link_options(test-sdl3-race-frames PRIVATE ${restunts_test_gc_sections})
        add_test(NAME sdl3-race-frames COMMAND test-sdl3-race-frames)
    endif()
    if(UNIX AND NOT CMAKE_CROSSCOMPILING AND Python3_Interpreter_FOUND)
        add_executable(test-extended-render ${platform}/main.c
            src/restunts/tests/test-extended-render.c)
        target_compile_definitions(test-extended-render PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(test-extended-render PRIVATE restunts_game)
        target_compile_options(test-extended-render PRIVATE -UNDEBUG)
        if(NOT APPLE)
            target_compile_definitions(test-extended-render PRIVATE RESTUNTS_WRAP_HILL_SHAPES)
            target_link_options(test-extended-render PRIVATE
                "LINKER:--wrap=shape3d_transform_and_queue")
        endif()
        add_test(NAME extended-render COMMAND test-extended-render
            --data-dir "${CMAKE_SOURCE_DIR}/stunts")
        set_tests_properties(extended-render PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;RESTUNTS_AUDIO_TRACE="
            TIMEOUT 120)
        add_executable(test-extended-replay-engine ${platform}/main.c
            src/restunts/tests/test-extended-replay-engine.c)
        target_compile_definitions(test-extended-replay-engine PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(test-extended-replay-engine PRIVATE restunts_game)
        target_compile_options(test-extended-replay-engine PRIVATE -UNDEBUG)
        add_test(NAME extended-replay-engine COMMAND test-extended-replay-engine
            --data-dir "${CMAKE_SOURCE_DIR}/stunts")
        set_tests_properties(extended-replay-engine PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;RESTUNTS_AUDIO_TRACE="
            TIMEOUT 120)
        add_executable(test-render-replay ${platform}/main.c src/restunts/tests/test-render-replay.c)
        target_compile_definitions(test-render-replay PRIVATE RESTUNTS_PIXLDUMP)
        target_link_libraries(test-render-replay PRIVATE restunts_game)
        target_compile_options(test-render-replay PRIVATE -UNDEBUG)
        add_test(NAME render-replay COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scripts/test-render-replay.py"
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
    add_executable(test-skybox-hires src/restunts/tests/test-skybox-hires.c
        ${core}/skybox_hires.c ${core}/hires.c ${core}/render_workers.c ${platform}/memory.c ${platform}/asset_path.c)
    target_link_libraries(test-skybox-hires PRIVATE restunts_options)
    target_compile_options(test-skybox-hires PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/skybox-test")
    add_test(NAME skybox-hires COMMAND test-skybox-hires)
    set_tests_properties(skybox-hires PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/skybox-test")
    add_executable(test-track-preview-skybox src/restunts/tests/test-track-preview-skybox.c)
    target_link_libraries(test-track-preview-skybox PRIVATE restunts_game)
    target_compile_options(test-track-preview-skybox PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/track-preview-test")
    add_test(NAME track-preview-skybox COMMAND test-track-preview-skybox)
    set_tests_properties(track-preview-skybox PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/track-preview-test")
    add_executable(test-opponent-portrait src/restunts/tests/test-opponent-portrait.c
        ${core}/opponent_portrait.c ${core}/hires.c ${core}/render_workers.c ${platform}/memory.c ${platform}/asset_path.c)
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
        ${core}/menu_background.c ${core}/hires.c ${core}/render_workers.c ${platform}/memory.c ${platform}/asset_path.c)
    target_link_libraries(test-menu-background PRIVATE restunts_options)
    target_compile_options(test-menu-background PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/menu-background-test")
    add_test(NAME menu-background COMMAND test-menu-background)
    set_tests_properties(menu-background PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/menu-background-test")
    add_executable(test-sdl3-hires src/restunts/tests/test-sdl3-hires.c ${core}/hires.c ${core}/render_workers.c ${platform}/memory.c)
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
    install(TARGETS test-shape3d-hires test-skybox-hires test-track-preview-skybox test-render-workers
        RUNTIME DESTINATION bin COMPONENT Tests EXCLUDE_FROM_ALL)
    foreach(test platform audio audio-trace audio-definition audio-sequence input fileio hires timer)
        if(TARGET test-sdl3-${test})
            install(TARGETS test-sdl3-${test}
                RUNTIME DESTINATION bin COMPONENT Tests EXCLUDE_FROM_ALL)
        endif()
    endforeach()
endif()
