add_library(ArcadeImGui STATIC
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_draw.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_tables.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_widgets.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_demo.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/backends/imgui_impl_glfw.cpp"
)
target_include_directories(ArcadeImGui PUBLIC
    "${PROJECT_SOURCE_DIR}/extern/imgui"
    "${PROJECT_SOURCE_DIR}/extern/imgui/backends"
)
target_compile_definitions(ArcadeImGui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(ArcadeImGui PUBLIC glfw)

add_executable(ArcadeEditor src/Main.cpp)
target_link_libraries(ArcadeEditor PRIVATE ArcadeEngine ArcadeImGui)
arcade_configure_target(ArcadeEditor)
