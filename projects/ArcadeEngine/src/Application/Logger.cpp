#include "Application/Logger.hpp"

#include <cstdio>
#include <cstdlib>
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
#ifdef _WIN32
    char* no_color_env = nullptr;
    size_t no_color_env_size = 0;
    const errno_t env_result = _dupenv_s(&no_color_env, &no_color_env_size, "NO_COLOR");
    if (env_result == 0 && no_color_env != nullptr)
    {
        m_UseAnsi = false;
    }
    std::free(no_color_env);
#else
    if (const char* no_color_env = std::getenv("NO_COLOR"); no_color_env != nullptr)
    {
        m_UseAnsi = false;
    }
#endif
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
