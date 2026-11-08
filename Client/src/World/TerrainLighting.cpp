#include "World/TerrainLighting.h"

#include "Core/Expected.h"
#include "Data/DataFile.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include "Data/WorldFolder.h"
#include "World/Ftol.h"
#include "World/WorldFiles.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Client::World
{
namespace
{
// Las unidades del mundo por celda (0x6b86b4) y su inversa, la de 0x5f99e0 (0x6b86c0, 0,01 en float).
constexpr float CellSize = 100.0f;
constexpr float CellsPerUnit = 0.01f;
// La última celda que 0x5f99e0 acepta, en x y en y: la 254, porque toma la de al lado.
constexpr std::int32_t LastSampledCell = 254;

// Los coeficientes de 0x5f8d90: n.x · 0,5 (0x6b882c) + a · n.y + b · n.z + 0,5, de 0 (0x6b86a0) a 1
// (0x6b87d4).
struct Shading
{
    float y = -0.5f;
    float z = 0.5f;
};
constexpr Shading NormalShading{};
constexpr Shading BrightShading{-1.0f, 1.0f};

float HeightAt(const Data::Terrain::TerrainHeights& heights, std::size_t x, std::size_t y)
{
    // La x + 1 y la y + 1 de 0x5f8b10 van en un byte (& 0xff).
    return heights.cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))];
}

// 0x5fd000(p1, p2, p3): (p2 − p1) × (p3 − p1), normalizada; con largo 0, la normal queda en 0.
glm::vec3 FaceNormal(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
{
    const glm::vec3 normal = glm::cross(p2 - p1, p3 - p1);
    const float length = glm::length(normal);
    return length != 0.0f ? normal / length : glm::vec3(0.0f);
}

// 0x5f8b10 en la celda (x, y): el triángulo (x + 1, y), (x + 1, y + 1), (x, y + 1).
glm::vec3 CellNormal(const Data::Terrain::TerrainHeights& heights, std::size_t x, std::size_t y)
{
    const float left = static_cast<float>(x) * CellSize;
    const float right = static_cast<float>(x + 1) * CellSize;
    const float bottom = static_cast<float>(y) * CellSize;
    const float top = static_cast<float>(y + 1) * CellSize;
    return FaceNormal({right, bottom, HeightAt(heights, x + 1, y)}, {right, top, HeightAt(heights, x + 1, y + 1)},
                      {left, top, HeightAt(heights, x, y + 1)});
}

float ShadeOf(const glm::vec3& normal, const Shading& shading)
{
    const float shade = (normal.x * 0.5f) + (shading.y * normal.y) + (shading.z * normal.z) + 0.5f;
    if (!(shade >= 0.0f))
    {
        return 0.0f;
    }
    return shade > 1.0f ? 1.0f : shade;
}

const Data::Terrain::TerrainLightColor& CellLight(const TerrainLighting& lighting, int x, int y)
{
    return lighting.cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))];
}
} // namespace

std::unique_ptr<TerrainLighting> ShadeTerrainLight(const Data::Terrain::TerrainLight& light,
                                                   const Data::Terrain::TerrainHeights& heights, std::uint8_t map)
{
    const Shading& shading = map == BrightShadingMap ? BrightShading : NormalShading;
    auto lighting = std::make_unique<TerrainLighting>();
    for (std::size_t y = 0; y < Data::Terrain::TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < Data::Terrain::TerrainSide; ++x)
        {
            const float shade = ShadeOf(CellNormal(heights, x, y), shading);
            const std::size_t cell = (y * Data::Terrain::TerrainSide) + x;
            for (std::size_t channel = 0; channel < Data::Terrain::TerrainLightChannels; ++channel)
            {
                lighting->cells[cell][channel] = shade * light.cells[cell][channel];
            }
        }
    }
    return lighting;
}

Core::Expected<std::unique_ptr<TerrainLighting>, std::string> LoadTerrainLighting(const std::filesystem::path& dataRoot,
                                                                                  std::uint8_t map)
{
    const std::string folder = Data::WorldFolder(map) + "\\";
    const Core::Expected<std::vector<char>, std::string> heightsFile =
        ReadWorldFile(dataRoot, folder + "TerrainHeight.OZB");
    if (!heightsFile.HasValue())
    {
        return Core::Unexpected(heightsFile.Error());
    }
    const auto heights = Data::Terrain::LoadTerrainHeights(std::as_bytes(std::span(heightsFile.Value())),
                                                           static_cast<std::uint8_t>(Data::WorldFolderNumber(map)));
    if (!heights.HasValue())
    {
        return Core::Unexpected("Data\\" + folder +
                                "TerrainHeight.OZB: " + std::string(Data::DescribeDataError(heights.Error())));
    }
    const Core::Expected<std::vector<char>, std::string> lightFile =
        ReadWorldFile(dataRoot, folder + "TerrainLight.OZJ");
    if (!lightFile.HasValue())
    {
        return Core::Unexpected(lightFile.Error());
    }
    const auto light = Data::Terrain::LoadTerrainLight(std::as_bytes(std::span(lightFile.Value())));
    if (!light.HasValue())
    {
        return Core::Unexpected("Data\\" + folder +
                                "TerrainLight.OZJ: " + std::string(Data::DescribeDataError(light.Error())));
    }
    return ShadeTerrainLight(*light.Value(), *heights.Value(), map);
}

glm::vec3 LightAt(const TerrainLighting& lighting, float x, float y)
{
    const float cellX = x * CellsPerUnit;
    const float cellY = y * CellsPerUnit;
    // Ambos stores F32 y conversiones FTOL64→low32 preceden a las guardas, en orden X/Y.
    const std::int32_t left = FtolLow(static_cast<double>(cellX));
    const std::int32_t bottom = FtolLow(static_cast<double>(cellY));
    if (left < 0 || bottom < 0 || left > LastSampledCell || bottom > LastSampledCell)
    {
        return glm::vec3(0.0f);
    }
    const double alongX = static_cast<double>(cellX) - static_cast<double>(left);
    const double alongY = static_cast<double>(cellY) - static_cast<double>(bottom);
    const Data::Terrain::TerrainLightColor& bottomLeft = CellLight(lighting, left, bottom);
    const Data::Terrain::TerrainLightColor& bottomRight = CellLight(lighting, left + 1, bottom);
    const Data::Terrain::TerrainLightColor& topLeft = CellLight(lighting, left, bottom + 1);
    const Data::Terrain::TerrainLightColor& topRight = CellLight(lighting, left + 1, bottom + 1);
    Data::Terrain::TerrainLightColor light{};
    for (std::size_t channel = 0; channel < Data::Terrain::TerrainLightChannels; ++channel)
    {
        // 0x5f99e0: Y por columna y luego X, reteniendo pesos/columnas en PC53/RN.
        // ClientLib ya desactiva FMA; sólo el resultado de cada canal vuelve a F32.
        const double leftColumn =
            ((static_cast<double>(topLeft[channel]) - static_cast<double>(bottomLeft[channel])) * alongY) +
            static_cast<double>(bottomLeft[channel]);
        const double rightColumn =
            ((static_cast<double>(topRight[channel]) - static_cast<double>(bottomRight[channel])) * alongY) +
            static_cast<double>(bottomRight[channel]);
        light[channel] = static_cast<float>(((rightColumn - leftColumn) * alongX) + leftColumn);
    }
    return {light[0], light[1], light[2]};
}
} // namespace Client::World
