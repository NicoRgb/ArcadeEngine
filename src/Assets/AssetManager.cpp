#include "Assets/AssetManager.hpp"
#include "Application/Application.hpp"

class AssetRegistry
{
public:
    struct AssetDesc
    {
        const std::function<Resource<Asset>(std::filesystem::path path, json metadata)> CreateFunc;
        const std::vector<const char*> Extensions;
    };

    void Register(
        const std::function<Resource<Asset>(std::filesystem::path path, json metadata)>& createFunc,
        const std::vector<const char*>& extensions)
    {
        m_AssetDescs.push_back({.CreateFunc = createFunc, .Extensions = extensions});
    }

private:
    std::vector<AssetDesc> m_AssetDescs;

    friend class AssetManager;
};

AssetRegistry g_AssetRegistry;

AssetRegisteree::AssetRegisteree(
    const std::function<Resource<Asset>(std::filesystem::path path, json metadata)>& createFunc,
    const std::vector<const char*>& extensions)
{
    g_AssetRegistry.Register(createFunc, extensions);
}

Result<void> AssetManager::IndexAssets(const std::filesystem::path& directory)
{
    if (!std::filesystem::exists(directory))
    {
        return MakeError(Error::InvalidArgument);
    }

    for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(directory))
    {
        if (!dirEntry.is_regular_file() || !dirEntry.is_symlink())
        {
            continue;
        }

        // metadata is parsed by the parent asset file
        if (dirEntry.path().extension() == ".asset")
        {
            continue;
        }

        auto res = IndexAsset(dirEntry.path());
        if (!res)
        {
            return MakeError(res.error());
        }
    }

    return {};
}

Result<void> AssetManager::IndexAsset(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        return MakeError(Error::InvalidArgument);
    }

    const std::string extension = path.extension().string();
    Resource<Asset> asset;

    for (const auto& assetType : g_AssetRegistry.m_AssetDescs)
    {
        for (const char* supportedExtension : assetType.Extensions)
        {
            if (extension != supportedExtension)
            {
                continue;
            }

            asset = assetType.CreateFunc(path, json());
            break;
        }
    }

    if (!asset)
    {
        LOG_ERROR("asset type not supported: %s", path.string());
        return {};
    }

    AssetId id;
    m_Assets.emplace(id, asset);
    m_ByKey.emplace(AssetKey{.Name = path.filename().string()}, id);

    return {};
}
