cmake_minimum_required(VERSION 3.25)
project(restunts_launcher_prototype LANGUAGES C CXX RC)

# Standalone experiment: the normal game build does not depend on wxWidgets.
if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "This feasibility prototype requires 32-bit Windows and MinGW-w64")
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RESTUNTS_WINDOWS_XP_VERSION 0x0501)
set(RESTUNTS_WINDOWS_XP_SUBSYSTEM_MAJOR 5)
set(RESTUNTS_WINDOWS_XP_SUBSYSTEM_MINOR 1)
add_compile_definitions(_WIN32_WINNT=${RESTUNTS_WINDOWS_XP_VERSION}
    WINVER=${RESTUNTS_WINDOWS_XP_VERSION})
# Apply to wxWidgets too. Imported compiler/runtime code is audited separately.
add_compile_options(-march=i586 -mno-mmx -mno-sse -mno-sse2 -mno-avx -mfpmath=387)
string(CONCAT restunts_subsystem_option
    "-Wl,--major-subsystem-version,${RESTUNTS_WINDOWS_XP_SUBSYSTEM_MAJOR},"
    "--minor-subsystem-version,${RESTUNTS_WINDOWS_XP_SUBSYSTEM_MINOR}")
add_link_options(-static -static-libgcc -static-libstdc++ "${restunts_subsystem_option}")

set(wxBUILD_TOOLKIT msw CACHE STRING "Use native Windows controls" FORCE)
set(wxBUILD_SHARED OFF CACHE BOOL "Static wxWidgets" FORCE)
set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "No compiler runtime DLLs" FORCE)
set(wxBUILD_PRECOMP ON CACHE STRING "Compile wxWidgets headers once per library" FORCE)
foreach(feature INSTALL SAMPLES TESTS DEMOS BENCHMARKS LOCALES)
    set(wxBUILD_${feature} OFF CACHE BOOL "Minimal launcher prototype" FORCE)
endforeach()
foreach(feature AUI HTML MEDIACTRL PROPGRID DEBUGREPORT RIBBON RICHTEXT STC WEBVIEW
        XRC XML SOCKETS WEBREQUEST OPENGL GRAPHICS_CONTEXT GRAPHICS_DIRECT2D
        LIBJPEG LIBPNG LIBTIFF NANOSVG REGEX ZLIB EXPAT HELP SVG ARTPROVIDER_TANGO
        PROTOCOL PROTOCOL_FILE PROTOCOL_FTP PROTOCOL_HTTP URL FS_INET IPC DIALUP_MANAGER
        WXHTML_HELP ZIPSTREAM FS_ZIP TASKBARICON TASKBARBUTTON NOTIFICATION_MESSAGE)
    set(wxUSE_${feature} OFF CACHE STRING "Unused by the launcher prototype" FORCE)
endforeach()

include(FetchContent)
FetchContent_Declare(wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.tar.bz2
    URL_HASH SHA256=a62719bb5e1dcc41c1a6fc1eecd499c7ee8521f70402c7b1bb3342a1e16344e0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wxWidgets)

add_executable(restunts-launcher-prototype WIN32 main.cpp prototype.rc)
target_link_libraries(restunts-launcher-prototype PRIVATE wx::core wx::base)
target_compile_options(restunts-launcher-prototype PRIVATE -Wall -Wextra -Werror)

add_executable(launcher-probe-child WIN32 probe_child.c)
target_link_libraries(launcher-probe-child PRIVATE shell32)
target_compile_options(launcher-probe-child PRIVATE -Wall -Wextra -Werror)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
foreach(prototype restunts-launcher-prototype launcher-probe-child)
    # The compiler's prebuilt runtime can import newer APIs despite the XP macros.
    set_property(TARGET ${prototype} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/check_windows.py")
    add_custom_command(TARGET ${prototype} POST_BUILD
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${prototype}-audit.json"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_windows.py"
            "$<TARGET_FILE:${prototype}>" --objdump "${CMAKE_OBJDUMP}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/${prototype}-audit.json" --quiet
        VERBATIM)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${prototype}-audit.json" DESTINATION .)
endforeach()

install(TARGETS restunts-launcher-prototype launcher-probe-child RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
install(FILES "${wxwidgets_SOURCE_DIR}/docs/licence.txt"
    "${wxwidgets_SOURCE_DIR}/docs/lgpl.txt" DESTINATION licenses/wxWidgets)
