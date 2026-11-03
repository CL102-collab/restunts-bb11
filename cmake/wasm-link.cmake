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
        "-sEXPORTED_RUNTIME_METHODS=['FS','callMain']"
        "SHELL:--pre-js \"${restunts_thread_mode}\""
        "SHELL:--shell-file \"${shell}\""
        "SHELL:--embed-file \"${asset_root}/assets@/assets\"")
    file(GLOB_RECURSE restunts_embedded_files CONFIGURE_DEPENDS
        "${asset_root}/assets/*")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${shell}" "${restunts_thread_mode}" ${restunts_embedded_files})
endfunction()
