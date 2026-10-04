#include "Application/Application.hpp"
#include "Application/Logger.hpp"
#include "Assets/AssetManager.hpp"
#include "Core/Result.hpp"

#include <memory>
#include <tracy/Tracy.hpp>

Application* Application::s_Instance = nullptr;

Application::Application() : m_EngineLogger(std::make_shared<StdoutLogSink>())
{
    s_Instance = this;
    auto assetManager = ResultOrThrow(m_ResourceManager.GetSingleton<AssetManager>());
    // ResultOrThrow((assetManager->IndexAssets("assets")));
}

Application::~Application()
{
    s_Instance = nullptr;
}

void Application::Run()
{
    while (true)
    {
        FrameMark;
    }
}
