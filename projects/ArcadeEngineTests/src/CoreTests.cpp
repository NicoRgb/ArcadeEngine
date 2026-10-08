#include "Application/Application.hpp"
#include "Core/Resource.hpp"
#include "Core/Result.hpp"

#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <vector>

namespace
{
class CollectingSink final : public LogSink
{
public:
    void ReceiveMessage(LogLevel level, std::string_view msg) override
    {
        std::scoped_lock lock(mutex);
        entries.emplace_back(level, msg);
    }
    std::mutex mutex;
    std::vector<std::pair<LogLevel, std::string>> entries;
};
} // namespace

TEST_CASE("ResultOrThrow returns successful values", "[core][result]")
{
    CHECK(ResultOrThrow(Result<int>{42}) == 42);
    CHECK_NOTHROW(ResultOrThrow(Result<void>{}));
    CHECK_FALSE(Result<int>{MAKE_ERROR(Error::NotFound)});
    CHECK(std::string_view(ErrorString(Error::InvalidArgument)) == "InvalidArgument");
    const auto richError = ErrorInfo{Error::IoFailure, "read failed",
                                     std::make_error_code(std::errc::permission_denied)};
    CHECK(ErrorMessage(richError).find("read failed") != std::string::npos);
    CHECK(richError.SystemCode == std::errc::permission_denied);
    CHECK(ErrorMessage(richError).find(richError.SystemCode.message()) != std::string::npos);
}

TEST_CASE("Logger fans out prefixed messages to multiple sinks", "[core][logger]")
{
    auto first = std::make_shared<CollectingSink>();
    auto second = std::make_shared<CollectingSink>();
    Logger logger("ArcadeEditor");
    logger.AddSink(first);
    logger.AddSink(second);
    logger.LogError("Failed to open {}", "scene.json");
    REQUIRE(first->entries.size() == 1);
    REQUIRE(second->entries.size() == 1);
    CHECK(first->entries[0].first == LogLevel::Error);
    CHECK(first->entries[0].second == "[ArcadeEditor] Failed to open scene.json");
    logger.RemoveSink(second);
    logger.LogInfo("Ready");
    CHECK(first->entries.size() == 2);
    CHECK(second->entries.size() == 1);
}

TEST_CASE("Resource handles retain and release their objects safely", "[core][resource]")
{
    ResourceManager manager;
    auto created = manager.CreateOwned<int>("owned", 7);
    REQUIRE(created);

    auto borrowed = *created;
    auto locked = borrowed.Lock();
    REQUIRE(locked);
    CHECK(**locked == 7);

    REQUIRE(manager.UnloadOwned(borrowed.Id()));
    CHECK_FALSE(borrowed.Expired());
    locked.reset();
    CHECK(borrowed.Expired());
    CHECK_FALSE(borrowed.Lock());
}

TEST_CASE("Application owns initialized engine services", "[engine][lifecycle]")
{
    CHECK_THROWS_AS(Application::Get(), std::logic_error);
    Application application;
    CHECK(&Application::Get() == &application);
    CHECK(application.GetResourceManager().GetSingleton<int>());
    CHECK_THROWS_AS(Application(), std::logic_error);
}
