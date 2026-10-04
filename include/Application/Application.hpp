#pragma once

#include "Application/Logger.hpp"
#include "Core/Resource.hpp"

class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    static Application& Get() { return *s_Instance; }

    Logger& GetEngineLogger() { return m_EngineLogger; }
    ResourceManager& GetResourceManager() { return m_ResourceManager; }

    void Run();

private:
    static Application* s_Instance;

    ResourceManager m_ResourceManager;
    Logger m_EngineLogger;
};

#if defined(APP_DEBUG)
#define LOG_DEBUG(fmt, ...)                                                                        \
    Application::Get().GetEngineLogger().LogDebug(fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_INFO(fmt, ...)                                                                         \
    Application::Get().GetEngineLogger().LogInfo(fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                         \
    Application::Get().GetEngineLogger().LogWarn(fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_ERROR(fmt, ...)                                                                        \
    Application::Get().GetEngineLogger().LogError(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#define LOG_INFO(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_ERROR(fmt, ...)
#endif
