# Emscripten application packaging. Embed custom cars, artwork and optional music.
# Original game resources are selected by the player and loaded into MEMFS by the shell.
set(restunts_web_assets "${CMAKE_CURRENT_BINARY_DIR}/web-assets")
set(restunts_web_shell "${CMAKE_SOURCE_DIR}/src/restunts/platform/wasm/shell.html")
# Recreate only this generated directory so removed assets cannot linger.
file(REMOVE_RECURSE "${restunts_web_assets}")
foreach(car_file IN LISTS restunts_car_files)
    configure_file("${CMAKE_SOURCE_DIR}/assets/cars/${car_file}"
        "${restunts_web_assets}/assets/cars/${car_file}" COPYONLY)
endforeach()
foreach(theme desert tropical alpine city country)
    foreach(image scen sce2 sce3 sce4)
        configure_file("${CMAKE_SOURCE_DIR}/assets/skyboxes/${theme}-${image}.png"
            "${restunts_web_assets}/assets/skyboxes/${theme}-${image}.png" COPYONLY)
    endforeach()
endforeach()
foreach(opponent RANGE 1 6)
    configure_file("${CMAKE_SOURCE_DIR}/assets/opponents/game/opp${opponent}.png"
        "${restunts_web_assets}/assets/opponents/game/opp${opponent}.png" COPYONLY)
    foreach(outcome win lose)
        set(animation "opp${opponent}${outcome}.webm")
        if(EXISTS "${CMAKE_SOURCE_DIR}/assets/opponents/animations/${animation}")
            configure_file("${CMAKE_SOURCE_DIR}/assets/opponents/animations/${animation}"
                "${restunts_web_assets}/assets/opponents/animations/${animation}" COPYONLY)
        endif()
    endforeach()
endforeach()

foreach(background IN LISTS menu_backgrounds)
    configure_file("${CMAKE_SOURCE_DIR}/assets/menus/${background}.png"
        "${restunts_web_assets}/assets/menus/${background}.png" COPYONLY)
endforeach()

# Optional replacements are captured at configure time, like the artwork above.
foreach(track IN LISTS music_tracks)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/music/${track}.ogg")
        configure_file("${CMAKE_SOURCE_DIR}/assets/music/${track}.ogg"
            "${restunts_web_assets}/assets/music/${track}.ogg" COPYONLY)
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/wasm-link.cmake")
restunts_configure_wasm(restunts "${restunts_web_assets}" "${restunts_web_shell}")
install(TARGETS restunts RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES docs/wasm.md docs/track-editor.md docs/cstrk-format.md docs/csrpl-format.md docs/cshig-format.md
    THIRD-PARTY-NOTICES.txt DESTINATION . COMPONENT Runtime)
install(FILES third_party/nuked-opl2-lite/LICENSE
    DESTINATION share/licenses/restunts RENAME Nuked-OPL2-LICENSE COMPONENT Runtime)
install(FILES "${SDL3_SOURCE_DIR}/LICENSE.txt"
    DESTINATION share/licenses/restunts RENAME SDL-LICENSE.txt COMPONENT Runtime)

# The browser cannot replace a shared library. Ship the application objects,
# SDL/WebM archives, linker inputs and exact OPL source so users can relink instead.
set(restunts_relink_directory share/restunts/wasm-relink)
configure_file(cmake/wasm-relink.CMakeLists.txt.in
    "${CMAKE_CURRENT_BINARY_DIR}/wasm-relink/CMakeLists.txt" @ONLY)
configure_file(cmake/nuked-build-info.txt.in
    "${CMAKE_CURRENT_BINARY_DIR}/nuked-build-info.txt" @ONLY)
execute_process(COMMAND "${CMAKE_C_COMPILER}" --version
    OUTPUT_VARIABLE restunts_wasm_compiler_info OUTPUT_STRIP_TRAILING_WHITESPACE)
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/nuked-build-info.txt"
    "\nEmscripten SDK identification:\n${restunts_wasm_compiler_info}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wasm-relink/CMakeLists.txt"
    cmake/wasm-link.cmake cmake/wasm-relink.md "${restunts_web_shell}"
    "${CMAKE_CURRENT_BINARY_DIR}/nuked-build-info.txt"
    DESTINATION ${restunts_relink_directory} COMPONENT Runtime)
install(FILES "$<TARGET_FILE:restunts_game>"
    DESTINATION ${restunts_relink_directory}/lib RENAME librestunts_game.a COMPONENT Runtime)
install(FILES "$<TARGET_FILE:restunts_webm>"
    DESTINATION ${restunts_relink_directory}/lib RENAME librestunts_webm.a COMPONENT Runtime)
install(FILES "$<TARGET_FILE:SDL3::SDL3>"
    DESTINATION ${restunts_relink_directory}/lib RENAME libSDL3.a COMPONENT Runtime)
install(FILES $<TARGET_OBJECTS:restunts_entry>
    DESTINATION ${restunts_relink_directory}/objects COMPONENT Runtime)
install(DIRECTORY "${restunts_web_assets}/"
    DESTINATION ${restunts_relink_directory}/data COMPONENT Runtime)
install(FILES third_party/nuked-opl2-lite/opl2.c third_party/nuked-opl2-lite/opl2.h
    third_party/nuked-opl2-lite/CMakeLists.txt third_party/nuked-opl2-lite/LICENSE
    third_party/nuked-opl2-lite/README.md
    DESTINATION ${restunts_relink_directory}/nuked-opl2-lite COMPONENT Runtime)
