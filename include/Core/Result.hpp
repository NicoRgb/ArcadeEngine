#pragma once

#include <expected>

enum class Error : uint8_t
{
    InvalidArgument,
    NotFound
};

template <typename T>
using Result = std::expected<T, Error>;

template <typename T>
auto make_error(T&& err)
{
    return std::unexpected<std::decay_t<T>>(std::forward<T>(err));
}
