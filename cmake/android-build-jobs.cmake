# Gradle passes its worker limit explicitly. Direct CMake builds use the same
# requested job count as other package builds, or all available processors.
if(NOT "$ENV{RESTUNTS_BUILD_JOBS}" STREQUAL "")
    set(restunts_android_default_jobs "$ENV{RESTUNTS_BUILD_JOBS}")
elseif(NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(restunts_android_default_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
    include(ProcessorCount)
    ProcessorCount(restunts_android_default_jobs)
    if(NOT restunts_android_default_jobs)
        set(restunts_android_default_jobs 1)
    endif()
endif()
set(RESTUNTS_ANDROID_BUILD_JOBS "${restunts_android_default_jobs}" CACHE STRING
    "Concurrent Android native build jobs")
if(NOT RESTUNTS_ANDROID_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "RESTUNTS_ANDROID_BUILD_JOBS must be a positive integer")
endif()
# A shared pool permits parallel linking without allowing compile/link work
# together to exceed the selected processor count when Gradle invokes Ninja.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS
    "restunts_android_build=${RESTUNTS_ANDROID_BUILD_JOBS}")
set(CMAKE_JOB_POOL_COMPILE restunts_android_build)
set(CMAKE_JOB_POOL_LINK restunts_android_build)
