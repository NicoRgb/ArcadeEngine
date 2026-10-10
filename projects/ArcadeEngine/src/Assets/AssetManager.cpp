#include "Assets/AssetManager.hpp"
#include "Application/Application.hpp"
#include "Core/Result.hpp"
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
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
    return IndexAssetsInternal(directory, false);
}

Result<void> AssetManager::IndexAssetsInternal(const std::filesystem::path& directory, bool rescan)
{
    LOG_INFO("Indexing asset directory {}", directory.string());

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return MAKE_ERROR(Error::InvalidArgument);
    }

    const auto normalizedDirectory = std::filesystem::weakly_canonical(directory, error);
    if (error)
    {
        return MAKE_ERROR_EXT(Error::IoFailure, "Could not resolve asset directory.", error);
    }

    std::filesystem::recursive_directory_iterator it(normalizedDirectory, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error)
    {
        return MAKE_ERROR(Error::InvalidArgument);
    }

    std::unordered_set<AssetKey, AssetKeyHash> foundAssets;
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
                return MAKE_ERROR_EXT(Error::IoFailure, "Could not resolve asset path.", error);
            }
            const auto relativePath = absolutePath.lexically_relative(normalizedDirectory);
            if (relativePath.empty() || *relativePath.begin() == "..")
            {
                return MAKE_ERROR(Error::InvalidArgument);
            }

            const std::string assetKey = relativePath.generic_string();
            auto res = IndexAsset(absolutePath, assetKey, rescan);
            if (!res)
            {
                return FORWARD_ERROR(res);
            }
            if (rescan && m_ByKey.contains(AssetKey{.Name = assetKey}))
            {
                foundAssets.insert(AssetKey{.Name = assetKey});
            }
        }

        it.increment(error);
        if (error)
        {
            return MAKE_ERROR(Error::InvalidArgument);
        }
    }

    if (rescan)
    {
        auto& resourceManager = Application::Get().GetResourceManager();
        for (auto assetIterator = m_ByKey.begin(); assetIterator != m_ByKey.end();)
        {
            if (foundAssets.contains(assetIterator->first))
            {
                ++assetIterator;
                continue;
            }

            const auto asset = m_Assets.find(assetIterator->second);
            if (asset != m_Assets.end())
            {
                auto unloaded = resourceManager.Unload(asset->second.Id());
                if (!unloaded && unloaded.error().Code != Error::NotFound)
                {
                    return FORWARD_ERROR(unloaded);
                }
                m_Assets.erase(asset);
            }
            assetIterator = m_ByKey.erase(assetIterator);
        }
    }

    LOG_INFO("Indexed {} registered assets", IndexedAssetCount());

    return {};
}

Result<void> AssetManager::RescanAssets(const std::filesystem::path& directory)
{
    return IndexAssetsInternal(directory, true);
}

Result<void> AssetManager::IndexAsset(const std::filesystem::path& path, std::string_view assetKey,
                                      bool rescan)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return MAKE_ERROR(Error::InvalidArgument);
    }

    const std::string extension = path.extension().string();
    const AssetKey key{.Name = std::string(assetKey)};
    const auto existingKey = m_ByKey.find(key);
    if (existingKey != m_ByKey.end())
    {
        if (!rescan)
        {
            return MAKE_ERROR(Error::InvalidArgument);
        }

        const auto existingAsset = m_Assets.find(existingKey->second);
        if (existingAsset != m_Assets.end() && existingAsset->second->GetPath() == path)
        {
            auto reloadResult = existingAsset->second->Reload();
            if (!reloadResult)
            {
                return FORWARD_ERROR(reloadResult);
            }
            return {};
        }

        if (existingAsset != m_Assets.end())
        {
            auto unloaded = Application::Get().GetResourceManager().Unload(existingAsset->second.Id());
            if (!unloaded && unloaded.error().Code != Error::NotFound)
            {
                return FORWARD_ERROR(unloaded);
            }
            m_Assets.erase(existingAsset);
        }
        m_ByKey.erase(existingKey);
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
                return MAKE_ERROR(Error::InvalidArgument);
            }
            matchingAssetType = &assetType;
            break;
        }
    }

    if (matchingAssetType == nullptr)
    {
        return {};
    }

    std::filesystem::path configPath = path.string() + ".asset";
    Result<json> configResult = GetOrCreateConfig(matchingAssetType->Name, configPath);
    if (!configResult)
    {
        return FORWARD_ERROR(configResult);
    }

    const json& config = configResult.value();
    auto assetResult = matchingAssetType->CreateFunc(path, assetKey, config);
    if (!assetResult)
    {
        return FORWARD_ERROR(assetResult);
    }
    Resource<Asset> asset = std::move(*assetResult);
    if (!asset)
    {
        return MAKE_ERROR_EXT(Error::Internal,
                              "Asset factory returned an empty resource: " + path.string(), {});
    }

    auto uuid = config.value<uint64_t>("UUID", 0);
    AssetId id(uuid);

    while (m_Assets.contains(id))
    {
        LOG_WARN("UUID collision");
        id = AssetId{};
    }

    auto metadataUpdated = asset->SetMetadataField("UUID", id.UUID);
    if (!metadataUpdated)
    {
        return FORWARD_ERROR(metadataUpdated);
    }

    auto loadResult = asset->LoadIfNeeded();
    if (!loadResult)
    {
        return FORWARD_ERROR(loadResult);
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
        return MAKE_ERROR(Error::NotFound);
    }

    const auto asset = m_Assets.find(it->second);
    if (asset == m_Assets.end())
    {
        return MAKE_ERROR(Error::NotFound);
    }

    return asset->second;
}

Result<json> AssetManager::GetOrCreateConfig(std::string_view assetType,
                                             const std::filesystem::path& configPath)
{
    json config;
    if (std::filesystem::exists(configPath))
    {
        std::ifstream configFile(configPath);
        if (!configFile.is_open())
        {
            return MAKE_ERROR(Error::IoFailure);
        }

        config = json::parse(configFile);
        configFile.close();

        if (config["type"].get<std::string>() == assetType)
        {
            return config;
        }

        LOG_WARN("Config has incorrect asset type... Regenerating {}",
                 configPath.filename().string());
    }

    config = {};
    config["type"] = assetType;

    std::ofstream configFile(configPath);
    if (!configFile.is_open())
    {
        return MAKE_ERROR(Error::IoFailure);
    }

    configFile << config.dump(4);
    configFile.close();

    LOG_INFO("Dumping {}", configPath.string());

    return config;
}
