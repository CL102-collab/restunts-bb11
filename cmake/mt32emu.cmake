# Munt's emulator code is LGPL-2.1-or-later. Roland ROM data is never fetched.
include(FetchContent)
set(RESTUNTS_MT32EMU_REVISION 91d2205eb8f2591d177b257b92e2c75f830508de)
set(RESTUNTS_MT32EMU_ARCHIVE_SHA256
    0aa5e04248f04386efa1b32b69fc47d07d058bb1ee39e581af3fb01f50574487)
FetchContent_Declare(restunts_munt
    URL https://codeload.github.com/munt/munt/tar.gz/91d2205eb8f2591d177b257b92e2c75f830508de
    URL_HASH SHA256=0aa5e04248f04386efa1b32b69fc47d07d058bb1ee39e581af3fb01f50574487
    # Populate first so the pinned core's install rules can be normalized below.
    SOURCE_SUBDIR restunts-populate-only
    BINARY_DIR "${FETCHCONTENT_BASE_DIR}/restunts_munt-build/mt32emu"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

if(EMSCRIPTEN)
    set(libmt32emu_SHARED OFF CACHE BOOL "Browser Munt library is relinkable statically" FORCE)
else()
    set(libmt32emu_SHARED ON CACHE BOOL "Keep native Munt replaceable" FORCE)
endif()
set(libmt32emu_C_INTERFACE ON CACHE BOOL "Expose Munt's stable C API" FORCE)
set(libmt32emu_CPP_INTERFACE ON CACHE BOOL "Expose Munt's ROM metadata" FORCE)
set(libmt32emu_BUILD_TESTING OFF CACHE BOOL "Use the game's Munt integration tests" FORCE)
set(libmt32emu_WITH_INTERNAL_RESAMPLER ON CACHE BOOL "Use Munt's portable resampler" FORCE)
set(libmt32emu_WITH_VERSION_TAGGING OFF CACHE BOOL "Use the stable C ABI" FORCE)
set(libmt32emu_WITH_SYMBOL_VERSIONING OFF CACHE BOOL "Portable Munt shared library" FORCE)
FetchContent_MakeAvailable(restunts_munt)
set(RESTUNTS_MT32EMU_SOURCE_DIR "${restunts_munt_SOURCE_DIR}")

# OpenBSD's CMake treats NAMELINK_ONLY as NAMELINK_SKIP, so Munt's separate
# runtime and namelink rules register mt32emu twice in the same export set.
# Use the upstream TODO's single rule; NAMELINK_COMPONENT is supported by 3.25.
set(restunts_mt32emu_install_old [=[install(TARGETS mt32emu EXPORT MT32EmuTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} ${libmt32emu_COMPONENT_DEVEL}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# TODO: Use NAMELINK_COMPONENT with CMake 3.12 instead.
if(libmt32emu_SHARED)
  install(TARGETS mt32emu EXPORT MT32EmuTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ${libmt32emu_COMPONENT_DEVEL} NAMELINK_ONLY
  )
endif()]=])
set(restunts_mt32emu_install_new [=[# Chocolate Stunts modification (2026-11-13): combine the exported library installs.
install(TARGETS mt32emu EXPORT MT32EmuTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} ${libmt32emu_COMPONENT_DEVEL}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_COMPONENT Devel
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)]=])
set(restunts_mt32emu_cmake "${RESTUNTS_MT32EMU_SOURCE_DIR}/mt32emu/CMakeLists.txt")
file(READ "${restunts_mt32emu_cmake}" restunts_mt32emu_cmake_content)
string(FIND "${restunts_mt32emu_cmake_content}" "${restunts_mt32emu_install_old}"
    restunts_mt32emu_install_position)
if(NOT restunts_mt32emu_install_position EQUAL -1)
    string(REPLACE "${restunts_mt32emu_install_old}" "${restunts_mt32emu_install_new}"
        restunts_mt32emu_cmake_content "${restunts_mt32emu_cmake_content}")
    file(WRITE "${restunts_mt32emu_cmake}" "${restunts_mt32emu_cmake_content}")
else()
    string(FIND "${restunts_mt32emu_cmake_content}" "${restunts_mt32emu_install_new}"
        restunts_mt32emu_install_position)
    if(restunts_mt32emu_install_position EQUAL -1)
        message(FATAL_ERROR "The pinned Munt core's install rules have changed")
    endif()
endif()
# Exclude upstream install rules; our curated source kit and runtime ship below.
add_subdirectory("${RESTUNTS_MT32EMU_SOURCE_DIR}/mt32emu"
    "${restunts_munt_BINARY_DIR}" EXCLUDE_FROM_ALL)
set_target_properties(mt32emu PROPERTIES
    OUTPUT_NAME mt32emu RUNTIME_OUTPUT_NAME mt32emu
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    POSITION_INDEPENDENT_CODE ON)
set_property(TARGET mt32emu PROPERTY VERSION)
set_property(TARGET mt32emu PROPERTY SOVERSION)
if(APPLE)
    set_target_properties(mt32emu PROPERTIES MACOSX_RPATH ON INSTALL_NAME_DIR "@rpath")
endif()
if(WIN32)
    set_target_properties(mt32emu PROPERTIES PREFIX "")
endif()
if(WIN32 AND MINGW)
    target_link_options(mt32emu PRIVATE -static-libgcc -static-libstdc++)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/mt32emu-install.cmake" OPTIONAL)
