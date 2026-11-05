#include "World/WorldFiles.h"

#include "Core/Expected.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::World
{
namespace
{
// Más que cualquier TerrainHeight.OZB (1.080 + 65.536 bytes), EncTerrain.map (196.610) o
// TerrainLight.OZJ del laboratorio.
constexpr std::uintmax_t MaxWorldFileBytes = std::uintmax_t{16} * 1024 * 1024;
} // namespace

Core::Expected<std::vector<char>, std::string> ReadWorldFile(const std::filesystem::path& dataRoot,
                                                             const std::string& relative)
{
    std::error_code error;
    const std::optional<std::filesystem::path> path = Data::ResolveDataPath(dataRoot, relative, error);
    if (!path.has_value())
    {
        return Core::Unexpected("Data\\" + relative + (error ? " no se puede buscar: " + error.message() : " no está"));
    }
    Core::Expected<std::vector<char>, std::error_code> bytes = Data::ReadDataFile(*path, MaxWorldFileBytes);
    if (!bytes.HasValue())
    {
        return Core::Unexpected("Data\\" + relative + " no se puede leer: " + bytes.Error().message());
    }
    return std::move(bytes).Value();
}
} // namespace Client::World
