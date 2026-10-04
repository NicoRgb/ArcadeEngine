add_executable(ArcadeEngineTests
    src/CoreTests.cpp
    src/AssetManagerTests.cpp
)
target_link_libraries(ArcadeEngineTests PRIVATE ArcadeEngine Catch2::Catch2WithMain)
arcade_configure_target(ArcadeEngineTests)
add_test(NAME ArcadeEngineTests COMMAND ArcadeEngineTests)
