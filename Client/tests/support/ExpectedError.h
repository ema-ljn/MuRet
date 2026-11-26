#pragma once

#include "Core/Expected.h"

#include <optional>

// std::nullopt if it carries a value, so an EXPECT_EQ against the expected error also fails, with a message, when
// there was no error.
namespace Client::Tests
{
template <typename T, typename E> std::optional<E> ErrorOf(const Core::Expected<T, E>& result)
{
    if (result.HasValue())
    {
        return std::nullopt;
    }
    return result.Error();
}
} // namespace Client::Tests
