# Cross-built Haiku tests run later on Haiku, after copying the checkout and
# build tree to the same absolute paths. Keep ordinary cross builds unchanged.
option(RESTUNTS_TESTS_ON_TARGET "Register cross-built Haiku regressions for execution on Haiku" OFF)
if(RESTUNTS_TESTS_ON_TARGET)
    if(NOT RESTUNTS_BUILD_TESTS OR NOT CMAKE_CROSSCOMPILING
            OR NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        message(FATAL_ERROR
            "RESTUNTS_TESTS_ON_TARGET requires a Haiku cross build with RESTUNTS_BUILD_TESTS=ON")
    endif()
    set(RESTUNTS_TARGET_TEST_PYTHON python3 CACHE STRING "Python command on the test target")
    set(RESTUNTS_TARGET_TEST_NODE "" CACHE STRING
        "Optional Node.js command on the test target; empty disables JavaScript tests")
    set(RESTUNTS_TARGET_TEST_CMAKE cmake CACHE STRING "CMake command on the test target")
    set(RESTUNTS_TARGET_TEST_CC gcc CACHE STRING "C compiler command on the test target")
    foreach(program PYTHON CMAKE CC)
        if(NOT RESTUNTS_TARGET_TEST_${program})
            message(FATAL_ERROR "RESTUNTS_TARGET_TEST_${program} must name a target command")
        endif()
    endforeach()
endif()

set(restunts_tests_runnable OFF)
if(NOT CMAKE_CROSSCOMPILING OR RESTUNTS_TESTS_ON_TARGET)
    set(restunts_tests_runnable ON)
endif()

# Keep configure-time asset generators on the host Python and compiler. Only
# CTest commands use these aliases, resolved from the guest's environment/PATH.
set(restunts_test_python "${Python3_EXECUTABLE}")
set(restunts_test_cmake "${CMAKE_COMMAND}")
set(restunts_test_compiler "${CMAKE_C_COMPILER}")
if(RESTUNTS_TESTS_ON_TARGET)
    set(restunts_test_python "${RESTUNTS_TARGET_TEST_PYTHON}")
    set(restunts_test_cmake "${RESTUNTS_TARGET_TEST_CMAKE}")
    set(restunts_test_compiler "${RESTUNTS_TARGET_TEST_CC}")
endif()
