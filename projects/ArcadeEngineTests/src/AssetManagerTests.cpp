#include "Application/Application.hpp"
#include "Assets/AssetManager.hpp"

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
    [](const std::filesystem::path& path, std::string_view assetKey, json metadata) {
        auto result = Application::Get().GetResourceManager().CreateShared<TestAsset>(
            assetKey, path, std::move(metadata));
        if (!result)
        {
            return Result<Resource<Asset>>(MakeError(result.error()));
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
                 ("arcade-engine-test-" + std::to_string(
                     std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                  std::to_string(counter++));
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

    AssetManager manager;
    REQUIRE(manager.IndexAssets(root));
    CHECK(manager.IndexedAssetCount() == 2);
    CHECK(manager.FindAsset("first/same.arcade-test"));
    CHECK(manager.FindAsset("second/same.arcade-test"));
    CHECK_FALSE(manager.FindAsset("same.arcade-test"));
    CHECK_FALSE(manager.IndexAssets(root));
}

TEST_CASE("Asset indexing rejects non-directories", "[assets]")
{
    TemporaryDirectory temporaryDirectory;
    AssetManager manager;
    CHECK_FALSE(manager.IndexAssets(temporaryDirectory.Path() / "missing"));
}
