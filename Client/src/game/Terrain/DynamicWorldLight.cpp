#include "Terrain/DynamicWorldLight.h"

#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"

#include <memory>

namespace Client::Terrain
{
namespace
{
// c32bbd5f: firstX/Y (0x7d2d364/368) en la .bss; lastX/Y (0x6c9d68/6c9d6c) con ff 00 00 00
// en los offsets de archivo 0x2c9d68/0x2c9d6c. La primera copia cubre la grilla completa.
constexpr Terrain::TerrainRange InitialRange{.firstX = 0, .lastX = 255, .firstY = 0, .lastY = 255};
} // namespace

DynamicWorldLight::DynamicWorldLight() : m_lighting(std::make_unique<Terrain::TerrainLighting>()), m_range(InitialRange)
{
    // 0x7b4ccc4 está en la cola virtual de .data: los 65.536 RGB nacen en +0, no en la base.
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
