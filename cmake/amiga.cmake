# Reuse exactly the classic DOS game's C sources, without the SDL enhancements.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_C_COMPILER MATCHES "m68k-amigaos")
    message(FATAL_ERROR "Use -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/amiga-m68k.cmake")
endif()

set(core "${CMAKE_SOURCE_DIR}/src/restunts/c")
set(amiga "${CMAKE_SOURCE_DIR}/src/restunts/platform/amiga")
set(dos_objects "${CMAKE_SOURCE_DIR}/src/restunts/dos/full-game-objects.mk")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dos_objects}")
file(READ "${dos_objects}" object_list)
string(REGEX MATCHALL "[a-zA-Z0-9_]+\\.obj" game_objects "${object_list}")
list(REMOVE_ITEM game_objects dosstart.obj)
list(REMOVE_DUPLICATES game_objects)
set(game_sources)
foreach(object IN LISTS game_objects)
    string(REPLACE ".obj" ".c" source "${object}")
    list(APPEND game_sources "${core}/${source}")
endforeach()

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE RESTUNTS_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT RESTUNTS_GIT_HASH)
    set(RESTUNTS_GIT_HASH "source-archive")
endif()

set(platform_sources
    "${amiga}/main.c" "${amiga}/memory.c" "${amiga}/file.c"
    "${amiga}/runtime.c" "${amiga}/input.c" "${amiga}/timer.c"
    "${amiga}/display.c" "${amiga}/video.c" "${amiga}/audio.c")
option(RESTUNTS_AMIGA_TRACE "Print Amiga startup stages for diagnostics" OFF)
function(configure_amiga_game target)
    target_include_directories(${target} PRIVATE "${core}" "${amiga}")
    target_compile_definitions(${target} PRIVATE RESTUNTS_FULL RESTUNTS_AMIGA
        RESTUNTS_GIT_HASH="${RESTUNTS_GIT_HASH}")
    if(RESTUNTS_AMIGA_TRACE)
        target_compile_definitions(${target} PRIVATE RESTUNTS_AMIGA_TRACE)
    endif()
    target_compile_options(${target} PRIVATE -Os -m68000 -fsigned-char -fno-strict-aliasing
        -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra -Wno-pointer-sign
        -Wno-missing-field-initializers -Wno-missing-braces)
    target_link_options(${target} PRIVATE -Wl,--gc-sections
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    set_target_properties(${target} PROPERTIES SUFFIX "")
endfunction()

add_executable(restunts ${game_sources} ${platform_sources})
configure_amiga_game(restunts)
# Keep diagnostic code out of the shipping executable: the extra text would
# otherwise consume resource memory needed by larger stock cars on a 1 MB A500+.
add_executable(test-amiga-platform EXCLUDE_FROM_ALL ${game_sources} ${platform_sources}
    "${amiga}/smoke.c")
configure_amiga_game(test-amiga-platform)
target_compile_definitions(test-amiga-platform PRIVATE RESTUNTS_AMIGA_SMOKE)
# The classic renderer uses integer math. libnix's libm also replaces printf
# with a floating-point formatter and requires a disk-based math library.
install(TARGETS restunts RUNTIME DESTINATION .)

# Run this executable on a real 68000 or in the emulator harness. Unlike a
# little-endian host test it also detects odd-address word loads on the 68000.
add_executable(test-amiga-endian EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-amiga-endian.c"
    "${core}/shape2d.c" "${core}/shape2d_blit.c" "${core}/shape3d.c"
    "${core}/audio_engine.c")
target_include_directories(test-amiga-endian PRIVATE "${core}")
target_compile_definitions(test-amiga-endian PRIVATE RESTUNTS_AMIGA)
target_compile_options(test-amiga-endian PRIVATE -Os -m68000 -fsigned-char
    -fno-strict-aliasing -ffunction-sections -fdata-sections -UNDEBUG)
target_link_options(test-amiga-endian PRIVATE -Wl,--gc-sections)

add_executable(test-amiga-display EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-amiga-display.c"
    "${amiga}/display.c")
target_include_directories(test-amiga-display PRIVATE "${core}" "${amiga}")
target_compile_options(test-amiga-display PRIVATE -Os -m68000 -fsigned-char -UNDEBUG)

add_executable(test-amiga-rle EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-amiga-rle.c"
    "${core}/shape2d.c" "${core}/shape2d_blit.c")
target_include_directories(test-amiga-rle PRIVATE "${core}")
target_compile_definitions(test-amiga-rle PRIVATE RESTUNTS_AMIGA)
target_compile_options(test-amiga-rle PRIVATE -Os -m68000 -fsigned-char
    -fno-strict-aliasing -ffunction-sections -fdata-sections -UNDEBUG)
target_link_options(test-amiga-rle PRIVATE -Wl,--gc-sections)

add_executable(test-amiga-unflip EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-amiga-unflip.c"
    "${core}/shape2d.c" "${core}/resource.c")
target_include_directories(test-amiga-unflip PRIVATE "${core}")
target_compile_definitions(test-amiga-unflip PRIVATE RESTUNTS_AMIGA)
target_compile_options(test-amiga-unflip PRIVATE -Os -m68000 -fsigned-char
    -fno-strict-aliasing -ffunction-sections -fdata-sections -UNDEBUG)
target_link_options(test-amiga-unflip PRIVATE -Wl,--gc-sections)

add_executable(test-amiga-decompression EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-amiga-decompression.c"
    "${core}/fileio.c")
target_include_directories(test-amiga-decompression PRIVATE "${core}")
target_compile_definitions(test-amiga-decompression PRIVATE RESTUNTS_AMIGA RESTUNTS_HEADLESS)
target_compile_options(test-amiga-decompression PRIVATE -Os -m68000 -fsigned-char
    -fno-strict-aliasing -ffunction-sections -fdata-sections -UNDEBUG)
target_link_options(test-amiga-decompression PRIVATE -Wl,--gc-sections)

# Shared option and custom-car physics tests can also run on the native CPU.
# Keep them outside the shipping executable and normal build.
function(configure_amiga_diagnostic target)
    target_include_directories(${target} PRIVATE "${core}")
    target_compile_definitions(${target} PRIVATE RESTUNTS_AMIGA)
    target_compile_options(${target} PRIVATE -Os -m68000 -fsigned-char
        -fno-strict-aliasing -ffunction-sections -fdata-sections -UNDEBUG
        -Wno-pointer-sign -Wno-missing-field-initializers -Wno-missing-braces)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    set_target_properties(${target} PROPERTIES SUFFIX "")
endfunction()

add_executable(test-amiga-command-line EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-command-line.c"
    "${core}/command_line.c")
configure_amiga_diagnostic(test-amiga-command-line)
target_compile_definitions(test-amiga-command-line PRIVATE
    RESTUNTS_GIT_HASH="0123456789abcdef0123456789abcdef01234567"
    RESTUNTS_BUILD_DATE="Feb 12 1991")

add_executable(test-amiga-startup EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-startup.c"
    "${core}/math.c" "${core}/owoot.c" "${core}/carsetup.c"
    "${core}/statecar.c" "${core}/strlib.c" "${core}/physics_collision.c"
    "${core}/physics_grip.c" "${core}/full_data.c" "${core}/full_strings.c"
    "${core}/headless_data.c" "${core}/legacy.c")
configure_amiga_diagnostic(test-amiga-startup)

# Exercise dual-way route confirmation and committed penalty times on 68000.
foreach(target test-amiga-penalty-routing test-amiga-penalty-routing-features)
    add_executable(${target} EXCLUDE_FROM_ALL
        "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-penalty-routing.c"
        "${core}/physics_grip.c" "${core}/strlib.c")
    configure_amiga_diagnostic(${target})
endforeach()
# Keep every feature assertion on a cycle-accurate A500+, while the full target
# also runs the expensive 8192-sample randomized compatibility fingerprint.
target_compile_definitions(test-amiga-penalty-routing-features PRIVATE PENALTY_ROUTING_FEATURE_ONLY)

add_executable(test-amiga-car-adhesion EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/src/restunts/tests/test-car-adhesion.c"
    "${core}/physics_collision.c" "${core}/trackobj.c" "${core}/headless_data.c"
    "${core}/headless_trackdata.c" "${core}/math.c" "${core}/legacy.c"
    "${core}/strlib.c" "${core}/trkutil.c" "${core}/resource.c"
    "${core}/physics_grip.c")
configure_amiga_diagnostic(test-amiga-car-adhesion)
target_compile_definitions(test-amiga-car-adhesion PRIVATE RESTUNTS_HEADLESS)
