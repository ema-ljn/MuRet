// Los vectores de CI, que están en el repo (Client/tests/vectors/): se leen enteros, y si falta uno
// el test falla.

#pragma once

#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Tests
{

// Los bytes del vector `name` de la carpeta `dir`. Si no está, el test falla y devuelve nada.
inline std::vector<std::byte> ReadVectorFile(const std::filesystem::path& dir, std::string_view name)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(dir / name);
    if (!file.has_value())
    {
        ADD_FAILURE() << "falta el vector " << name << " en " << dir;
        return {};
    }
    const std::span<const std::byte> bytes = std::as_bytes(std::span(*file));
    return {bytes.begin(), bytes.end()};
}

} // namespace Client::Tests
