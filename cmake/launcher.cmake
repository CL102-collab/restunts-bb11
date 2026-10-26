# Keep desktop GUI dependencies separate from the SDL game and command-line tools.
set(restunts_system_wx_default OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    # HaikuPorts carries native-platform fixes absent from upstream wxWidgets.
    set(restunts_system_wx_default ON)
endif()
option(RESTUNTS_SYSTEM_WX "Use installed wxWidgets 3.2 instead of the pinned source"
    ${restunts_system_wx_default})

function(restunts_add_desktop_launcher)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(launcher_directory "${CMAKE_SOURCE_DIR}/src/restunts/launcher")
    set(genghost_gui_directory "${CMAKE_SOURCE_DIR}/src/restunts/genghost_gui")
    if(RESTUNTS_TARGET_X86 AND NOT RESTUNTS_SSE2)
        add_compile_options(-mno-mmx)
    endif()
    if(RESTUNTS_SYSTEM_WX)
        if(CMAKE_SYSTEM_NAME STREQUAL "Haiku" AND CMAKE_HAIKU_SECONDARY_ARCH
                AND NOT wxWidgets_CONFIG_EXECUTABLE)
            # Hybrid Haiku's primary and modern C++ libraries use different ABIs.
            # FindwxWidgets does not add the secondary executable directories.
            find_program(wxWidgets_CONFIG_EXECUTABLE NAMES wx-config wx-config-3.2
                HINTS
                    "/boot/home/config/non-packaged/bin/${CMAKE_HAIKU_SECONDARY_ARCH}"
                    "/boot/system/non-packaged/bin/${CMAKE_HAIKU_SECONDARY_ARCH}"
                    "/boot/system/bin/${CMAKE_HAIKU_SECONDARY_ARCH}"
                NO_DEFAULT_PATH REQUIRED)
        endif()
        find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
        add_library(restunts_launcher_wx INTERFACE)
        target_include_directories(restunts_launcher_wx INTERFACE ${wxWidgets_INCLUDE_DIRS})
        target_compile_definitions(restunts_launcher_wx INTERFACE ${wxWidgets_DEFINITIONS})
        if(WIN32)
            target_compile_definitions(restunts_launcher_wx INTERFACE UNICODE _UNICODE)
        endif()
        separate_arguments(wx_flags NATIVE_COMMAND "${wxWidgets_CXX_FLAGS}")
        target_compile_options(restunts_launcher_wx INTERFACE ${wx_flags})
        target_link_directories(restunts_launcher_wx INTERFACE ${wxWidgets_LIBRARY_DIRS})
        target_link_libraries(restunts_launcher_wx INTERFACE ${wxWidgets_LIBRARIES})
        set(launcher_wx_libraries restunts_launcher_wx)
        set(launcher_test_wx_libraries restunts_launcher_wx)
        if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
            # HaikuPorts wxQt 3.2.4 supplies the library; retain its upstream
            # license texts in the portable runtime alongside the notices.
            include(FetchContent)
            foreach(license licence lgpl)
                if(license STREQUAL "licence")
                    set(license_hash e8453be21aea2b7cc9a39d5b0475190bbfa0a60605b4178377590b94d9c43eae)
                    set(license_name wxWidgets-LICENSE)
                else()
                    set(license_hash b7993225104d90ddd8024fd838faf300bea5e83d91203eab98e29512acebd69c)
                    set(license_name wxWidgets-LGPL.txt)
                endif()
                set(license_content restunts_wx_haiku_${license})
                FetchContent_Declare(${license_content}
                    URL https://raw.githubusercontent.com/wxWidgets/wxWidgets/v3.2.4/docs/${license}.txt
                    URL_HASH SHA256=${license_hash}
                    DOWNLOAD_NO_EXTRACT TRUE)
                FetchContent_MakeAvailable(${license_content})
                install(FILES "${${license_content}_SOURCE_DIR}/${license}.txt"
                    DESTINATION share/licenses/restunts RENAME ${license_name} COMPONENT Runtime)
            endforeach()
        endif()
    else()
        include(FetchContent)
        if(WIN32)
            set(launcher_toolkit msw)
        elseif(APPLE)
            set(launcher_toolkit osx_cocoa)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
            set(launcher_toolkit qt)
        else()
            set(launcher_toolkit gtk3)
        endif()
        set(RESTUNTS_WX_TOOLKIT "${launcher_toolkit}" CACHE STRING "Desktop launcher toolkit")
        set_property(CACHE RESTUNTS_WX_TOOLKIT PROPERTY STRINGS msw osx_cocoa gtk2 gtk3 qt)
        set(wxBUILD_TOOLKIT "${RESTUNTS_WX_TOOLKIT}" CACHE STRING "Desktop controls" FORCE)
        set(wxBUILD_SHARED OFF CACHE BOOL "Bundle wxWidgets in the launcher" FORCE)
        if(WIN32)
            set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "Bundle the Windows compiler runtime" FORCE)
        endif()
        set(wxBUILD_PRECOMP ON CACHE STRING "Share wxWidgets precompiled headers" FORCE)
        set(wxBUILD_CXX_STANDARD 11 CACHE STRING "Launcher C++ standard" FORCE)
        foreach(feature INSTALL SAMPLES TESTS DEMOS BENCHMARKS LOCALES)
            set(wxBUILD_${feature} OFF CACHE BOOL "Only the desktop launcher needs wxWidgets" FORCE)
        endforeach()
        foreach(feature AUI HTML MEDIACTRL PROPGRID DEBUGREPORT RIBBON RICHTEXT STC WEBVIEW
                XRC XML SOCKETS WEBREQUEST OPENGL GRAPHICS_DIRECT2D LIBJPEG LIBPNG LIBTIFF
                NANOSVG REGEX ZLIB EXPAT HELP SVG ARTPROVIDER_TANGO PROTOCOL PROTOCOL_FILE
                PROTOCOL_FTP PROTOCOL_HTTP URL FS_INET IPC DIALUP_MANAGER WXHTML_HELP
                ZIPSTREAM FS_ZIP TASKBARICON TASKBARBUTTON NOTIFICATION_MESSAGE SOUND JOYSTICK
                FSWATCHER)
            set(wxUSE_${feature} OFF CACHE STRING "Unused by the desktop launcher" FORCE)
        endforeach()
        if(APPLE OR RESTUNTS_WX_TOOLKIT STREQUAL "gtk3")
            set(wxUSE_GRAPHICS_CONTEXT ON CACHE BOOL "Required by the native toolkit" FORCE)
        else()
            set(wxUSE_GRAPHICS_CONTEXT OFF CACHE BOOL "Use native dialog controls" FORCE)
        endif()
        FetchContent_Declare(wxWidgets
            URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.tar.bz2
            URL_HASH SHA256=a62719bb5e1dcc41c1a6fc1eecd499c7ee8521f70402c7b1bb3342a1e16344e0
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(wxWidgets)
        set(launcher_wx_libraries wx::core wx::base)
        set(launcher_test_wx_libraries wx::base)
        install(FILES "${wxwidgets_SOURCE_DIR}/docs/licence.txt"
            DESTINATION share/licenses/restunts RENAME wxWidgets-LICENSE COMPONENT Runtime)
        install(FILES "${wxwidgets_SOURCE_DIR}/docs/lgpl.txt"
            DESTINATION share/licenses/restunts RENAME wxWidgets-LGPL.txt COMPONENT Runtime)
    endif()
    add_executable(restunts-launcher WIN32
        "${launcher_directory}/main.cpp" "${launcher_directory}/options.cpp"
        "${launcher_directory}/game_data.cpp")
    add_executable(genghost-gui WIN32
        "${genghost_gui_directory}/main.cpp" "${genghost_gui_directory}/options.cpp"
        "${launcher_directory}/options.cpp" "${launcher_directory}/game_data.cpp")
    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku" OR RESTUNTS_WX_TOOLKIT STREQUAL "qt"
            OR "__WXQT__" IN_LIST wxWidgets_DEFINITIONS)
        if(CMAKE_SYSTEM_NAME STREQUAL "Haiku" AND CMAKE_HAIKU_SECONDARY_ARCH AND NOT Qt5_DIR)
            # Keep direct Qt calls in the same modern ABI as the wxQt library.
            find_path(Qt5_DIR Qt5Config.cmake
                HINTS
                    "/boot/home/config/non-packaged/lib/${CMAKE_HAIKU_SECONDARY_ARCH}/cmake/Qt5"
                    "/boot/system/non-packaged/lib/${CMAKE_HAIKU_SECONDARY_ARCH}/cmake/Qt5"
                    "/boot/system/lib/${CMAKE_HAIKU_SECONDARY_ARCH}/cmake/Qt5"
                NO_DEFAULT_PATH REQUIRED)
        endif()
        find_package(Qt5 REQUIRED COMPONENTS Widgets)
        list(APPEND launcher_wx_libraries Qt5::Widgets)
    endif()
    foreach(desktop_gui restunts-launcher genghost-gui)
        if(WIN32)
            target_sources(${desktop_gui} PRIVATE "${launcher_directory}/launcher.rc")
        endif()
        target_link_libraries(${desktop_gui} PRIVATE ${launcher_wx_libraries})
        target_compile_features(${desktop_gui} PRIVATE cxx_std_11)
        if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
            target_link_libraries(${desktop_gui} PRIVATE be)
        endif()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${desktop_gui} PRIVATE -Wall -Wextra -Werror)
        endif()
        if(WIN32 AND RESTUNTS_TARGET_X86)
            set(launcher_xp_subsystem_major 5)
            set(launcher_xp_subsystem_minor 1)
            target_link_options(${desktop_gui} PRIVATE
                "-Wl,--major-subsystem-version,${launcher_xp_subsystem_major},--minor-subsystem-version,${launcher_xp_subsystem_minor}")
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            set(checker "${CMAKE_SOURCE_DIR}/tools/launcher-prototype/check_windows.py")
            set(audit "${CMAKE_CURRENT_BINARY_DIR}/${desktop_gui}-audit.json")
            set(instruction_policy)
            if(RESTUNTS_SSE2)
                list(APPEND instruction_policy --allow-simd)
            endif()
            set_property(TARGET ${desktop_gui} APPEND PROPERTY LINK_DEPENDS "${checker}")
            add_custom_command(TARGET ${desktop_gui} POST_BUILD
                BYPRODUCTS "${audit}"
                COMMAND "${Python3_EXECUTABLE}" "${checker}" "$<TARGET_FILE:${desktop_gui}>"
                    --objdump "${CMAKE_OBJDUMP}" --output "${audit}" --quiet ${instruction_policy}
                VERBATIM)
            install(FILES "${audit}" DESTINATION share/restunts COMPONENT Runtime)
        endif()
    endforeach()
    install(TARGETS restunts-launcher genghost-gui RUNTIME DESTINATION bin COMPONENT Runtime)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        install(FILES
            third_party/gcc-runtime/COPYING3.txt
            third_party/gcc-runtime/COPYING.RUNTIME.txt
            third_party/gcc-runtime/gcc-mingw-w64-copyright.txt
            third_party/gcc-runtime/README.md
            DESTINATION share/licenses/gcc-runtime COMPONENT Runtime)
    elseif(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        install(FILES
            third_party/llvm-runtime/libcxx-LICENSE.txt
            third_party/llvm-runtime/libcxxabi-LICENSE.txt
            third_party/llvm-runtime/libunwind-LICENSE.txt
            third_party/llvm-runtime/compiler-rt-LICENSE.txt
            third_party/llvm-runtime/README.md
            DESTINATION share/licenses/llvm-runtime COMPONENT Runtime)
    endif()
    install(FILES docs/launcher.md DESTINATION share/docs/restunts COMPONENT Runtime)
    if(APPLE)
        set(bundle_directory "${CMAKE_CURRENT_BINARY_DIR}/Restunts.app/Contents")
        file(MAKE_DIRECTORY "${bundle_directory}/MacOS")
        configure_file("${launcher_directory}/Info.plist.in" "${bundle_directory}/Info.plist" @ONLY)
        install(FILES "${bundle_directory}/Info.plist"
            DESTINATION Restunts.app/Contents COMPONENT Runtime)
        install(PROGRAMS "${launcher_directory}/macos-launcher.sh"
            DESTINATION Restunts.app/Contents/MacOS RENAME restunts-launcher COMPONENT Runtime)
        set(genghost_bundle_directory "${CMAKE_CURRENT_BINARY_DIR}/Genghost.app/Contents")
        file(MAKE_DIRECTORY "${genghost_bundle_directory}/MacOS")
        configure_file("${genghost_gui_directory}/Info.plist.in"
            "${genghost_bundle_directory}/Info.plist" @ONLY)
        install(FILES "${genghost_bundle_directory}/Info.plist"
            DESTINATION Genghost.app/Contents COMPONENT Runtime)
        install(PROGRAMS "${genghost_gui_directory}/macos-genghost.sh"
            DESTINATION Genghost.app/Contents/MacOS RENAME genghost-gui COMPONENT Runtime)
    elseif(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        install(FILES "${launcher_directory}/restunts.desktop"
            "${genghost_gui_directory}/genghost.desktop"
            DESTINATION share/applications COMPONENT Runtime)
        install(FILES site/assets/favicon.svg DESTINATION share/icons/hicolor/scalable/apps
            RENAME restunts.svg COMPONENT Runtime)
    endif()
    if(RESTUNTS_BUILD_TESTS)
        foreach(check options game_data)
            string(REPLACE "_" "-" test_suffix "${check}")
            add_executable(test-launcher-${test_suffix}
                "${launcher_directory}/test_${check}.cpp" "${launcher_directory}/${check}.cpp")
            target_link_libraries(test-launcher-${test_suffix} PRIVATE ${launcher_test_wx_libraries})
            target_compile_definitions(test-launcher-${test_suffix} PRIVATE wxUSE_GUI=0)
            target_compile_features(test-launcher-${test_suffix} PRIVATE cxx_std_11)
            if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
                target_link_libraries(test-launcher-${test_suffix} PRIVATE be)
            endif()
            add_test(NAME launcher-${test_suffix} COMMAND test-launcher-${test_suffix})
        endforeach()
        add_executable(test-genghost-gui-options
            "${genghost_gui_directory}/test_options.cpp" "${genghost_gui_directory}/options.cpp"
            "${launcher_directory}/options.cpp")
        target_link_libraries(test-genghost-gui-options PRIVATE ${launcher_test_wx_libraries})
        target_compile_definitions(test-genghost-gui-options PRIVATE wxUSE_GUI=0)
        target_compile_features(test-genghost-gui-options PRIVATE cxx_std_11)
        if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
            target_link_libraries(test-genghost-gui-options PRIVATE be)
        endif()
        add_test(NAME genghost-gui-options COMMAND test-genghost-gui-options)
    endif()
endfunction()

restunts_add_desktop_launcher()
