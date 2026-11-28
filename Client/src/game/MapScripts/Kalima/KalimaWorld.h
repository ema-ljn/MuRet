#pragma once

#include "Core/Expected.h"
#include "Kernel/KalimaWater.h"
#include "MapScripts/Kalima/KalimaWaves.h"

#include <cstdint>
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

// 06 § Kalima: portable owner of 0x4377a0/0x723c48. The map does not stand in for its existence.
namespace Client::MapScripts::Kalima
{
class KalimaWorld final : public Kernel::KalimaWater
{
public:
    explicit KalimaWorld(std::int32_t map);
    ~KalimaWorld() override;
    KalimaWorld(const KalimaWorld&) = delete;
    KalimaWorld& operator=(const KalimaWorld&) = delete;
    KalimaWorld(KalimaWorld&&) = delete;
    KalimaWorld& operator=(KalimaWorld&&) = delete;
    [[nodiscard]] std::unique_ptr<Kernel::KalimaWater> CloneForFrame() const override;
    [[nodiscard]] Core::Expected<void, std::string> Update(const Kernel::KalimaFrameContext& context) override;
    [[nodiscard]] Core::Expected<void, std::string> Draw(Render::ModelBatch& batch, std::uint8_t heroTileX,
                                                         std::uint8_t heroTileY) override;
    [[nodiscard]] Core::Expected<void, std::string> Disturb(std::int32_t tileX, std::int32_t tileY, std::int32_t radius,
                                                            std::int32_t amplitude, Crt::CrtRandom& random) override;
    [[nodiscard]] std::optional<float> Height(float x, float y) const override;
    [[nodiscard]] Core::Expected<void, std::string> DrawSurfaceDecal(Render::ModelBatch& batch,
                                                                     const Terrain::TerrainDecal& decal) const override;
    [[nodiscard]] std::int32_t Map() const override;
    [[nodiscard]] const KalimaWaveState& Waves() const;

private:
    struct Storage;
    std::unique_ptr<Storage> m_storage;
};
} // namespace Client::MapScripts::Kalima
