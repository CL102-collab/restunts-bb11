# Older wayland-scanner versions reject frozen and deprecated-since metadata
# in their DTD. These attributes affect validation and generated comments only.
foreach(protocol_name IN ITEMS wayland.xml color-management-v1.xml)
    set(protocol_file "${SDL3_SOURCE_DIR}/wayland-protocols/${protocol_name}")
    file(READ "${protocol_file}" protocol)
    string(REPLACE " version=\"1\" frozen=\"true\">" " version=\"1\">"
        compatible_protocol "${protocol}")
    string(REGEX REPLACE " deprecated-since=\"[0-9]+\"" ""
        compatible_protocol "${compatible_protocol}")
    if(NOT protocol STREQUAL compatible_protocol)
        file(WRITE "${protocol_file}" "${compatible_protocol}")
    endif()
endforeach()

# Legacy glibc already defines a fortified printf macro. SDL's Steam driver
# defines its own alias but never uses it.
set(steam_file "${SDL3_SOURCE_DIR}/src/joystick/hidapi/SDL_hidapi_steam.c")
file(READ "${steam_file}" steam_source)
string(REPLACE "#define printf SDL_Log\n" "" compatible_steam_source "${steam_source}")
if(NOT steam_source STREQUAL compatible_steam_source)
    file(WRITE "${steam_file}" "${compatible_steam_source}")
endif()
