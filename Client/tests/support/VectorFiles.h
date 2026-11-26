#pragma once

#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The CI vectors, which are in the repo (Client/tests/vectors/): read whole, and if one is missing the test fails.
namespace Client::Tests
{
// If it is missing, the test fails and it returns nothing.
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
