#include "Terrain/DynamicWorldLight.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainLight.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace Client::Terrain
{
namespace
{
// c32bbd5f: firstX/Y (0x7d2d364/0x7d2d368) in the .bss; lastX/Y (0x6c9d68/0x6c9d6c) hold ff 00 00 00 at file offsets
// 0x2c9d68/0x2c9d6c. The first copy covers the whole grid.
constexpr Terrain::TerrainRange InitialRange{.firstX = 0, .lastX = 255, .firstY = 0, .lastY = 255};
} // namespace

DynamicWorldLight::DynamicWorldLight() : m_lighting(std::make_unique<Terrain::TerrainLighting>()), m_range(InitialRange)
{
    // 0x7b4ccc4 is in the virtual tail of .data: the 65,536 RGB values start at +0, not at the base.
}

Terrain::TerrainLighting& DynamicWorldLight::Lighting()
{
    return *m_lighting;
}

const Terrain::TerrainLighting& DynamicWorldLight::Lighting() const
{
    return *m_lighting;
}

const Terrain::TerrainRange& DynamicWorldLight::Range() const
{
    return m_range;
}

void DynamicWorldLight::PublishRange(const Terrain::TerrainRange& range)
{
    m_range = range;
}

void DynamicWorldLight::Update(const Terrain::TerrainLighting& base)
{
    Terrain::CopyTerrainLightWindow(base, *m_lighting, m_range.firstX, m_range.lastX, m_range.firstY, m_range.lastY);
}

Core::Expected<void, std::string> AddDynamicLight(TerrainLighting* destination, const glm::vec3& position,
                                                  const glm::vec3& color, std::int32_t radius, std::string_view address)
{
    if (destination == nullptr)
    {
        return Core::Unexpected(std::format("{}: falta el buffer dinámico 0x7b4ccc4 para 0x5f9780", address));
    }
    // The kernel needs a center and representable int32 limits; do not invent a cell for NaN.
    constexpr float CellScale = std::bit_cast<float>(0x3c23d70au);
    for (int axis = 0; axis < 2; ++axis)
    {
        const float scaled = static_cast<float>(static_cast<double>(position[axis]) * static_cast<double>(CellScale));
        const double center = std::trunc(static_cast<double>(scaled));
        if (!std::isfinite(center) || center < static_cast<double>(std::numeric_limits<std::int32_t>::min()) + radius ||
            center > static_cast<double>(std::numeric_limits<std::int32_t>::max()) - radius - 1)
        {
            return Core::Unexpected(std::format("{}: centro de luz fuera del dominio int32 de 0x5f9780", address));
        }
    }
    const Data::Terrain::TerrainLightColor channels{color.x, color.y, color.z};
    AddTerrainLight(*destination, position.x, position.y, channels, radius);
    return {};
}
} // namespace Client::Terrain
