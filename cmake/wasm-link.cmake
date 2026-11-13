# Shared by the game build and its relocatable LGPL relink package.
function(restunts_configure_wasm target asset_root shell)
    # Legacy rendering uses large automatic arrays. Asyncify saves nested game
    # loops while the browser processes input, audio and display callbacks.
    set(restunts_stack_bytes 1048576)
    set(restunts_asyncify_stack_bytes 1048576)
    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    set(restunts_thread_mode "${CMAKE_CURRENT_BINARY_DIR}/${target}-thread-mode.js")
    if(RESTUNTS_WASM_THREADS)
        set(restunts_wasm_environment "web,worker")
        set(restunts_thread_support true)
        # Synchronous render jobs run on the browser thread. Preload all possible
        # render workers plus one spare so their startup never needs that thread
        # to process a message while it is waiting for a completed render job.
        set(restunts_pthread_pool_size 8)
        target_link_options(${target} PRIVATE -pthread
            -sPTHREAD_POOL_SIZE=${restunts_pthread_pool_size}
            -sPTHREAD_POOL_SIZE_STRICT=2
            -sDEFAULT_PTHREAD_STACK_SIZE=${restunts_stack_bytes})
    else()
        set(restunts_wasm_environment "web")
        set(restunts_thread_support false)
    endif()
    file(WRITE "${restunts_thread_mode}"
        "Module['restuntsMultiCoreSupported'] = ${restunts_thread_support};\n")
    target_link_options(${target} PRIVATE
        -sSINGLE_FILE=1
        -sENVIRONMENT=${restunts_wasm_environment}
        -sASYNCIFY=1
        -sASYNCIFY_STACK_SIZE=${restunts_asyncify_stack_bytes}
        -sSTACK_SIZE=${restunts_stack_bytes}
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1
        "-sEXPORTED_RUNTIME_METHODS=['FS','callMain','ccall','UTF8ToString']"
        "-sEXPORTED_FUNCTIONS=['_main','_mt32_rom_inventory_json','_mt32_rom_inventory_free']"
        "SHELL:--pre-js \"${restunts_thread_mode}\""
        "SHELL:--shell-file \"${shell}\""
        "SHELL:--embed-file \"${asset_root}/assets@/assets\"")
    file(GLOB_RECURSE restunts_embedded_files CONFIGURE_DEPENDS
        "${asset_root}/assets/*")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${shell}" "${restunts_thread_mode}" ${restunts_embedded_files})
endfunction()

# The audio instance is a serial, DOM-free module hosted by a dedicated Worker.
# It deliberately has neither an application entry point nor Asyncify imports.
function(restunts_configure_wasm_audio_runtime target)
    set_target_properties(${target} PROPERTIES SUFFIX ".js" LINKER_LANGUAGE CXX)
    target_link_options(${target} PRIVATE
        --no-entry
        -sSINGLE_FILE=1
        -sMODULARIZE=1
        -sEXPORT_NAME=createRestuntsAudioRuntime
        -sENVIRONMENT=worker
        -sSTACK_SIZE=1048576
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=0
        -sFORCE_FILESYSTEM=1
        "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','FS','HEAPU8','HEAP16']"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_audio_command','_audio_response_free','_audio_render_tick','_audio_pop_finish']")
    foreach(symbol SDL_InitSubSystem SDL_QuitSubSystem SDL_WasInit
            SDL_OpenAudioDeviceStream SDL_DestroyAudioStream SDL_SetAudioStreamFormat
            SDL_PutAudioStreamData SDL_GetAudioStreamQueued SDL_ClearAudioStream
            SDL_SetAudioStreamGain SDL_ResumeAudioStreamDevice SDL_PauseAudioStreamDevice)
        target_link_options(${target} PRIVATE "-Wl,--wrap=${symbol}")
    endforeach()
endfunction()

function(restunts_embed_wasm_audio target runtime worker worklet bridge generator)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(audio_sources "${CMAKE_CURRENT_BINARY_DIR}/${target}-audio-sources.js")
    add_custom_command(OUTPUT "${audio_sources}"
        COMMAND "${Python3_EXECUTABLE}" "${generator}"
            --runtime "${runtime}" --worker "${worker}" --worklet "${worklet}"
            --output "${audio_sources}"
        DEPENDS "${runtime}" "${worker}" "${worklet}" "${generator}"
        VERBATIM)
    add_custom_target(${target}_audio_sources DEPENDS "${audio_sources}")
    add_dependencies(${target} ${target}_audio_sources)
    target_link_options(${target} PRIVATE
        "SHELL:--pre-js \"${audio_sources}\""
        "SHELL:--pre-js \"${bridge}\"")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${audio_sources}" "${bridge}")
endfunction()
