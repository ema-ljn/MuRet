#include "Terrain/TerrainLighting.h"

#include "Core/Expected.h"
#include "Data/DataFile.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include "Maps/WorldFolder.h"
#include "Terrain/FaceNormal.h"
#include "Math/Ftol.h"
#include "Terrain/WorldFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Client::Terrain
{
namespace
{
// The world units per cell (0x6b86b4) and their inverse, that of 0x5f99e0 (0x6b86c0, 0.01 as float).
constexpr float CellSize = 100.0f;
constexpr float CellsPerUnit = 0.01f;
// The last cell 0x5f99e0 accepts, in x and in y: 254, because it takes the next one.
constexpr std::int32_t LastSampledCell = 254;
// The RGB window of 0x5fbf60 includes the four cells of the last block.
constexpr std::int32_t LightWindowTail = 3;
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);

// The coefficients of n.y and n.z in 0x5f8d90, floats in memory: −0.5 (0x6ba298) and 0.5 (0x6b882c), or −1
// (0x6b8b9c) and 1 (0x6b87d4) on map 30.
struct Shading
{
    float y = -0.5f;
    float z = 0.5f;
};
constexpr Shading NormalShading{};
constexpr Shading BrightShading{-1.0f, 1.0f};
// 0x6b882c: the weight of n.x and what 0x5f8d90 adds at the end.
constexpr double ShadeHalf = 0.5;

float HeightAt(const Data::Terrain::TerrainHeights& heights, std::size_t x, std::size_t y)
{
    // The x + 1 and y + 1 of 0x5f8b10 go in a byte (& 0xff).
    return heights.cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))];
}

// 0x5f8b10 at cell (x, y): 0x5fd000 with the triangle (x + 1, y), (x + 1, y + 1), (x, y + 1). The widths are 100 in x
// and in y, so nz is 10,000 and the length is never 0.
glm::vec3 CellNormal(const Data::Terrain::TerrainHeights& heights, std::size_t x, std::size_t y)
{
    const float left = static_cast<float>(x) * CellSize;
    const float right = static_cast<float>(x + 1) * CellSize;
    const float bottom = static_cast<float>(y) * CellSize;
    const float top = static_cast<float>(y + 1) * CellSize;
    return FaceNormal({right, bottom, HeightAt(heights, x + 1, y)}, {right, top, HeightAt(heights, x + 1, y + 1)},
                      {left, top, HeightAt(heights, x, y + 1)});
}

// 0x5f8d90 at a cell, in ASM order and in the register (53 bits): (b · n.z + a · n.y) + n.x · 0.5, then + 0.5. Below
// 0 or NaN gives 0 (FCOM with C0); above 1 gives 1. The shade stays in the register for the three channels.
double ShadeOf(const glm::vec3& normal, const Shading& shading)
{
    const double slope = (static_cast<double>(shading.z) * static_cast<double>(normal.z)) +
                         (static_cast<double>(shading.y) * static_cast<double>(normal.y));
    const double shade = (slope + (static_cast<double>(normal.x) * ShadeHalf)) + ShadeHalf;
    if (!(shade >= 0.0))
    {
        return 0.0;
    }
    return shade > 1.0 ? 1.0 : shade;
}

const Data::Terrain::TerrainLightColor& CellLight(const TerrainLighting& lighting, int x, int y)
{
    return lighting.cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))];
}
// 0x5f9780 stores each channel in F32, but decides the floor with the wide sum still live.
void AddWeightedLight(Data::Terrain::TerrainLightColor& destination, const Data::Terrain::TerrainLightColor& color,
                      float weight)
{
    for (std::size_t channel = 0; channel < Data::Terrain::TerrainLightChannels; ++channel)
    {
        const double product = static_cast<double>(weight) * static_cast<double>(color[channel]);
        const double sum = product + static_cast<double>(destination[channel]);
        destination[channel] = static_cast<float>(sum);
        if (!(sum >= 0.0))
        {
            destination[channel] = 0.0f;
        }
    }
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
            const double shade = ShadeOf(CellNormal(heights, x, y), shading);
            const std::size_t cell = (y * Data::Terrain::TerrainSide) + x;
            for (std::size_t channel = 0; channel < Data::Terrain::TerrainLightChannels; ++channel)
            {
                // FMUL float and FSTP float: the product is rounded to 53 bits and then to float.
                lighting->cells[cell][channel] =
                    static_cast<float>(shade * static_cast<double>(light.cells[cell][channel]));
            }
        }
    }
    return lighting;
}

void ReshadeTerrainLightWindow(TerrainLighting& lighting, const Data::Terrain::TerrainLight& raw,
                               const Data::Terrain::TerrainHeights& heights, std::int32_t centerX, std::int32_t centerY)
{
    centerX = std::clamp(centerX, 4, 252);
    centerY = std::clamp(centerY, 4, 252);
    for (int y = centerY - 4; y < centerY + 4; ++y)
        for (int x = centerX - 4; x < centerX + 4; ++x)
        {
            const auto normal = CellNormal(heights, static_cast<std::size_t>(x), static_cast<std::size_t>(y));
            // 0x5f8e82..0x5f8ea8: adds X + Z before the product, subtracts Y and stores a float before the clamp.
            const float shade = static_cast<float>((static_cast<double>(normal.x) + normal.z) * .5 -
                                                   static_cast<double>(normal.y) * .5 + .5);
            const float clamped = !(shade >= 0) ? 0.0f : std::min(shade, 1.0f);
            const auto cell = Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));
            for (std::size_t channel = 0; channel < Data::Terrain::TerrainLightChannels; ++channel)
                lighting.cells[cell][channel] =
                    static_cast<float>(static_cast<double>(clamped) * raw.cells[cell][channel]);
        }
}

Core::Expected<std::unique_ptr<TerrainLighting>, std::string> LoadTerrainLighting(const std::filesystem::path& dataRoot,
                                                                                  std::uint8_t map)
{
    const std::string folder = Maps::WorldFolder(map) + "\\";
    const Core::Expected<std::vector<char>, std::string> heightsFile =
        ReadWorldFile(dataRoot, folder + "TerrainHeight.OZB");
    if (!heightsFile.HasValue())
    {
        return Core::Unexpected(heightsFile.Error());
    }
    const auto heights = Data::Terrain::LoadTerrainHeights(std::as_bytes(std::span(heightsFile.Value())),
                                                           static_cast<std::uint8_t>(Maps::WorldFolderNumber(map)));
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

void CopyTerrainLightWindow(const TerrainLighting& source, TerrainLighting& destination, std::int32_t firstX,
                            std::int32_t lastX, std::int32_t firstY, std::int32_t lastY)
{
    if (&source == &destination)
    {
        return;
    }
    for (std::int32_t row = firstY; row <= lastY + LightWindowTail; ++row)
    {
        for (std::int32_t column = firstX; column <= lastX + LightWindowTail; ++column)
        {
            const std::size_t index =
                Data::Terrain::CellIndex(static_cast<std::uint8_t>(column), static_cast<std::uint8_t>(row));
            for (std::size_t channel = 0; channel < Data::Terrain::TerrainLightChannels; ++channel)
            {
                // MOV dword keeps the bits, without FP operations or quieting a signaling NaN.
                std::memcpy(&destination.cells[index][channel], &source.cells[index][channel], sizeof(float));
            }
        }
    }
}

void AddTerrainLight(TerrainLighting& destination, float worldX, float worldY,
                     const Data::Terrain::TerrainLightColor& color, std::int32_t radius)
{
    const float radiusFloat = static_cast<float>(radius);
    // The FSTP calls materialize cellX/Y before __ftol and the distance: 100 * float(0.01) rounds to 1. dx/dy use those
    // float32 values with their fraction, not the wide product or the truncated center.
    const float cellX = static_cast<float>(static_cast<double>(worldX) * static_cast<double>(CellsPerUnit));
    const float cellY = static_cast<float>(static_cast<double>(worldY) * static_cast<double>(CellsPerUnit));
    const auto centerX = static_cast<std::int32_t>(cellX);
    const auto centerY = static_cast<std::int32_t>(cellY);
    for (std::int32_t row = centerY - radius; row <= centerY + radius; ++row)
    {
        // 0x5f97ed..0x5f97f4 keeps the scaled float32 Y, not the sweep's truncated center.
        const double dy = static_cast<double>(cellY) - static_cast<double>(row);
        const double dySquared = dy * dy;
        for (std::int32_t column = centerX - radius; column <= centerX + radius; ++column)
        {
            const double dx = static_cast<double>(cellX) - static_cast<double>(column);
            const double dxSquared = dx * dx;
            const double distance = std::sqrt(dxSquared + dySquared);
            const double difference = static_cast<double>(radiusFloat) - distance;
            const double weightWide = difference / static_cast<double>(radiusFloat);
            const float weight = static_cast<float>(weightWide);
            if (!(weightWide > 0.0))
            {
                continue;
            }
            const std::size_t index =
                Data::Terrain::CellIndex(static_cast<std::uint8_t>(column), static_cast<std::uint8_t>(row));
            AddWeightedLight(destination.cells[index], color, weight);
        }
    }
}

glm::vec3 LightAt(const TerrainLighting& lighting, float x, float y)
{
    const float cellX = x * CellsPerUnit;
    const float cellY = y * CellsPerUnit;
    // Both F32 stores and FTOL64 → low32 conversions come before the guards, in X/Y order.
    const std::int32_t left = Math::FtolLow(static_cast<double>(cellX));
    const std::int32_t bottom = Math::FtolLow(static_cast<double>(cellY));
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
        // 0x5f99e0: Y per column and then X, keeping weights and columns in PC53/RN. ClientLib already turns FMA off;
        // only each channel's result goes back to F32.
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
} // namespace Client::Terrain
