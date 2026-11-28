#include "MapScripts/Crywolf/CrywolfWater.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainDefines.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::MapScripts::Crywolf
{
namespace
{
using Data::Terrain::TerrainCellCount;
using Data::Terrain::TerrainSide;

constexpr std::uint8_t WaterTile = 5;
constexpr int BubbleOdds = 50;
constexpr int BubbleSpread = 100;
constexpr double CellSize = Terrain::CellSizeAsDouble; // 0x6b86b4.
constexpr double BubbleLift = 10.0;                    // 0x6b87a8.
constexpr float BubbleScale = 1.0f;                    // 0x5fa6f2.
constexpr std::int32_t BubbleSubtype = 0;

// The corners like Terrain::DrawTerrainCell: (x, y), (x + 1, y), (x + 1, y + 1) and (x, y + 1), with the layer's wrap
// on the last row.
std::array<std::size_t, 4> Corners(std::uint8_t x, std::uint8_t y)
{
    const std::size_t first = Data::Terrain::CellIndex(x, y);
    const std::size_t below = first + TerrainSide;
    return {first, (first + 1) % TerrainCellCount, (below + 1) % TerrainCellCount, below % TerrainCellCount};
}

// 0x5fa6ab..0x5fa731, one request of the water UVs.
Core::Expected<void, std::string> Bubble(std::uint8_t x, std::uint8_t y, const CrywolfWaterContext& context)
{
    if (context.random == nullptr)
        return Core::Unexpected(std::string("0x5fa6ab: las UV del agua del 34 sacan rand(), y el pase no lo tiene"));
    Crt::CrtRandom& random = *context.random;
    if (random.Next() % BubbleOdds != 0)
        return {};
    // fild of the integer, the coordinate times 100 and faddp, in the x87 up to the fstp.
    const int across = (random.Next() % BubbleSpread) + 1;
    const auto bubbleX =
        static_cast<float>(static_cast<double>(across) + (static_cast<double>(static_cast<float>(x)) * CellSize));
    const int down = (random.Next() % BubbleSpread) + 1;
    const auto bubbleY =
        static_cast<float>(static_cast<double>(down) + (static_cast<double>(static_cast<float>(y)) * CellSize));
    const auto bubbleZ = static_cast<float>(static_cast<double>(context.heroZ) + BubbleLift);
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x5fa731: falta el pool de partículas 0x743a198 para la 698/0"));
    const glm::vec3 light(std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3ecccccdu),
                          std::bit_cast<float>(0x3e4ccccdu));
    const auto created = context.particles->CreateChecked(
        Particles::BubbleParticle, glm::vec3(bubbleX, bubbleY, bubbleZ), context.heroAngles, light, BubbleSubtype,
        BubbleScale, context.worldClock.get(), random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x5fa731: el alta de 0x698/0 falla con error {}", static_cast<unsigned>(created.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepCrywolfWaterCell(const Terrain::TerrainView& view, std::uint8_t x, std::uint8_t y,
                                                       const CrywolfWaterContext& context)
{
    if (view.mapping == nullptr)
        return Core::Unexpected(std::string("0x5fa830: falta el mapeo del terreno del 34"));
    const Data::Terrain::TerrainMapping& mapping = *view.mapping;
    const std::array<std::size_t, 4> corners = Corners(x, y);
    bool someBelowOne = false;
    bool someAboveZero = false;
    for (const std::size_t corner : corners)
    {
        someBelowOne = someBelowOne || mapping.alpha[corner] < 1.0f;
        someAboveZero = someAboveZero || mapping.alpha[corner] > 0.0f;
    }
    // 0x5fa8d6..0x5fa8f6: local_8 at 1 only with layer 1 below at 5.
    const bool baseIsWater = someBelowOne && mapping.layer1[corners[0]] == WaterTile;
    if (baseIsWater)
    {
        if (auto bubble = Bubble(x, y, context); !bubble.HasValue())
            return bubble;
    }
    // 0x5fa9bf..0x5fa9dc: layer 2 with local_8, which goes back to 0 if layer 2 is not 5.
    if (someBelowOne && someAboveZero && baseIsWater && mapping.layer2[corners[0]] == WaterTile)
    {
        if (auto bubble = Bubble(x, y, context); !bubble.HasValue())
            return bubble;
    }
    return {};
}
} // namespace Client::MapScripts::Crywolf
