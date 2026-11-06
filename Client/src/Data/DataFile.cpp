#include "Data/DataFile.h"

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Data
{
Core::Expected<std::vector<char>, std::error_code> ReadDataFilePrefix(const std::filesystem::path& path,
                                                                      std::size_t byteCount)
{
    if (!std::in_range<std::streamsize>(byteCount) || byteCount > std::vector<char>{}.max_size())
    {
        return Core::Unexpected(std::make_error_code(std::errc::value_too_large));
    }
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (error)
    {
        return Core::Unexpected(error);
    }
    if (std::filesystem::is_directory(status))
    {
        return Core::Unexpected(std::make_error_code(std::errc::is_a_directory));
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return Core::Unexpected(std::make_error_code(std::errc::not_supported));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return Core::Unexpected(std::make_error_code(std::errc::io_error));
    }
    std::vector<char> bytes(byteCount);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (file.bad())
    {
        return Core::Unexpected(std::make_error_code(std::errc::io_error));
    }
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    return bytes;
}

Core::Expected<std::vector<char>, std::error_code> ReadDataFile(const std::filesystem::path& path,
                                                                std::uintmax_t maxBytes)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return Core::Unexpected(error);
    }
    if (size > maxBytes)
    {
        return Core::Unexpected(std::make_error_code(std::errc::file_too_large));
    }
    return ReadDataFilePrefix(path, static_cast<std::size_t>(size));
}

std::string_view DescribeDataError(DataError error)
{
    switch (error)
    {
    case DataError::FileTooShort:
        return "el archivo es más corto de lo que pide su formato";
    case DataError::ChecksumMismatch:
        return "el checksum no es el de sus registros";
    case DataError::WrongWorld:
        return "es de otro mundo";
    case DataError::UnexpectedSize:
        return "no tiene ninguno de los tamaños que acepta su formato";
    case DataError::InvalidData:
        return "un valor no es uno de los que acepta su formato";
    }
    return "un error sin nombre";
}
} // namespace Client::Data
