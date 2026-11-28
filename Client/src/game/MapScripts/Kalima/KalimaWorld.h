#pragma once

#include "Core/Expected.h"
#include "MapScripts/Kalima/KalimaWaves.h"

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

// 06 § Kalima: portable owner of 0x4377a0/0x723c48. The map does not stand in for its existence.
namespace Client::MapScripts::Kalima
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
class KalimaWorld final
{
public:
    explicit KalimaWorld(std::int32_t map);
    ~KalimaWorld();
    KalimaWorld(const KalimaWorld&) = delete;
    KalimaWorld& operator=(const KalimaWorld&) = delete;
    [[nodiscard]] std::unique_ptr<KalimaWorld> CloneForFrame() const;
    // 0x41cca0: conditional disturbance, selector, waves and periodic fields, in that order.
    [[nodiscard]] Core::Expected<void, std::string> Update(const KalimaFrameContext& context);
    // 0x41cf00/0x41d2d0: same camera; publishes the water (Alpha, 0x23) and the highlight (Additive, 0x24).
    [[nodiscard]] Core::Expected<void, std::string> Draw(Render::ModelBatch& batch, std::uint8_t heroTileX,
                                                         std::uint8_t heroTileY);
    // 0x437860: tile coordinates, the radius in wave cells. Keeps the selector.
    [[nodiscard]] Core::Expected<void, std::string> Disturb(std::int32_t tileX, std::int32_t tileY, std::int32_t radius,
                                                            std::int32_t amplitude, Crt::CrtRandom& random);
    // 0x4378e0/0x41dc80: clamps negatives; outside the defined plane it does not invent an alias.
    [[nodiscard]] std::optional<float> Height(float x, float y) const;
    [[nodiscard]] const KalimaWaveState& Waves() const;
    [[nodiscard]] std::int32_t Map() const;

private:
    struct Storage;
    std::unique_ptr<Storage> m_storage;
};
} // namespace Client::MapScripts::Kalima
