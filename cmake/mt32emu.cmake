# Munt's emulator code is LGPL-2.1-or-later. Roland ROM data is never fetched.
include(FetchContent)
set(RESTUNTS_MT32EMU_REVISION 91d2205eb8f2591d177b257b92e2c75f830508de)
set(RESTUNTS_MT32EMU_ARCHIVE_SHA256
    0aa5e04248f04386efa1b32b69fc47d07d058bb1ee39e581af3fb01f50574487)
FetchContent_Declare(restunts_munt
    URL https://codeload.github.com/munt/munt/tar.gz/91d2205eb8f2591d177b257b92e2c75f830508de
    URL_HASH SHA256=0aa5e04248f04386efa1b32b69fc47d07d058bb1ee39e581af3fb01f50574487
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(restunts_munt)
if(NOT restunts_munt_POPULATED)
    FetchContent_Populate(restunts_munt)
endif()
set(RESTUNTS_MT32EMU_SOURCE_DIR "${restunts_munt_SOURCE_DIR}")

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
add_subdirectory("${RESTUNTS_MT32EMU_SOURCE_DIR}/mt32emu"
    "${restunts_munt_BINARY_DIR}/mt32emu" EXCLUDE_FROM_ALL)
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
