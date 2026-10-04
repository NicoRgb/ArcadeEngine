cmake_minimum_required(VERSION 3.25)

project(ArcadeEngine VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(ARCADE_BUILD_EDITOR "Build ArcadeEditor and its Dear ImGui dependencies" ON)
option(ARCADE_ENABLE_TRACY "Enable Tracy profiling in ArcadeEditor only" OFF)
option(ENABLE_PCH "Use precompiled headers for project targets" OFF)
option(ENABLE_UNITY_BUILD "Enable CMake unity builds for project targets" OFF)
option(ENABLE_COMPILER_CACHE "Use ccache or sccache when available" ON)
option(ENABLE_CLANG_TIDY "Run clang-tidy on project targets" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_COMPILER_CACHE)
    find_program(CCACHE_PROGRAM ccache)
    find_program(SCCACHE_PROGRAM sccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    elseif(SCCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    endif()
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()

if(ENABLE_ASAN OR ENABLE_UBSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "AddressSanitizer and UndefinedBehaviorSanitizer require GCC or Clang.")
    endif()
    set(ARCADE_SANITIZERS "")
    if(ENABLE_ASAN)
        list(APPEND ARCADE_SANITIZERS address)
    endif()
    if(ENABLE_UBSAN)
        list(APPEND ARCADE_SANITIZERS undefined)
    endif()
    list(JOIN ARCADE_SANITIZERS "," ARCADE_SANITIZER_LIST)
endif()

function(arcade_configure_target target)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        UNITY_BUILD ${ENABLE_UNITY_BUILD}
    )

    if(ENABLE_PCH)
        target_precompile_headers(${target} PRIVATE
            <algorithm>
            <array>
            <cstdint>
            <memory>
            <optional>
            <string>
            <string_view>
            <utility>
            <vector>
        )
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /Zc:preprocessor)
        if(ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(ENABLE_ASAN OR ENABLE_UBSAN)
            target_compile_options(${target} PRIVATE
                "-fsanitize=${ARCADE_SANITIZER_LIST}"
                -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE "-fsanitize=${ARCADE_SANITIZER_LIST}")
        endif()
    endif()

    if(ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}"
        )
    endif()
endfunction()

function(arcade_require_submodule path sentinel)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${path}/${sentinel}")
        message(FATAL_ERROR
            "Missing dependency submodule '${path}'. Initialize submodules recursively "
            "with: git submodule update --init --recursive"
        )
    endif()
endfunction()

arcade_require_submodule(extern/glfw CMakeLists.txt)
arcade_require_submodule(extern/glm CMakeLists.txt)
arcade_require_submodule(extern/json single_include/nlohmann/json.hpp)
arcade_require_submodule(extern/nvrhi CMakeLists.txt)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# The engine is itself a shared library and the editor's ImGui platform
# backend also calls GLFW. Sharing one GLFW instance avoids crossing GLFW's
# internal window state between separate static copies.
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
add_subdirectory(extern/glfw EXCLUDE_FROM_ALL)
add_subdirectory(extern/glm EXCLUDE_FROM_ALL)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/json/single_include"
)

set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(NVRHI_WITH_VULKAN OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX12 ON CACHE BOOL "" FORCE)
    set(ARCADE_NVRHI_BACKEND nvrhi_d3d12)
else()
    set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
    set(NVRHI_FETCH_VULKAN_HEADERS ON CACHE BOOL "" FORCE)
    set(ARCADE_NVRHI_BACKEND nvrhi_vk)
endif()
add_subdirectory(extern/nvrhi EXCLUDE_FROM_ALL)

if(NOT WIN32)
    # Vulkan remains a platform loader for NVRHI's Vulkan backend and native
    # device bootstrap. VMA and Volk are not engine targets.
    find_package(Vulkan REQUIRED)
endif()

add_subdirectory(projects/ArcadeEngine)
add_subdirectory(projects/ArcadeRuntime)

if(ARCADE_BUILD_EDITOR)
    arcade_require_submodule(extern/imgui imgui.cpp)
    add_subdirectory(projects/ArcadeEditor)

    if(ARCADE_ENABLE_TRACY)
        arcade_require_submodule(extern/tracy CMakeLists.txt)
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_STATIC ON CACHE BOOL "" FORCE)
        set(TRACY_LTO OFF CACHE BOOL "" FORCE)
        set(TRACY_NO_EXIT OFF CACHE BOOL "" FORCE)
        add_subdirectory(extern/tracy EXCLUDE_FROM_ALL)
        target_link_libraries(ArcadeEditor PRIVATE Tracy::TracyClient)
        target_compile_definitions(ArcadeEditor PRIVATE ARCADE_ENABLE_TRACY)
    endif()
endif()

if(BUILD_TESTING)
    arcade_require_submodule(extern/catch2 CMakeLists.txt)
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    add_subdirectory(extern/catch2 EXCLUDE_FROM_ALL)

    add_subdirectory(projects/ArcadeEngineTests)
endif()

message(STATUS "ArcadeEngine shared library: enabled")
message(STATUS "ArcadeEditor: ${ARCADE_BUILD_EDITOR}")
message(STATUS "ArcadeRuntime: enabled")
message(STATUS "Tracy in ArcadeEditor: ${ARCADE_ENABLE_TRACY}")
message(STATUS "Engine tests: ${BUILD_TESTING}")
message(STATUS "NVRHI backend: ${ARCADE_NVRHI_BACKEND}")
