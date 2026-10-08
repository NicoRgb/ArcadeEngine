#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Core/Export.hpp"
#include "Core/Resource.hpp"
#include "Core/Result.hpp"

#include "Application/Application.hpp"

using nlohmann::json;

struct AssetId
{
    uint64_t UUID;

    AssetId() { GenerateUUID(); }

    AssetId(uint64_t uuid) : UUID(uuid)
    {
        if (uuid == 0)
        {
            GenerateUUID();
        }
    }

    friend constexpr bool operator==(AssetId, AssetId) = default;
    explicit constexpr operator bool() const noexcept { return UUID != 0; }

private:
    void GenerateUUID()
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> distrib(std::numeric_limits<uint64_t>::min(),
                                                        std::numeric_limits<uint64_t>::max());

        UUID = distrib(gen);
    }
};

struct AssetIdHash
{
    size_t operator()(AssetId id) const noexcept { return std::hash<uint64_t>{}(id.UUID); }
};

struct AssetKey
{
    std::string Name;
    friend bool operator==(const AssetKey&, const AssetKey&) = default;
};

struct AssetKeyHash
{
    size_t operator()(const AssetKey& key) const noexcept
    {
        return std::hash<std::string_view>{}(key.Name);
    }
};

class Asset
{
public:
    Asset(std::filesystem::path path, json metadata)
        : m_Path(std::move(path)), m_Metadata(std::move(metadata))
    {
    }

    ARCADE_ENGINE_API virtual ~Asset();
    virtual void Load() = 0;

    [[nodiscard]]
    const std::filesystem::path& GetPath() const
    {
        return m_Path;
    }

    [[nodiscard]]
    const json& GetMetadata() const
    {
        return m_Metadata;
    }

    [[nodiscard]] ARCADE_ENGINE_API Result<void> SetMetadataField(std::string_view key, json value);

protected:
    std::filesystem::path m_Path;
    json m_Metadata;

private:
    [[nodiscard]] Result<void> WriteMetadata(const json& metadata) const;
};

class AssetRegisteree
{
public:
    ARCADE_ENGINE_API AssetRegisteree(
        const char* name,
        const std::function<Result<Resource<Asset>>(const std::filesystem::path& path,
                                                    std::string_view assetKey, json metadata)>&
            createFunc,
        const std::vector<const char*>& extensions);
};

#define REGISTER_ASSET_TYPE(className, ...)                                                        \
    static AssetRegisteree g_Registeree{                                                           \
        #className,                                                                                \
        [](const std::filesystem::path& path, std::string_view assetKey,                           \
           json metadata) -> Result<Resource<Asset>>                                               \
        {                                                                                          \
            auto result = Application::Get().GetResourceManager().CreateShared<className>(         \
                assetKey, path, std::move(metadata));                                              \
            if (!result)                                                                           \
            {                                                                                      \
                return FORWARD_ERROR(result);                                                      \
            }                                                                                      \
            return Resource<Asset>(*result);                                                       \
        },                                                                                         \
        {__VA_ARGS__}};

class AssetManager
{
public:
    AssetManager() = default;
    ~AssetManager() = default;

    ARCADE_ENGINE_API Result<void> IndexAssets(const std::filesystem::path& directory);
    ARCADE_ENGINE_API Result<void> RescanAssets(const std::filesystem::path& directory);
    [[nodiscard]] ARCADE_ENGINE_API Result<Resource<Asset>> FindAsset(std::string_view name) const;
    [[nodiscard]] size_t IndexedAssetCount() const noexcept { return m_Assets.size(); }

private:
    Result<void> IndexAsset(const std::filesystem::path& path, std::string_view assetKey);
    static Result<json> GetOrCreateConfig(std::string_view assetType,
                                          const std::filesystem::path& configPath);

    std::unordered_map<AssetId, Resource<Asset>, AssetIdHash> m_Assets;
    std::unordered_map<AssetKey, AssetId, AssetKeyHash> m_ByKey;
};
