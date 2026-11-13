# Keep the independent audio realm serial even when the main game uses pthreads.
include("${CMAKE_CURRENT_LIST_DIR}/wasm-link.cmake")
set(restunts_audio_runtime_source "${CMAKE_SOURCE_DIR}/src/restunts/platform/wasm/browser_audio_runtime.c")
set(restunts_audio_commands_source "${CMAKE_SOURCE_DIR}/src/restunts/platform/sdl3/audio_commands.c")
set(restunts_audio_completions_source "${CMAKE_SOURCE_DIR}/src/restunts/platform/sdl3/audio_completions.c")
set(restunts_audio_worker_js "${CMAKE_SOURCE_DIR}/src/restunts/platform/sdl3/browser_audio_worker.js")
set(restunts_audio_worklet_js "${CMAKE_SOURCE_DIR}/src/restunts/platform/sdl3/browser_audio_worklet.js")
set(restunts_audio_bridge_js "${CMAKE_SOURCE_DIR}/src/restunts/platform/sdl3/browser_audio.js")
set(restunts_audio_generator "${CMAKE_SOURCE_DIR}/tools/scripts/generate-audio-sources.py")

# Bound the nested Ninja build independently of the parent generator's pool.
set(restunts_audio_default_jobs 4)
if(NOT "$ENV{RESTUNTS_BUILD_JOBS}" STREQUAL "")
    set(restunts_audio_default_jobs "$ENV{RESTUNTS_BUILD_JOBS}")
elseif(NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(restunts_audio_default_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
endif()
set(RESTUNTS_WASM_AUDIO_BUILD_JOBS "${restunts_audio_default_jobs}" CACHE STRING
    "Parallel jobs for the independent serial audio sub-build")
if(NOT RESTUNTS_WASM_AUDIO_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "RESTUNTS_WASM_AUDIO_BUILD_JOBS must be a positive integer")
endif()

if(RESTUNTS_WASM_THREADS)
    # A pthread-built archive cannot be linked into the serial audio module.
    # Reuse pinned dependency SOURCE trees, with separate objects and caches.
    include(ExternalProject)
    set(restunts_audio_serial_build "${CMAKE_CURRENT_BINARY_DIR}/audio-runtime-serial")
    ExternalProject_Add(restunts_audio_serial
        SOURCE_DIR "${CMAKE_SOURCE_DIR}"
        BINARY_DIR "${restunts_audio_serial_build}"
        CMAKE_ARGS
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            -DRESTUNTS_WASM_THREADS=OFF
            -DRESTUNTS_BUILD_LAUNCHER=OFF
            -DRESTUNTS_BUILD_TESTS=OFF
            "-DFETCHCONTENT_SOURCE_DIR_SDL3=${SDL3_SOURCE_DIR}"
            "-DFETCHCONTENT_SOURCE_DIR_RESTUNTS_MUNT=${restunts_munt_SOURCE_DIR}"
            "-DFETCHCONTENT_SOURCE_DIR_RESTUNTS_VPX=${restunts_vpx_SOURCE_DIR}"
            "-DFETCHCONTENT_SOURCE_DIR_RESTUNTS_NESTEGG=${restunts_nestegg_SOURCE_DIR}"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target restunts_audio_runtime
            --parallel "${RESTUNTS_WASM_AUDIO_BUILD_JOBS}"
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS "${restunts_audio_serial_build}/restunts_audio_runtime.js"
        EXCLUDE_FROM_ALL TRUE)
    set(restunts_audio_runtime_js "${restunts_audio_serial_build}/restunts_audio_runtime.js")
    set(restunts_audio_relink_inputs "${restunts_audio_serial_build}/audio-relink-inputs")
    set(restunts_audio_runtime_target restunts_audio_serial)
else()
    # Define specialized objects before the game archive: their complete worker
    # and timer interfaces prevent the archive's main-thread implementations
    # (and its EM_ASYNC_JS bridge) from being selected by the linker.
    add_library(restunts_audio_runtime_objects OBJECT
        "${restunts_audio_runtime_source}" "${restunts_audio_commands_source}"
        "${restunts_audio_completions_source}")
    target_compile_definitions(restunts_audio_runtime_objects PRIVATE
        RESTUNTS_BROWSER_AUDIO_RUNTIME=1 RESTUNTS_AUDIO_WORKER)
    target_link_libraries(restunts_audio_runtime_objects PRIVATE restunts_game)
    add_executable(restunts_audio_runtime $<TARGET_OBJECTS:restunts_audio_runtime_objects>)
    target_link_libraries(restunts_audio_runtime PRIVATE restunts_game)
    restunts_configure_wasm_audio_runtime(restunts_audio_runtime)
    set(restunts_audio_runtime_js "${CMAKE_CURRENT_BINARY_DIR}/restunts_audio_runtime.js")
    set(restunts_audio_relink_inputs "${CMAKE_CURRENT_BINARY_DIR}/audio-relink-inputs")
    set(restunts_audio_runtime_target restunts_audio_runtime)
    add_custom_command(TARGET restunts_audio_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${restunts_audio_relink_inputs}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${restunts_audio_relink_inputs}/objects" "${restunts_audio_relink_inputs}/lib"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_OBJECTS:restunts_audio_runtime_objects> "${restunts_audio_relink_inputs}/objects/"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:restunts_game>" "${restunts_audio_relink_inputs}/lib/librestunts_game.a"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:restunts_webm>" "${restunts_audio_relink_inputs}/lib/librestunts_webm.a"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>" "${restunts_audio_relink_inputs}/lib/libSDL3.a"
        COMMAND_EXPAND_LISTS VERBATIM)
endif()

restunts_embed_wasm_audio(restunts "${restunts_audio_runtime_js}"
    "${restunts_audio_worker_js}" "${restunts_audio_worklet_js}"
    "${restunts_audio_bridge_js}" "${restunts_audio_generator}")
add_dependencies(restunts_audio_sources ${restunts_audio_runtime_target})
