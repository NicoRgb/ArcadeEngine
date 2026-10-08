#include "Assets/AssetManager.hpp"
#include "Application/Application.hpp"
#include "Core/Result.hpp"

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <vector>

class AssetRegistry
{
public:
    struct AssetDesc
    {
        std::string Name;
        std::function<Result<Resource<Asset>>(const std::filesystem::path&, std::string_view, json)>
            CreateFunc;
        std::vector<std::string> Extensions;
    };

    void Register(const char* name,
                  const std::function<Result<Resource<Asset>>(const std::filesystem::path& path,
                                                              std::string_view assetKey,
                                                              json metadata)>& createFunc,
                  const std::vector<const char*>& extensions)
    {
        AssetDesc desc{.Name = std::string(name), .CreateFunc = createFunc, .Extensions = {}};
        desc.Extensions.reserve(extensions.size());
        for (const auto* extension : extensions)
        {
            if (extension == nullptr || *extension == '\0')
            {
                throw std::invalid_argument("Asset extensions must not be empty.");
            }
            desc.Extensions.emplace_back(extension);
        }
        m_AssetDescs.push_back(std::move(desc));
    }

private:
    std::vector<AssetDesc> m_AssetDescs;

    friend class AssetManager;
};

AssetRegistry& GetAssetRegistry()
{
    static AssetRegistry registry;
    return registry;
}

AssetRegisteree::AssetRegisteree(
    const char* name,
    const std::function<Result<Resource<Asset>>(
        const std::filesystem::path& path, std::string_view assetKey, json metadata)>& createFunc,
    const std::vector<const char*>& extensions)
{
    GetAssetRegistry().Register(name, createFunc, extensions);
}

Result<void> AssetManager::IndexAssets(const std::filesystem::path& directory)
{
    LOG_INFO("Indexing asset directory {}", directory.string());

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return MakeError(Error::InvalidArgument);
    }

    const auto normalizedDirectory = std::filesystem::weakly_canonical(directory, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Could not resolve asset directory.", error);
    }

    std::filesystem::recursive_directory_iterator it(normalizedDirectory, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error)
    {
        return MakeError(Error::InvalidArgument);
    }

    while (it != end)
    {
        const auto dirEntry = *it;
        if (!dirEntry.is_regular_file(error) || error)
        {
            error.clear();
        }
        else if (dirEntry.path().extension() != ".asset")
        {
            const auto absolutePath = std::filesystem::weakly_canonical(dirEntry.path(), error);
            if (error)
            {
                return MakeError(Error::IoFailure, "Could not resolve asset path.", error);
            }
            const auto relativePath = absolutePath.lexically_relative(normalizedDirectory);
            if (relativePath.empty() || *relativePath.begin() == "..")
            {
                return MakeError(Error::InvalidArgument);
            }

            auto res = IndexAsset(absolutePath, relativePath.generic_string());
            if (!res)
            {
                return MakeError(res.error());
            }
        }

        it.increment(error);
        if (error)
        {
            return MakeError(Error::InvalidArgument);
        }
    }

    LOG_INFO("Indexed {} registered assets", IndexedAssetCount());

    return {};
}

Result<void> AssetManager::RescanAssets(const std::filesystem::path& directory)
{
    auto& resourceManager = Application::Get().GetResourceManager();
    std::vector<ResourceId> oldResources;
    oldResources.reserve(m_Assets.size());
    for (const auto& [id, asset] : m_Assets)
    {
        (void)id;
        oldResources.push_back(asset.Id());
    }
    for (const ResourceId id : oldResources)
    {
        auto unloaded = resourceManager.Unload(id);
        if (!unloaded && unloaded.error().Code != Error::NotFound)
        {
            return MakeError(unloaded.error());
        }
    }
    m_Assets.clear();
    m_ByKey.clear();
    return IndexAssets(directory);
}

Result<void> AssetManager::IndexAsset(const std::filesystem::path& path, std::string_view assetKey)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return MakeError(Error::InvalidArgument);
    }

    const std::string extension = path.extension().string();
    const AssetKey key{.Name = std::string(assetKey)};
    if (m_ByKey.contains(key))
    {
        return MakeError(Error::InvalidArgument);
    }

    const AssetRegistry::AssetDesc* matchingAssetType = nullptr;
    for (const auto& assetType : GetAssetRegistry().m_AssetDescs)
    {
        for (const std::string& supportedExtension : assetType.Extensions)
        {
            if (extension != supportedExtension)
            {
                continue;
            }

            if (matchingAssetType != nullptr)
            {
                LOG_ERROR("asset type is ambiguous for extension: {}", extension);
                return MakeError(Error::InvalidArgument);
            }
            matchingAssetType = &assetType;
            break;
        }
    }

    if (matchingAssetType == nullptr)
    {
        return {};
    }

    auto assetResult = matchingAssetType->CreateFunc(path, assetKey, json());
    if (!assetResult)
    {
        return MakeError(assetResult.error());
    }
    Resource<Asset> asset = std::move(*assetResult);
    if (!asset)
        return MakeError(Error::Internal,
                         "Asset factory returned an empty resource: " + path.string());

    AssetId id;
    while (m_Assets.contains(id))
    {
        id = AssetId{};
    }

    m_Assets.emplace(id, std::move(asset));
    m_ByKey.emplace(key, id);

    return {};
}

Result<Resource<Asset>> AssetManager::FindAsset(std::string_view name) const
{
    const auto it = m_ByKey.find(AssetKey{.Name = std::string(name)});
    if (it == m_ByKey.end())
    {
        return MakeError(Error::NotFound);
    }

    const auto asset = m_Assets.find(it->second);
    if (asset == m_Assets.end())
    {
        return MakeError(Error::NotFound);
    }

    return asset->second;
}
