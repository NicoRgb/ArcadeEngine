#include "ArcadeEditor/EditorLogBuffer.hpp"

void EditorLogBuffer::ReceiveMessage(LogLevel level, std::string_view msg)
{
    std::scoped_lock lock(m_Mutex);
    if (m_Capacity == 0)
    {
        return;
    }
    m_Entries.push_back(EditorLogEntry{std::chrono::system_clock::now(), level, std::string(msg)});
    while (m_Entries.size() > m_Capacity)
    {
        m_Entries.pop_front();
    }
}

std::vector<EditorLogEntry> EditorLogBuffer::Snapshot() const
{
    std::scoped_lock lock(m_Mutex);
    return {m_Entries.begin(), m_Entries.end()};
}

void EditorLogBuffer::Clear()
{
    std::scoped_lock lock(m_Mutex);
    m_Entries.clear();
}
