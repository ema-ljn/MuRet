#pragma once

#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"
#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The dynamic table of 0x7b4ccc4 and the range the last 0x5fbb00 left: they live from one scene to the next, without
// being cleared when a map loads. Contract: 06 § La luz del terreno en un punto; 08 P-C46 (a).
namespace Client::Terrain
{
class DynamicWorldLight
{
public:
    DynamicWorldLight();

    [[nodiscard]] Terrain::TerrainLighting& Lighting();
    [[nodiscard]] const Terrain::TerrainLighting& Lighting() const;
    [[nodiscard]] const Terrain::TerrainRange& Range() const;

    // 0x5fbb00 in the drawing: publishes the four limits without copying or clearing light.
    void PublishRange(const Terrain::TerrainRange& range);
    // The first block of 0x5fbf60, in the step: copies the base into the last drawing's window, with lastX/lastY + 3
    // and wrap. Outside it the history stays. It does not update the waves or add lights. The limits keep the integer
    // preconditions of Terrain::CopyTerrainLightWindow.
    void Update(const Terrain::TerrainLighting& base);

private:
    std::unique_ptr<Terrain::TerrainLighting> m_lighting;
    Terrain::TerrainRange m_range{};
};

// 0x5f9780(x, y, color, radius, 0x7b4ccc4) on the durable dynamic light. Without a destination, or with a center that
// does not fit in int32 (NaN included), it cuts with `address` without touching anything.
[[nodiscard]] Core::Expected<void, std::string> AddDynamicLight(TerrainLighting* destination, const glm::vec3& position,
                                                                const glm::vec3& color, std::int32_t radius,
                                                                std::string_view address);
} // namespace Client::Terrain
