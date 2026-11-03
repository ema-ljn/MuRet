#include "Data/DataFile.h"

#include <cstddef>
#include <fstream>
#include <ios>

namespace Client::Data
{
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
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file.is_open() || file.bad())
    {
        return Core::Unexpected(std::make_error_code(std::errc::io_error));
    }
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    return bytes;
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
