add_library(ArcadeImGui STATIC
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_draw.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_tables.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/imgui_widgets.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/backends/imgui_impl_glfw.cpp"
    "${PROJECT_SOURCE_DIR}/extern/imgui/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(ArcadeImGui PUBLIC
    "${PROJECT_SOURCE_DIR}/extern/imgui"
    "${PROJECT_SOURCE_DIR}/extern/imgui/backends"
)
target_compile_definitions(ArcadeImGui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(ArcadeImGui PUBLIC glfw OpenGL::GL)

add_library(ArcadeEditorCore STATIC
    src/EditorCommand.cpp
    src/EditorDocument.cpp
    src/EditorProject.cpp
    src/EditorLogBuffer.cpp
    src/EditorThumbnail.cpp
    src/EditorAssetWatcher.cpp
)
target_include_directories(ArcadeEditorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/extern/slang/external/stb"
            "${PROJECT_SOURCE_DIR}/extern/watcher/include")
target_link_libraries(ArcadeEditorCore PUBLIC nlohmann_json ArcadeEngine)
arcade_configure_target(ArcadeEditorCore)

add_library(ArcadeEditorUI STATIC
    src/EditorAssetBrowserPanel.cpp
    src/EditorDockspaceBuilder.cpp
    src/EditorPanels.cpp
    src/EditorTheme.cpp
)
target_include_directories(ArcadeEditorUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ArcadeEditorUI PUBLIC ArcadeEditorCore ArcadeImGui nlohmann_json)
arcade_configure_target(ArcadeEditorUI)

add_executable(ArcadeEditor
    src/EditorApplication.cpp
    src/Main.cpp
)
target_link_libraries(ArcadeEditor PRIVATE ArcadeEngine ArcadeEditorUI OpenGL::GL)
target_compile_definitions(ArcadeEditor PRIVATE
    GLFW_INCLUDE_NONE
    ARCADE_EDITOR_ASSET_DIR="$<TARGET_FILE_DIR:ArcadeEditor>/assets"
    ARCADE_EDITOR_CONTENT_DIR="$<TARGET_FILE_DIR:ArcadeEditor>/content"
)
arcade_configure_target(ArcadeEditor)
add_custom_command(TARGET ArcadeEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ArcadeEditor>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content" "$<TARGET_FILE_DIR:ArcadeEditor>/content"
    VERBATIM
)
