#include "support/FileBytes.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <system_error>

namespace Client::Tests
{
// Con el tamaño de antemano y un solo read(): leer con istreambuf_iterator no marca el stream si
// falla, y libstdc++ tira una excepción en vez de devolver nada.
std::optional<std::vector<char>> ReadFileBytes(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return std::nullopt;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (bytes.empty())
    {
        return bytes;
    }
    if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
    {
        return std::nullopt;
    }
    return bytes;
}
} // namespace Client::Tests
