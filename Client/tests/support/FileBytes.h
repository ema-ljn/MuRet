#pragma once

#include <filesystem>
#include <optional>
#include <vector>

// Reads a whole file, for the golden tests. The product does not read from disk in the data layer: its loaders
// receive the bytes.
namespace Client::Tests
{
// std::nullopt if it is not a regular file (a folder, for example) or could not be read whole. As char: a loader
// receives them as std::as_bytes(std::span(*file)). Reads as many bytes as the size the system reports, so it suits
// files that do not change while read, like the golden ones. One from /proc reports 0 bytes and reads empty.
std::optional<std::vector<char>> ReadFileBytes(const std::filesystem::path& path);
} // namespace Client::Tests
