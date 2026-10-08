#include "Application/Application.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/ShaderAsset.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <fstream>

namespace
{
class TestAsset final : public Asset
{
public:
    using Asset::Asset;
    void Load() override {}
};

AssetRegisteree g_TestAssetRegistration(
    "TestAsset",
    [](const std::filesystem::path& path, std::string_view assetKey, json metadata)
    {
        auto result = Application::Get().GetResourceManager().CreateShared<TestAsset>(
            assetKey, path, std::move(metadata));
        if (!result)
        {
            return Result<Resource<Asset>>(FORWARD_ERROR(result));
        }
        return Result<Resource<Asset>>(Resource<Asset>(*result));
    },
    {".arcade-test"});

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        static std::atomic_uint64_t counter = 0;
        m_Path = std::filesystem::temp_directory_path() /
                 ("arcade-engine-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(counter++));
        std::filesystem::create_directories(m_Path);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_Path, ignored);
    }

    const std::filesystem::path& Path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};
} // namespace

TEST_CASE("Asset indexing includes ordinary files and keys them by relative path", "[assets]")
{
    Application application;
    TemporaryDirectory temporaryDirectory;
    const auto root = temporaryDirectory.Path();
    std::filesystem::create_directories(root / "first");
    std::filesystem::create_directories(root / "second");
    std::ofstream(root / "first" / "same.arcade-test") << "first";
    std::ofstream(root / "second" / "same.arcade-test") << "second";
    std::ofstream(root / "preview.hlsl") << "float4 main() : SV_Target { return 1; }";

    AssetManager manager;
    REQUIRE(manager.IndexAssets(root));
    CHECK(manager.IndexedAssetCount() == 3);
    CHECK(manager.FindAsset("first/same.arcade-test"));
    CHECK(manager.FindAsset("second/same.arcade-test"));
    auto shader = manager.FindAsset("preview.hlsl");
    REQUIRE(shader);
    auto shaderAsset = shader->DynamicCast<ShaderAsset>();
    REQUIRE(shaderAsset);
    CHECK(shaderAsset->Source().find("SV_Target") != std::string::npos);
    CHECK_FALSE(manager.FindAsset("same.arcade-test"));
    CHECK_FALSE(manager.IndexAssets(root));

    std::filesystem::remove(root / "second" / "same.arcade-test");
    std::ofstream(root / "first" / "new.arcade-test") << "new";
    std::ofstream(root / "preview.hlsl", std::ios::trunc)
        << "float4 main() : SV_Target { return 0; }";
    REQUIRE(manager.RescanAssets(root));
    CHECK(manager.IndexedAssetCount() == 3);
    CHECK(manager.FindAsset("first/same.arcade-test"));
    CHECK(manager.FindAsset("first/new.arcade-test"));
    CHECK_FALSE(manager.FindAsset("second/same.arcade-test"));
    auto rescannedShader = manager.FindAsset("preview.hlsl");
    REQUIRE(rescannedShader);
    auto rescannedShaderAsset = rescannedShader->DynamicCast<ShaderAsset>();
    REQUIRE(rescannedShaderAsset);
    CHECK(rescannedShaderAsset->Source().find("return 0") != std::string::npos);
    CHECK(rescannedShaderAsset->Source().find("return 1") == std::string::npos);
}

TEST_CASE("Asset indexing rejects non-directories", "[assets]")
{
    Application application;
    TemporaryDirectory temporaryDirectory;
    AssetManager manager;
    CHECK_FALSE(manager.IndexAssets(temporaryDirectory.Path() / "missing"));
}
