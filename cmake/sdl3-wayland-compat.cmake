# The pinned wayland.xml marks two stable version-1 interfaces as frozen.
# Older wayland-scanner versions reject that metadata in their DTD. The
# attribute only enforces version 1, so it does not affect generated bindings.
set(protocol_file "${SDL3_SOURCE_DIR}/wayland-protocols/wayland.xml")
file(READ "${protocol_file}" protocol)
string(REPLACE " version=\"1\" frozen=\"true\">" " version=\"1\">" compatible_protocol "${protocol}")
if(NOT protocol STREQUAL compatible_protocol)
    file(WRITE "${protocol_file}" "${compatible_protocol}")
endif()
