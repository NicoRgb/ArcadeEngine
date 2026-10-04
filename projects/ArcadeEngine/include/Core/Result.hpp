#pragma once

#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <expected>
#include <cstdio>

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

inline const char* ErrorString(Error error)
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
        std::fprintf(stderr, "ResultOrThrow called on Error-Result %s\n", ErrorString(res.error()));
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
