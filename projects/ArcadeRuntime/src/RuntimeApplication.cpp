#include "RuntimeApplication.hpp"

#include "Application/Application.hpp"
#include "Assets/AssetManager.hpp"

#include <system_error>
#include <utility>

#ifndef ARCADE_RUNTIME_CONTENT_DIR
#define ARCADE_RUNTIME_CONTENT_DIR "content"
#endif

RuntimeApplication::RuntimeApplication(std::filesystem::path assetDirectory)
    : m_AssetDirectory(std::move(assetDirectory))
{
}

int RuntimeApplication::Run()
{
    Application application;

    std::error_code error;
    const auto workingDirectory = std::filesystem::current_path(error);
    if (error)
    {
        application.GetEngineLogger().LogError("Could not resolve runtime working directory: {}",
                                               error.message());
        return 1;
    }

    const auto defaultAssetDirectory = std::filesystem::path(ARCADE_RUNTIME_CONTENT_DIR);
    const auto requestedDirectory =
        m_AssetDirectory.empty() ? defaultAssetDirectory : m_AssetDirectory;
    const auto assetDirectory = std::filesystem::weakly_canonical(
        requestedDirectory.is_absolute() ? requestedDirectory
                                         : workingDirectory / requestedDirectory,
        error);
    if (error || !std::filesystem::is_directory(assetDirectory, error) || error)
    {
        application.GetEngineLogger().LogError("Runtime asset directory is unavailable: {}",
                                               requestedDirectory.string());
        return 1;
    }

    auto indexed = application.GetAssetManager().IndexAssets(assetDirectory);
    if (!indexed)
    {
        application.GetEngineLogger().LogError("Could not index runtime assets at {}: {}",
                                               assetDirectory.string(),
                                               ErrorMessage(indexed.error()));
        return 1;
    }

    application.GetEngineLogger().LogInfo("Runtime ready with {} registered assets from {}",
                                          application.GetAssetManager().IndexedAssetCount(),
                                          assetDirectory.string());
    return 0;
}
