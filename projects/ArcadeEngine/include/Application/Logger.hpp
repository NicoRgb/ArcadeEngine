#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Export.hpp"

enum class LogLevel : uint8_t
{
    Debug,
    Info,
    Warn,
    Error
};

class ARCADE_ENGINE_API LogSink
{
public:
    virtual ~LogSink() = default;

    virtual void ReceiveMessage(LogLevel level, std::string_view msg) = 0;
};

class Logger
{
public:
    explicit Logger(const std::shared_ptr<LogSink>& logSink, std::string prefix = {})
        : m_Prefix(std::move(prefix))
    {
        if (!logSink)
        {
            throw std::invalid_argument("Logger requires a log sink.");
        }
        m_Sinks.push_back(logSink);
    }
    explicit Logger(std::string prefix = {}) : m_Prefix(std::move(prefix)) {}
    ~Logger() = default;

    void AddSink(const std::shared_ptr<LogSink>& sink)
    {
        if (!sink)
        {
            throw std::invalid_argument("Logger cannot add an empty sink.");
        }
        std::scoped_lock lock(m_Mutex);
        if (std::ranges::find(m_Sinks, sink) == m_Sinks.end())
        {
            m_Sinks.push_back(sink);
        }
    }

    void RemoveSink(const std::shared_ptr<LogSink>& sink)
    {
        std::scoped_lock lock(m_Mutex);
        std::erase(m_Sinks, sink);
    }

    template <class... Args>
    void LogDebug(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    template <class... Args>
    void LogInfo(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template <class... Args>
    void LogWarn(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template <class... Args>
    void LogError(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

private:
    template <class... Args>
    void LogMessage(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
    {
        std::string msg = std::format(fmt, std::forward<Args>(args)...);
        if (!m_Prefix.empty())
        {
            msg = std::format("[{}] {}", m_Prefix, msg);
        }
        std::vector<std::shared_ptr<LogSink>> sinks;
        {
            std::scoped_lock lock(m_Mutex);
            sinks = m_Sinks;
        }
        for (const auto& sink : sinks)
        {
            sink->ReceiveMessage(level, msg);
        }
    }

    std::string m_Prefix;
    mutable std::mutex m_Mutex;
    std::vector<std::shared_ptr<LogSink>> m_Sinks;
};

class StdoutLogSink : public LogSink
{
public:
    ARCADE_ENGINE_API StdoutLogSink();
    ~StdoutLogSink() = default;

    ARCADE_ENGINE_API void ReceiveMessage(LogLevel level, std::string_view msg) override;

private:
    static constexpr std::string_view RESET = "\033[0m";
    static constexpr std::string_view GREY = "\033[90m";
    static constexpr std::string_view GREEN = "\033[32m";
    static constexpr std::string_view YELLOW = "\033[33m";
    static constexpr std::string_view RED = "\033[31m";

    struct LevelConfig
    {
        std::string_view name;
        std::string_view color;
    };

    static constexpr LevelConfig get_config(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:
            return {.name = "[DEBUG]", .color = GREY};
        case LogLevel::Info:
            return {.name = "[INFO] ", .color = GREEN};
        case LogLevel::Warn:
            return {.name = "[WARN] ", .color = YELLOW};
        case LogLevel::Error:
            return {.name = "[ERROR]", .color = RED};
        default:
            return {.name = "[LOG]  ", .color = RESET};
        }
    }

    bool m_UseAnsi = false;
};
