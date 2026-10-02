#pragma once

#include <format>
#include <string_view>
#include <utility>

enum class LogLevel : uint8_t
{
    Debug,
    Info,
    Warn,
    Error
};

class LogSink
{
public:
    virtual ~LogSink() = default;

    virtual void ReceiveMessage(LogLevel level, std::string_view msg) = 0;
};

class Logger
{
public:
    explicit Logger(const std::shared_ptr<LogSink>& logSink) : m_Sink(logSink) {}
    ~Logger() = default;

    template <class... Args> void LogDebug(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    template <class... Args> void LogInfo(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template <class... Args> void LogWarn(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template <class... Args> void LogError(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMessage(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

private:
    template <class... Args>
    void LogMessage(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
    {
        std::string msg = std::format(fmt, std::forward<Args>(args)...);
        m_Sink->ReceiveMessage(level, msg);
    }

    std::shared_ptr<LogSink> m_Sink;
};

class StdoutLogSink : public LogSink
{
public:
    StdoutLogSink();
    ~StdoutLogSink() = default;

    void ReceiveMessage(LogLevel level, std::string_view msg) override;

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
