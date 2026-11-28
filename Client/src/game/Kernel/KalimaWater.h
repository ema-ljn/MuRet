#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Terrain
{
struct TerrainDecal;
} // namespace Client::Terrain

// Kalima's water (0x4377a0/0x723c48) as the modules below its map script reach it: Kernel::WorldMap holds it, the
// effects, the skills and the monsters disturb it, and MapScripts/Kalima's KalimaWorld implements it.
namespace Client::Kernel
{
struct KalimaFrameContext
{
    std::int32_t logicSteps;
    float worldTime;
    std::uint8_t heroTileX;
    std::uint8_t heroTileY;
    Crt::CrtRandom& random;
    // Each callback returns the value by copy, after closing its logical access.
    // Update calls them at the native points; it does not read both axes ahead.
    std::function<Core::Expected<std::int32_t, std::string>()> readHeroX{};
    std::function<Core::Expected<std::int32_t, std::string>()> readHeroY{};
};

class KalimaWater
{
public:
    virtual ~KalimaWater() = default;
    KalimaWater(const KalimaWater&) = delete;
    KalimaWater& operator=(const KalimaWater&) = delete;
    KalimaWater(KalimaWater&&) = delete;
    KalimaWater& operator=(KalimaWater&&) = delete;

    [[nodiscard]] virtual std::unique_ptr<KalimaWater> CloneForFrame() const = 0;
    // 0x41cca0: conditional disturbance, selector, waves and periodic fields, in that order.
    [[nodiscard]] virtual Core::Expected<void, std::string> Update(const KalimaFrameContext& context) = 0;
    // 0x41cf00/0x41d2d0: same camera; publishes the water (Alpha, 0x23) and the highlight (Additive, 0x24).
    [[nodiscard]] virtual Core::Expected<void, std::string> Draw(Render::ModelBatch& batch, std::uint8_t heroTileX,
                                                                 std::uint8_t heroTileY) = 0;
    // 0x437860: tile coordinates, the radius in wave cells. Keeps the selector.
    [[nodiscard]] virtual Core::Expected<void, std::string> Disturb(std::int32_t tileX, std::int32_t tileY,
                                                                    std::int32_t radius, std::int32_t amplitude,
                                                                    Crt::CrtRandom& random) = 0;
    // 0x4378e0/0x41dc80: clamps negatives; outside the defined plane it does not invent an alias.
    [[nodiscard]] virtual std::optional<float> Height(float x, float y) const = 0;
    // A ground effect's decal laid over the water (MapScripts/Kalima/KalimaSurfaceDecal.h).
    [[nodiscard]] virtual Core::Expected<void, std::string>
    DrawSurfaceDecal(Render::ModelBatch& batch, const Terrain::TerrainDecal& decal) const = 0;
    [[nodiscard]] virtual std::int32_t Map() const = 0;

protected:
    KalimaWater() = default;
};
} // namespace Client::Kernel
