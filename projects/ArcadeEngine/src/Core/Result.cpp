#include "Core/Result.hpp"
#include "Application/Application.hpp"

#include <filesystem>

ARCADE_ENGINE_API std::unexpected<ErrorInfo> MakeError(Error code, std::string message,
                                                       std::error_code systemCode, std::string file,
                                                       uint64_t line)
{
    ErrorInfo info(code, std::move(message), systemCode);

#ifdef LOG_MAKE_ERROR
    std::filesystem::path filename = std::filesystem::path(file).filename();
    LOG_ERROR("MakeError called: Error::{} at {}:{}", ErrorMessage(info), filename.string(), line);
#endif

    return std::unexpected<ErrorInfo>(info);
}

ARCADE_ENGINE_API std::unexpected<ErrorInfo> MakeError(ErrorInfo error)
{
    return std::unexpected<ErrorInfo>(std::move(error));
}
