#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

namespace Client::Data
{
// A file over `maxBytes` is not of the requested format and is not read: std::errc::file_too_large. If it shrinks
// while being read, the bytes read so far come back and the loader checks the length.
[[nodiscard]] ENGINE_API Core::Expected<std::vector<char>, std::error_code>
ReadDataFile(const std::filesystem::path& path, std::uintmax_t maxBytes);

// An EOF returns the bytes available and the loader decides if they are enough (Gate.bmd needs only 2,304). A count
// that does not fit std::streamsize or the container is std::errc::value_too_large before any I/O or allocation.
// Follows symlinks to regular files; rejects directories with is_a_directory and other types with not_supported.
[[nodiscard]] ENGINE_API Core::Expected<std::vector<char>, std::error_code>
ReadDataFilePrefix(const std::filesystem::path& path, std::size_t byteCount);

// Never includes bytes of the file.
[[nodiscard]] ENGINE_API std::string_view DescribeDataError(DataError error);
} // namespace Client::Data
