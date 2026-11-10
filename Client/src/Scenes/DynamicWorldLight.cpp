#include "Scenes/DynamicWorldLight.h"

#include "World/TerrainCells.h"
#include "World/TerrainLighting.h"

#include <memory>

namespace Client::Scenes
{
namespace
{
// c32bbd5f: firstX/Y (0x7d2d364/368) en la .bss; lastX/Y (0x6c9d68/6c9d6c) con ff 00 00 00
// en los offsets de archivo 0x2c9d68/0x2c9d6c. La primera copia cubre la grilla completa.
constexpr World::TerrainRange InitialRange{.firstX = 0, .lastX = 255, .firstY = 0, .lastY = 255};
} // namespace

DynamicWorldLight::DynamicWorldLight() : m_lighting(std::make_unique<World::TerrainLighting>()), m_range(InitialRange)
{
    // 0x7b4ccc4 está en la cola virtual de .data: los 65.536 RGB nacen en +0, no en la base.
}

World::TerrainLighting& DynamicWorldLight::Lighting()
{
    return *m_lighting;
}

const World::TerrainLighting& DynamicWorldLight::Lighting() const
{
    return *m_lighting;
}

const World::TerrainRange& DynamicWorldLight::Range() const
{
    return m_range;
}

void DynamicWorldLight::PublishRange(const World::TerrainRange& range)
{
    m_range = range;
}

void DynamicWorldLight::Update(const World::TerrainLighting& base)
{
    World::CopyTerrainLightWindow(base, *m_lighting, m_range.firstX, m_range.lastX, m_range.firstY, m_range.lastY);
}
} // namespace Client::Scenes
