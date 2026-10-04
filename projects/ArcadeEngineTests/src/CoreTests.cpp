#include "Application/Application.hpp"
#include "Core/Resource.hpp"
#include "Core/Result.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ResultOrThrow returns successful values", "[core][result]")
{
    CHECK(ResultOrThrow(Result<int>{42}) == 42);
    CHECK_NOTHROW(ResultOrThrow(Result<void>{}));
    CHECK_FALSE(Result<int>{MakeError(Error::NotFound)});
    CHECK(std::string_view(ErrorString(Error::InvalidArgument)) == "InvalidArgument");
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
