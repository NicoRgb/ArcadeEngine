#pragma once

#include "Core/Export.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <print>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

enum class Error : uint8_t
{
    InvalidArgument,
    NotFound,
    AlreadyExists,
    PermissionDenied,
    IoFailure,
    ParseFailure,
    InvalidState,
    Unsupported,
    Cancelled,
    Internal,
};

struct ErrorInfo
{
    Error Code = Error::Internal;
    std::string Message;
    std::error_code SystemCode;

    ErrorInfo() = default;
    ErrorInfo(Error code, std::string message = {}, std::error_code systemCode = {})
        : Code(code), Message(std::move(message)), SystemCode(systemCode)
    {
    }
};

template <typename T>
using Result = std::expected<T, ErrorInfo>;

#define MAKE_ERROR(code) MakeError(code, "no message", {}, __FILE__, __LINE__)
#define MAKE_ERROR_MSG(code, msg) MakeError(code, msg, {}, __FILE__, __LINE__)
#define MAKE_ERROR_EXT(code, msg, sys) MakeError(code, msg, sys, __FILE__, __LINE__)

#define FORWARD_ERROR(err) MakeError((err).error())

ARCADE_ENGINE_API std::unexpected<ErrorInfo> MakeError(Error code, std::string message = {},
                                                       std::error_code systemCode = {},
                                                       std::string file = {}, uint64_t line = 0);

ARCADE_ENGINE_API std::unexpected<ErrorInfo> MakeError(ErrorInfo error);

inline const char* ErrorString(Error error)
{
    switch (error)
    {
    case Error::InvalidArgument:
        return "InvalidArgument";
    case Error::NotFound:
        return "NotFound";
    case Error::AlreadyExists:
        return "AlreadyExists";
    case Error::PermissionDenied:
        return "PermissionDenied";
    case Error::IoFailure:
        return "IoFailure";
    case Error::ParseFailure:
        return "ParseFailure";
    case Error::InvalidState:
        return "InvalidState";
    case Error::Unsupported:
        return "Unsupported";
    case Error::Cancelled:
        return "Cancelled";
    case Error::Internal:
        return "Internal";
    }
    return "Unknown";
}

inline std::string ErrorMessage(const ErrorInfo& error)
{
    std::string message = ErrorString(error.Code);
    if (!error.Message.empty())
    {
        message += ": ";
        message += error.Message;
    }
    if (error.SystemCode)
    {
        message += " (";
        message += error.SystemCode.message();
        message += ")";
    }
    return message;
}

inline const char* ErrorString(const ErrorInfo& error)
{
    return ErrorString(error.Code);
}

template <typename T>
T ResultOrThrow(Result<T> res)
{
    if (!res)
    {
        const std::string message = ErrorMessage(res.error());
        std::println(stderr, "ResultOrThrow called on Error-Result {}", message);
        std::exit(EXIT_FAILURE);
    }

    if constexpr (std::is_void_v<T>)
    {
        return;
    }
    else
    {
        return std::move(res).value();
    }
}
