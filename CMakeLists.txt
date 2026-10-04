cmake_minimum_required(VERSION 3.25)

# ------------------------------------------------------------------------------
# Project
# ------------------------------------------------------------------------------
set(APP_NAME "vulkan_app" CACHE STRING "Executable name")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(${APP_NAME}
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put all generated files in one directory tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# For single-config generators (Ninja/Makefiles), default to Debug.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Build type"
        FORCE
    )
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(ENABLE_PCH
    "Use a precompiled header"
    ON
)

option(ENABLE_UNITY_BUILD
    "Enable CMake unity/jumbo builds"
    OFF
)

option(ENABLE_COMPILER_CACHE
    "Use ccache or sccache when available"
    ON
)

option(BUILD_TRACY
    "Enable Tracy instrumentation"
    ON
)

# ------------------------------------------------------------------------------
# Build speed
# ------------------------------------------------------------------------------

# Unity builds can make clean builds considerably faster, but are deliberately
# off by default because they make incremental builds less granular.
set(CMAKE_UNITY_BUILD ${ENABLE_UNITY_BUILD})

# Optional compiler cache.
if(ENABLE_COMPILER_CACHE)
    find_program(CCACHE_PROGRAM ccache)
    find_program(SCCACHE_PROGRAM sccache)

    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    elseif(SCCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
        message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    endif()
endif()

# ------------------------------------------------------------------------------
# Debug build: no optimization, debug symbols
#
# GCC / Clang / AppleClang:
# -O0
# -g0
#
# MSVC / clang-cl:
# /Od
# remove /Zi /ZI /RTC*
#
# This intentionally sacrifices debugger information for faster compilation
# and smaller debug objects.
# ------------------------------------------------------------------------------
if(MSVC)
    # Remove MSVC debug-info/runtime-check flags that CMake may put into
    # CMAKE_CXX_FLAGS_DEBUG.
    string(REGEX REPLACE "/Zi" ""
        CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}"
    )

    string(REGEX REPLACE "/RTC[0-9a-zA-Z]*" ""
        CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}"
    )

    # Avoid inheriting linker debug info settings where present.
    string(REGEX REPLACE "/DEBUG(:[A-Z]+)?" ""
        CMAKE_EXE_LINKER_FLAGS_DEBUG
        "${CMAKE_EXE_LINKER_FLAGS_DEBUG}"
    )

    string(REGEX REPLACE "/DEBUG(:[A-Z]+)?" ""
        CMAKE_SHARED_LINKER_FLAGS_DEBUG
        "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}"
    )

    add_compile_options(
        "$<$<CONFIG:Debug>:/Od>"
    )

else()
    # GCC / Clang / AppleClang.
    #
    # -O0 wins over the normal Debug -Og/-O0 setting.
    add_compile_options(
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Debug>:-g>"
    )

    add_link_options(
        "$<$<CONFIG:Debug>:-g>"
    )
endif()

# No expensive interprocedural optimization in Debug.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# ------------------------------------------------------------------------------
# Local Vulkan SDK
# ------------------------------------------------------------------------------
set(VULKAN_SDK_ROOT
    "$ENV{VULKAN_SDK}"
    CACHE PATH
    "Root directory of the Vulkan SDK"
)

if(NOT VULKAN_SDK_ROOT)
    message(FATAL_ERROR
        "VULKAN_SDK is not set. "
        "Set VULKAN_SDK or pass -DVULKAN_SDK_ROOT=/path/to/VulkanSDK"
    )
endif()

if(NOT EXISTS "${VULKAN_SDK_ROOT}")
    message(FATAL_ERROR
        "VULKAN_SDK_ROOT does not exist: ${VULKAN_SDK_ROOT}"
    )
endif()

message(STATUS "Vulkan SDK: ${VULKAN_SDK_ROOT}")

# Let CMake's FindVulkan use the local SDK first.
list(PREPEND CMAKE_PREFIX_PATH
    "${VULKAN_SDK_ROOT}"
    "${VULKAN_SDK_ROOT}/lib"
)

find_package(Vulkan REQUIRED)

# ------------------------------------------------------------------------------
# GLM
#
# GLM is vendored as a git submodule under extern/glm. It is header-only, so
# expose it through GLM's CMake target.
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/glm/CMakeLists.txt")
    message(FATAL_ERROR "Missing extern/glm")
endif()

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/glm"
    "${CMAKE_BINARY_DIR}/extern/glm"
    EXCLUDE_FROM_ALL
)

# ------------------------------------------------------------------------------
# nlohmann/json
#
# Header-only JSON library. Vendored under extern/json.
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/json/single_include/nlohmann/json.hpp")
    message(FATAL_ERROR "Missing extern/json")
endif()

add_library(nlohmann_json INTERFACE)

target_include_directories(nlohmann_json
    INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/json/single_include"
)

# ------------------------------------------------------------------------------
# Vulkan SDK: VMA
#
# VMA is distributed as a single-header library and normally does not require
# linking a separate binary library.
# ------------------------------------------------------------------------------
find_path(VMA_INCLUDE_DIR
    NAMES vma/vk_mem_alloc.h
    PATHS
    "${VULKAN_SDK_ROOT}/include"
    REQUIRED
)

add_library(vulkan_sdk_vma INTERFACE)
target_include_directories(vulkan_sdk_vma
    INTERFACE
    "${VMA_INCLUDE_DIR}"
)

# ------------------------------------------------------------------------------
# Vulkan SDK: Volk
#
# Prefer an installed CMake package if one exists. Otherwise locate the SDK
# headers + library directly.
# ------------------------------------------------------------------------------
set(VOLK_TARGET "")

find_package(volk CONFIG QUIET
    PATHS "${VULKAN_SDK_ROOT}"
)

if(TARGET volk::volk)
    set(VOLK_TARGET volk::volk)
elseif(TARGET volk)
    set(VOLK_TARGET volk)
else()
    find_path(VOLK_INCLUDE_DIR
        NAMES volk/volk.h
        PATHS
        "${VULKAN_SDK_ROOT}/include"
        REQUIRED
    )

    find_library(VOLK_LIBRARY
        NAMES volk
        PATHS
        "${VULKAN_SDK_ROOT}/lib"
        REQUIRED
    )

    add_library(vulkan_sdk_volk UNKNOWN IMPORTED)
    set_target_properties(vulkan_sdk_volk PROPERTIES
        IMPORTED_LOCATION "${VOLK_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${VOLK_INCLUDE_DIR}"
    )

    set(VOLK_TARGET vulkan_sdk_volk)
endif()

# Platform definitions required by Vulkan/Volk headers.
if(WIN32)
    set(VULKAN_PLATFORM_DEFINE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    set(VULKAN_PLATFORM_DEFINE VK_USE_PLATFORM_MACOS_MVK)
endif()

# ------------------------------------------------------------------------------
# External dependencies
# ------------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/CMakeLists.txt")
    message(FATAL_ERROR "Missing extern/glfw")
endif()

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw"
    "${CMAKE_BINARY_DIR}/extern/glfw"
    EXCLUDE_FROM_ALL
)

# ------------------------------------------------------------------------------
# Slang
#
# Reflection is part of the normal Slang API exposed by slang::slang; there is
# no separate reflection library to link.
#
# Disable targets/features that this executable does not need. This can make
# configuration/builds substantially lighter.
# ------------------------------------------------------------------------------
set(SLANG_ENABLE_PCH ON CACHE BOOL "" FORCE)

set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGRT OFF CACHE BOOL "" FORCE)

set(SLANG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "" FORCE)

set(SLANG_ENABLE_GFX OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "" FORCE)

set(SLANG_ENABLE_DXIL OFF CACHE BOOL "" FORCE)

# We do not want development/test behavior in the embedded standard modules.
set(SLANG_STANDARD_MODULE_DEVELOP_BUILD OFF CACHE BOOL "" FORCE)

# Avoid Slang's own sccache integration here. We optionally configure the
# compiler launcher for the complete project above.
set(SLANG_USE_SCCACHE OFF CACHE BOOL "" FORCE)

set(SLANG_ENABLE_SPLIT_DEBUG_INFO OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/slang/CMakeLists.txt")
    message(FATAL_ERROR "Missing extern/slang")
endif()

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/slang"
    "${CMAKE_BINARY_DIR}/extern/slang"
    EXCLUDE_FROM_ALL
)

# ------------------------------------------------------------------------------
# Tracy
# ------------------------------------------------------------------------------
set(TRACY_ENABLE
    ${BUILD_TRACY}
    CACHE BOOL
    "Enable Tracy profiling"
    FORCE
)

set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_LTO OFF CACHE BOOL "" FORCE)

set(TRACY_Fortran OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/tracy/CMakeLists.txt")
    message(FATAL_ERROR "Missing extern/tracy")
endif()

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/tracy"
    "${CMAKE_BINARY_DIR}/extern/tracy"
    EXCLUDE_FROM_ALL
)

# ------------------------------------------------------------------------------
# Project sources
#
# CONFIGURE_DEPENDS means adding/removing files under src/ or include/ causes
# CMake to re-run automatically.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE APP_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
)

file(GLOB_RECURSE APP_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.inl"
)

if(NOT APP_SOURCES)
    message(FATAL_ERROR
        "No C++ source files found under ${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
endif()

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PREFIX "src"
    FILES ${APP_SOURCES}
)

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PREFIX "include"
    FILES ${APP_HEADERS}
)

add_executable(${APP_NAME}
    ${APP_SOURCES}
    ${APP_HEADERS}
)

option(ENABLE_CLANG_TIDY
    "Run clang-tidy on the application target"
    OFF
)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE
        NAMES clang-tidy
        REQUIRED
    )

    message(STATUS "clang-tidy: ${CLANG_TIDY_EXECUTABLE}")

    set_target_properties(${APP_NAME}
        PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}"
    )
endif()

option(ENABLE_ASAN
    "Enable AddressSanitizer"
    OFF
)

option(ENABLE_UBSAN
    "Enable UndefinedBehaviorSanitizer"
    OFF
)

if(ENABLE_ASAN OR ENABLE_UBSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR
            "Sanitizers currently require GCC, Clang, or AppleClang."
        )
    endif()

    set(SANITIZERS "")

    if(ENABLE_ASAN)
        list(APPEND SANITIZERS "address")
    endif()

    if(ENABLE_UBSAN)
        list(APPEND SANITIZERS "undefined")
    endif()

    list(JOIN SANITIZERS "," SANITIZER_LIST)

    target_compile_options(${APP_NAME}
        PRIVATE
        -fsanitize=${SANITIZER_LIST}
        -fno-omit-frame-pointer
        -g
        -O1
    )

    target_link_options(${APP_NAME}
        PRIVATE
        -fsanitize=${SANITIZER_LIST}
        -g
    )
endif()

# ------------------------------------------------------------------------------
# Include directories
# ------------------------------------------------------------------------------
target_include_directories(${APP_NAME}
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ------------------------------------------------------------------------------
# Platform defines
# ------------------------------------------------------------------------------
if(VULKAN_PLATFORM_DEFINE)
    target_compile_definitions(${APP_NAME}
        PRIVATE
        ${VULKAN_PLATFORM_DEFINE}
    )
endif()

# ------------------------------------------------------------------------------
# Build configuration defines
#
# APP_DEBUG is defined for Debug builds.
# APP_RELEASE is defined for Release builds.
# ------------------------------------------------------------------------------
target_compile_definitions(${APP_NAME}
    PRIVATE
    $<$<CONFIG:Debug>:APP_DEBUG>
    $<$<CONFIG:Release>:APP_RELEASE>
)

# GLFW has its own Vulkan support and doesn't need to pull in OpenGL headers.
target_compile_definitions(${APP_NAME}
    PRIVATE
    GLFW_INCLUDE_NONE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    VK_NO_PROTOTYPES
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
target_link_libraries(${APP_NAME}
    PRIVATE
    glfw
    slang
    glm::glm
    nlohmann_json
    vulkan_sdk_vma
    ${VOLK_TARGET}
)

if(BUILD_TRACY)
    target_link_libraries(${APP_NAME}
        PRIVATE
        Tracy::TracyClient
    )
endif()

# ------------------------------------------------------------------------------
# Precompiled header
#
# Keeping the PCH here avoids adding another source file. These are the headers
# that are typically expensive and/or widely shared throughout a Vulkan engine.
#
# CMake generates the PCH automatically for the target.
# ------------------------------------------------------------------------------
if(ENABLE_PCH)
    target_precompile_headers(${APP_NAME}
        PRIVATE
        <algorithm>
        <array>
        <cassert>
        <cstdint>
        <cstring>
        <functional>
        <memory>
        <optional>
        <span>
        <string>
        <string_view>
        <tuple>
        <unordered_map>
        <unordered_set>
        <utility>
        <variant>
        <vector>
        <GLFW/glfw3.h>
        <glm/glm.hpp>
        <slang.h>
        <vma/vk_mem_alloc.h>
        <volk/volk.h>
        <tracy/Tracy.hpp>
    )
endif()

# ------------------------------------------------------------------------------
# Compiler-specific settings
# ------------------------------------------------------------------------------
if(MSVC)
    target_compile_options(${APP_NAME}
        PRIVATE
        /permissive-
        /W4
        /EHsc
        /MP
    )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(${APP_NAME}
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${APP_NAME}
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# ------------------------------------------------------------------------------
# IDE niceties
# ------------------------------------------------------------------------------
set_target_properties(${APP_NAME} PROPERTIES
    CXX_EXTENSIONS OFF
)

if(WIN32)
    # Keep the standard console entry point. Change to WIN32 here if you want
    # a GUI-subsystem executable with no console window.
    set_target_properties(${APP_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

# ------------------------------------------------------------------------------
# Build summary
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "==========================================")
message(STATUS " ${APP_NAME}")
message(STATUS "==========================================")
message(STATUS " Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ standard:    C++23")
message(STATUS " PCH:             ${ENABLE_PCH}")
message(STATUS " Unity build:     ${ENABLE_UNITY_BUILD}")
message(STATUS " Tracy:            ${BUILD_TRACY}")
message(STATUS " Vulkan SDK:      ${VULKAN_SDK_ROOT}")
message(STATUS "==========================================")
message(STATUS "")
