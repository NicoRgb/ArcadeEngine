add_executable(ArcadeRuntime
    src/Main.cpp
    src/RuntimeApplication.cpp
)
target_include_directories(ArcadeRuntime PRIVATE include)
target_link_libraries(ArcadeRuntime PRIVATE ArcadeEngine)
target_compile_definitions(ArcadeRuntime PRIVATE
    ARCADE_RUNTIME_CONTENT_DIR="$<TARGET_FILE_DIR:ArcadeRuntime>/content"
)
arcade_configure_target(ArcadeRuntime)
add_custom_command(TARGET ArcadeRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content"
        "$<TARGET_FILE_DIR:ArcadeRuntime>/content"
    VERBATIM
)
