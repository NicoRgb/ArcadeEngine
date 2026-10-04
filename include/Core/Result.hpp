#pragma once

#include <cstdlib>
#include <expected>
#include <print>

enum class Error : uint8_t
{
    InvalidArgument,
    NotFound
};

template <typename T>
using Result = std::expected<T, Error>;

template <typename T>
auto MakeError(T&& err)
{
    return std::unexpected<std::decay_t<T>>(std::forward<T>(err));
}

static const char* ErrorString(Error error)
{
    switch (error)
    {
    case Error::InvalidArgument:
        return "InvalidArgument";
    case Error::NotFound:
        return "NotFound";
    }

    return "Unknown";
}

template <typename T>
T ResultOrThrow(Result<T> res)
{
    if (!res)
    {
        std::println("ResultOrThrow called on Error-Result {}", ErrorString(res.error()));
        exit(EXIT_FAILURE);
    }
}
