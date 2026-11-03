# The scene renderer loads Vulkan at runtime. Software-only startup therefore
# needs no graphics driver, and legacy Windows/Android users retain their ABI.
add_library(restunts_graphics STATIC "${CMAKE_SOURCE_DIR}/src/restunts/c/renderer/graphics_backend.c")
set_target_properties(restunts_graphics PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(restunts_graphics PUBLIC "${CMAKE_SOURCE_DIR}/src/restunts/c")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(restunts_graphics PRIVATE -fsigned-char -fno-strict-aliasing
        -Wall -Wextra -Wno-missing-field-initializers)
endif()

set(restunts_native_vulkan OFF)
if(NOT APPLE AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "DOS"
        AND NOT RESTUNTS_GENGHOST_ONLY)
    set(restunts_native_vulkan ON)
    # Headers are portable build inputs; no host Vulkan SDK or link library is
    # required when cross-compiling an optional runtime driver.
    include(FetchContent)
    set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "Only Vulkan C declarations are needed" FORCE)
    set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "Do not build SDK tests" FORCE)
    set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "Only install application dependencies" FORCE)
    FetchContent_Declare(restunts_vulkan_headers
        URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.tar.gz
        URL_HASH SHA256=e204e0b3c19f622d197df945737f5db913d6621830999b8578d34e80a7c90585
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(restunts_vulkan_headers)
    target_sources(restunts_graphics PRIVATE
        "${CMAKE_SOURCE_DIR}/src/restunts/c/renderer/vulkan_context.c"
        "${CMAKE_SOURCE_DIR}/src/restunts/c/renderer/vulkan_dispatch.c")
    target_compile_definitions(restunts_graphics PRIVATE RESTUNTS_NATIVE_VULKAN)
    target_link_libraries(restunts_graphics PRIVATE Vulkan::Headers ${CMAKE_DL_LIBS})
    install(FILES "${restunts_vulkan_headers_SOURCE_DIR}/LICENSE.md"
        DESTINATION share/licenses/restunts RENAME Vulkan-Headers-LICENSE.md COMPONENT Runtime)
    install(FILES "${restunts_vulkan_headers_SOURCE_DIR}/LICENSES/Apache-2.0.txt"
        DESTINATION share/licenses/restunts RENAME Vulkan-Headers-Apache-2.0.txt COMPONENT Runtime)
    install(FILES "${restunts_vulkan_headers_SOURCE_DIR}/LICENSES/MIT.txt"
        DESTINATION share/licenses/restunts RENAME Vulkan-Headers-MIT.txt COMPONENT Runtime)
endif()

target_link_libraries(restunts_options INTERFACE restunts_graphics)
