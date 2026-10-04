#pragma once

#include <filesystem>
#include <functional>
#include <random>
#include <utility>

#include <nlohmann/json.hpp>

#include "Core/Resource.hpp"
#include "Core/Result.hpp"

using namespace nlohmann;

struct AssetId
{
    uint64_t UUID;

    AssetId()
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> distrib(std::numeric_limits<uint64_t>::min(),
                                                        std::numeric_limits<uint64_t>::max());

        UUID = distrib(gen);
    }

    AssetId(uint64_t uuid) : UUID(uuid) {}

    friend constexpr bool operator==(AssetId, AssetId) = default;
    explicit constexpr operator bool() const noexcept { return UUID != 0; }
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

    virtual ~Asset() = default;
    virtual void Load() = 0;

    [[__nodiscard__]]
    const std::filesystem::path& GetPath() const
    {
        return m_Path;
    }

    [[__nodiscard__]]
    const json& GetMetadata() const
    {
        return m_Metadata;
    }

protected:
    std::filesystem::path m_Path;
    json m_Metadata;
};

class AssetRegisteree
{
public:
    AssetRegisteree(
        const std::function<Resource<Asset>(std::filesystem::path path, json metadata)>& createFunc,
        const std::vector<const char*>& extensions);
};

#define REGISTER_ASSET_TYPE(className, ...)                                                        \
    AssetRegisteree g_Registeree{                                                                  \
        [](std::filesystem::path path, json metadata) -> Resource<Asset>                           \
        {                                                                                          \
            return ResultOrThrow(Application::Get().GetResourceManager().CreateShared<className>(  \
                path.filename().string(), path, metadata));                                        \
        },                                                                                         \
        {__VA_ARGS__}};

class AssetManager
{
public:
    AssetManager() = default;
    ~AssetManager() = default;

    Result<void> IndexAssets(const std::filesystem::path& directory);

private:
    Result<void> IndexAsset(const std::filesystem::path& path);

    std::unordered_map<AssetId, Resource<Asset>, AssetIdHash> m_Assets;
    std::unordered_map<AssetKey, AssetId, AssetKeyHash> m_ByKey;
};
