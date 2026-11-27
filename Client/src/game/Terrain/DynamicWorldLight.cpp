#include "Terrain/DynamicWorldLight.h"

#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"

#include <memory>

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
} // namespace Client::Terrain
