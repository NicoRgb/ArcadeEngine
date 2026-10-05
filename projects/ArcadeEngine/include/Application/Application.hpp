#pragma once

#include <stdexcept>

#include "Application/Logger.hpp"
#include "Core/Export.hpp"
#include "Core/Resource.hpp"

class Application
{
public:
    ARCADE_ENGINE_API Application();
    ARCADE_ENGINE_API ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    ARCADE_ENGINE_API static Application& Get();

    Logger& GetEngineLogger() { return m_EngineLogger; }
    ResourceManager& GetResourceManager() { return m_ResourceManager; }

private:
    static Application* s_Instance;

    ResourceManager m_ResourceManager;
    Logger m_EngineLogger;
};

#if !defined(NDEBUG)
#define LOG_DEBUG(...) Application::Get().GetEngineLogger().LogDebug(__VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#define LOG_INFO(...) Application::Get().GetEngineLogger().LogInfo(__VA_ARGS__)
#define LOG_WARN(...) Application::Get().GetEngineLogger().LogWarn(__VA_ARGS__)
#define LOG_ERROR(...) Application::Get().GetEngineLogger().LogError(__VA_ARGS__)
