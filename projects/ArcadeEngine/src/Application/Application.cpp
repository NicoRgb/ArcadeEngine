#include "Application/Application.hpp"
#include "Application/Logger.hpp"
#include "Assets/AssetManager.hpp"
#include "Core/Result.hpp"

#include <memory>
#include <stdexcept>

Application* Application::s_Instance = nullptr;

Asset::~Asset() = default;

Application::Application() : m_EngineLogger(std::make_shared<StdoutLogSink>())
{
    if (s_Instance != nullptr)
    {
        throw std::logic_error("Only one Application may be active at a time.");
    }

    s_Instance = this;
}

AssetManager& Application::GetAssetManager()
{
    return *ResultOrThrow(m_ResourceManager.GetSingleton<AssetManager>()).Get();
}

Application::~Application()
{
    if (s_Instance == this)
    {
        s_Instance = nullptr;
    }
}

Application& Application::Get()
{
    if (s_Instance == nullptr)
    {
        throw std::logic_error("Application::Get called without an active Application.");
    }

    return *s_Instance;
}
