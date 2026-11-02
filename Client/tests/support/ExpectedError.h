#pragma once

#include "Core/Expected.h"

#include <optional>

// El error de un Core::Expected, para compararlo en un test: std::nullopt si trae un valor. Así un
// EXPECT_EQ con el error esperado falla, con un mensaje, también cuando no hubo error.
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
