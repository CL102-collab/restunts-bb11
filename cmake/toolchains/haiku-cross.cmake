# Linux-hosted modern GCC tools and matching Haiku R1/beta6 development files.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    RESTUNTS_HAIKU_TARGET RESTUNTS_HAIKU_SDK_ROOT)
if(NOT RESTUNTS_HAIKU_SDK_ROOT OR NOT IS_ABSOLUTE "${RESTUNTS_HAIKU_SDK_ROOT}")
    message(FATAL_ERROR "RESTUNTS_HAIKU_SDK_ROOT must name an absolute prepared SDK path")
endif()
if(NOT EXISTS "${RESTUNTS_HAIKU_SDK_ROOT}/restunts-target")
    message(FATAL_ERROR "The Haiku SDK is missing its restunts-target marker")
endif()
file(READ "${RESTUNTS_HAIKU_SDK_ROOT}/restunts-target" restunts_haiku_sdk_target)
string(STRIP "${restunts_haiku_sdk_target}" restunts_haiku_sdk_target)
if(NOT restunts_haiku_sdk_target STREQUAL RESTUNTS_HAIKU_TARGET)
    message(FATAL_ERROR "The Haiku SDK does not match ${RESTUNTS_HAIKU_TARGET}")
endif()
if(RESTUNTS_HAIKU_TARGET STREQUAL "haiku-x64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(restunts_haiku_arch_subdir "")
elseif(RESTUNTS_HAIKU_TARGET STREQUAL "haiku-x86")
    set(CMAKE_SYSTEM_PROCESSOR i586)
    set(restunts_haiku_arch_subdir /x86)
    set(CMAKE_LIBRARY_ARCHITECTURE x86)
    set(CMAKE_C_LIBRARY_ARCHITECTURE x86)
    set(CMAKE_CXX_LIBRARY_ARCHITECTURE x86)
else()
    message(FATAL_ERROR "Unsupported Haiku cross target: ${RESTUNTS_HAIKU_TARGET}")
endif()
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSROOT "${RESTUNTS_HAIKU_SDK_ROOT}/sysroot")
if(NOT IS_DIRECTORY "${CMAKE_SYSROOT}/boot/system/develop")
    message(FATAL_ERROR "The Haiku SDK is missing its target development sysroot")
endif()

# These wrappers run on Linux and select the SDK's modern target compiler ABI.
# In particular, the hybrid x86 SDK must never invoke the guest's primary GCC2.
foreach(restunts_haiku_tool C_COMPILER:cc CXX_COMPILER:c++ AR:ar RANLIB:ranlib
        LINKER:ld READELF:readelf STRIP:strip OBJDUMP:objdump)
    string(REPLACE ":" ";" restunts_haiku_tool_parts "${restunts_haiku_tool}")
    list(GET restunts_haiku_tool_parts 0 restunts_haiku_variable)
    list(GET restunts_haiku_tool_parts 1 restunts_haiku_filename)
    set(restunts_haiku_program "${RESTUNTS_HAIKU_SDK_ROOT}/bin/${restunts_haiku_filename}")
    if(NOT EXISTS "${restunts_haiku_program}")
        message(FATAL_ERROR "The Haiku SDK is missing its host tool: ${restunts_haiku_program}")
    endif()
    set(CMAKE_${restunts_haiku_variable} "${restunts_haiku_program}")
endforeach()
if(EXISTS "${RESTUNTS_HAIKU_SDK_ROOT}/bin/nm")
    set(CMAKE_NM "${RESTUNTS_HAIKU_SDK_ROOT}/bin/nm")
endif()

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
list(PREPEND CMAKE_PREFIX_PATH
    /boot/system/non-packaged/develop /boot/system/develop
    /boot/system/non-packaged /boot/system)
list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)

# wx-config is a host-executable wrapper that reports rooted target flags.
# CMake's standard FindwxWidgets must not discover the Linux host's wxWidgets.
if(NOT wxWidgets_CONFIG_EXECUTABLE AND EXISTS "${RESTUNTS_HAIKU_SDK_ROOT}/bin/wx-config")
    set(wxWidgets_CONFIG_EXECUTABLE "${RESTUNTS_HAIKU_SDK_ROOT}/bin/wx-config"
        CACHE FILEPATH "Haiku SDK wxWidgets configuration wrapper")
endif()

# pkg-config executes on Linux but describes only the selected target ABI.
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH FALSE)
set(restunts_haiku_pkgconfig_dirs)
foreach(restunts_haiku_prefix /boot/system/non-packaged /boot/system)
    list(APPEND restunts_haiku_pkgconfig_dirs
        "${CMAKE_SYSROOT}${restunts_haiku_prefix}/develop/lib${restunts_haiku_arch_subdir}/pkgconfig"
        "${CMAKE_SYSROOT}${restunts_haiku_prefix}/lib${restunts_haiku_arch_subdir}/pkgconfig"
        "${CMAKE_SYSROOT}${restunts_haiku_prefix}/data/pkgconfig")
endforeach()
list(JOIN restunts_haiku_pkgconfig_dirs ":" restunts_haiku_pkgconfig_path)
set(ENV{PKG_CONFIG_LIBDIR} "${restunts_haiku_pkgconfig_path}")
