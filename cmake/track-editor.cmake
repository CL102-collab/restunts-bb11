# Generate the same offline editor for every SDL3 runtime, including Android and DOS.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(restunts_editor_source "${CMAKE_SOURCE_DIR}/src/restunts/platform/wasm")
set(restunts_editor_builder "${CMAKE_SOURCE_DIR}/tools/scripts/build-track-editor.py")
set(restunts_editor_html "${CMAKE_CURRENT_BINARY_DIR}/tracked.html")
set(restunts_editor_inputs "${restunts_editor_builder}")
foreach(editor_file track-editor.html track-editor.css track-editor.js
        track-editor-model.js track-editor-resources.js
        track-editor-route-data.js track-editor-validation.js)
    list(APPEND restunts_editor_inputs "${restunts_editor_source}/${editor_file}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${restunts_editor_inputs})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${restunts_editor_builder}"
    --output "${restunts_editor_html}"
    RESULT_VARIABLE restunts_editor_result
    ERROR_VARIABLE restunts_editor_error)
if(NOT restunts_editor_result EQUAL 0)
    message(FATAL_ERROR "Unable to bundle the track editor: ${restunts_editor_error}")
endif()
if(EMSCRIPTEN)
    install(FILES "${restunts_editor_html}" DESTINATION . COMPONENT Runtime)
else()
    install(FILES "${restunts_editor_html}"
        DESTINATION share/restunts/editor COMPONENT Runtime)
    install(FILES docs/cstrk-format.md docs/csrpl-format.md docs/cshig-format.md docs/track-editor.md
        DESTINATION share/docs/restunts COMPONENT Runtime)
endif()
