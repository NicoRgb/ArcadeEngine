#pragma once

#include "Application/Logger.hpp"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct EditorLogEntry
{
    std::chrono::system_clock::time_point Time;
    LogLevel Level = LogLevel::Info;
    std::string Message;
};

class EditorLogBuffer final : public LogSink
{
public:
    explicit EditorLogBuffer(std::size_t capacity = 4000) : m_Capacity(capacity) {}
    void ReceiveMessage(LogLevel level, std::string_view msg) override;
    [[nodiscard]] std::vector<EditorLogEntry> Snapshot() const;
    void Clear();

private:
    const std::size_t m_Capacity;
    mutable std::mutex m_Mutex;
    std::deque<EditorLogEntry> m_Entries;
};
