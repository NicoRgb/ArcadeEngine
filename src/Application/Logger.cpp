#include "Application/Logger.hpp"

#include <iostream>

#ifdef _WIN32
#include <io.h>
#define IS_TERMINAL(stream) _isatty(_fileno(stream))
#else
#include <unistd.h>
#define IS_TERMINAL(stream) isatty(fileno(stream))
#endif

StdoutLogSink::StdoutLogSink()
{
    m_UseAnsi = IS_TERMINAL(stdout);
    if (const char* no_color_env = std::getenv("NO_COLOR"); no_color_env != nullptr)
    {
        m_UseAnsi = false;
    }
}

void StdoutLogSink::ReceiveMessage(LogLevel level, std::string_view msg)
{
    auto config = get_config(level);

    if (m_UseAnsi)
    {
        std::cout << config.color << config.name << RESET << " " << msg << "\n";
    }
    else
    {
        std::cout << config.name << " " << msg << "\n";
    }
}
