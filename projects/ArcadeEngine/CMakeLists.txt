add_library(ArcadeEngine SHARED
    src/Application/Application.cpp
    src/Application/GraphicsDevice.cpp
    src/Application/Logger.cpp
    src/Application/Window.cpp
    src/Assets/AssetManager.cpp
)

target_compile_definitions(ArcadeEngine PRIVATE ARCADE_ENGINE_BUILD GLFW_INCLUDE_NONE)
target_include_directories(ArcadeEngine PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(ArcadeEngine
    PUBLIC nlohmann_json
    PRIVATE glfw glm::glm nvrhi ${ARCADE_NVRHI_BACKEND}
)

if(WIN32)
    target_link_libraries(ArcadeEngine PRIVATE d3d12 dxgi)
else()
    target_link_libraries(ArcadeEngine PRIVATE Vulkan::Vulkan)
    if(APPLE)
        target_compile_definitions(ArcadeEngine PRIVATE VK_USE_PLATFORM_METAL_EXT)
    elseif(UNIX)
        target_compile_definitions(ArcadeEngine PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    endif()
endif()

arcade_configure_target(ArcadeEngine)
