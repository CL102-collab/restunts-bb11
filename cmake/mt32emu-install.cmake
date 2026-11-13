# Distribution materials for the pinned Munt core, without proprietary ROMs.
if(NOT TARGET mt32emu)
    return()
endif()
set(restunts_mt32emu_core "${RESTUNTS_MT32EMU_SOURCE_DIR}/mt32emu")
set(restunts_mt32emu_package share/restunts/mt32emu)
file(READ "${CMAKE_SOURCE_DIR}/cmake/mt32emu-source-manifest.json" restunts_mt32emu_manifest)
string(JSON restunts_mt32emu_source_count LENGTH "${restunts_mt32emu_manifest}" files)
math(EXPR restunts_mt32emu_source_last "${restunts_mt32emu_source_count} - 1")
configure_file("${CMAKE_SOURCE_DIR}/cmake/mt32emu-build-info.txt.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mt32emu-build-info.txt" @ONLY)
foreach(component Runtime Tests)
    if(component STREQUAL Tests)
        set(restunts_mt32emu_install_exclusion EXCLUDE_FROM_ALL)
    else()
        set(restunts_mt32emu_install_exclusion)
    endif()
    # Gradle includes shared libraries in the APK; keep its outer archive source-only.
    if(NOT EMSCRIPTEN AND NOT ANDROID)
        install(TARGETS mt32emu
            RUNTIME DESTINATION bin COMPONENT ${component} ${restunts_mt32emu_install_exclusion}
            LIBRARY DESTINATION lib COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    endif()
    install(FILES "${restunts_mt32emu_core}/COPYING.LESSER.txt"
        DESTINATION share/licenses/restunts RENAME MT32EMU-COPYING.LESSER.txt
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    install(FILES "${restunts_mt32emu_core}/COPYING.txt"
        DESTINATION share/licenses/restunts RENAME MT32EMU-COPYING.txt
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    foreach(index RANGE ${restunts_mt32emu_source_last})
        string(JSON source GET "${restunts_mt32emu_manifest}" files ${index})
        get_filename_component(directory "${source}" DIRECTORY)
        install(FILES "${restunts_mt32emu_core}/${source}"
            DESTINATION "${restunts_mt32emu_package}/${directory}"
            COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    endforeach()
    install(FILES "${CMAKE_SOURCE_DIR}/cmake/mt32emu-rebuild.CMakeLists.txt"
        DESTINATION ${restunts_mt32emu_package}/rebuild RENAME CMakeLists.txt
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    install(FILES "${CMAKE_SOURCE_DIR}/cmake/mt32emu-rebuild.md"
        DESTINATION ${restunts_mt32emu_package} RENAME README-restunts.md
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mt32emu-build-info.txt"
        "${CMAKE_SOURCE_DIR}/cmake/mt32emu-source-manifest.json"
        DESTINATION ${restunts_mt32emu_package}
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    install(FILES "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
        DESTINATION ${restunts_mt32emu_package}/build-context
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    install(FILES "${CMAKE_SOURCE_DIR}/cmake/mt32emu.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/mt32emu-install.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/mt32emu-build-info.txt.in"
        "${CMAKE_SOURCE_DIR}/cmake/mt32emu-source-manifest.json"
        "${CMAKE_SOURCE_DIR}/cmake/mt32emu-rebuild.CMakeLists.txt"
        "${CMAKE_SOURCE_DIR}/cmake/mt32emu-rebuild.md"
        DESTINATION ${restunts_mt32emu_package}/build-context/cmake
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/cmake/toolchains/"
        DESTINATION ${restunts_mt32emu_package}/build-context/cmake/toolchains
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion}
        FILES_MATCHING PATTERN "*.cmake" PATTERN "*.h")
    if(EMSCRIPTEN)
        set(restunts_mt32emu_doc_directory .)
    else()
        set(restunts_mt32emu_doc_directory share/docs/restunts)
    endif()
    install(FILES "${CMAKE_SOURCE_DIR}/docs/mt32.md"
        DESTINATION ${restunts_mt32emu_doc_directory}
        COMPONENT ${component} ${restunts_mt32emu_install_exclusion})
endforeach()
