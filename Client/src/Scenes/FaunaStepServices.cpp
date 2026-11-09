#include "Scenes/FaunaStepServices.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "Render/Models/Cloth.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ITerrainAttributeQuerySource.h"
#include "World/TerrainAttributeQuery.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace Client::Scenes
{
namespace
{
// 446120-contract §prefijo:0x446219,5f9780 con radio16 y estos bits F32.
constexpr std::int32_t PrefixRadius = 16;
constexpr Data::Terrain::TerrainLightColor PrefixColor{-0.3f, -0.3f, -0.2f};
constexpr float LightCellsPerUnit = 0.01f;

// Cada ReadScale relee el productor prestado. Attributes se pide después de Y y X;
// la tabla es la misma que TileAttributes y Height, nunca un snapshot/cache paralelo.
class QuerySource final : public World::ITerrainAttributeQuerySource
{
public:
    QuerySource(float& scale, const Data::Terrain::TerrainAttributes& attributes)
        : m_scale(scale), m_attributes(attributes)
    {
    }
    float ReadScale() override
    {
        return m_scale;
    }
    const Data::Terrain::TerrainAttributes& Attributes() override
    {
        return m_attributes;
    }

private:
    float& m_scale;
    const Data::Terrain::TerrainAttributes& m_attributes;
};

bool LightCoordinateRepresented(float value)
{
    const float scaled = static_cast<float>(static_cast<double>(value) * static_cast<double>(LightCellsPerUnit));
    if (!std::isfinite(scaled))
        return false;
    const double center = std::trunc(static_cast<double>(scaled));
    // AddTerrainLight incrementa el último índice antes de salir del for.
    return center >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) + PrefixRadius &&
           center <= static_cast<double>(std::numeric_limits<std::int32_t>::max()) - PrefixRadius - 1;
}
} // namespace

FaunaEnvironmentAccess::FaunaEnvironmentAccess(WorldState& world, Render::Models::ClothWorld& cloth,
                                               World::WorldTerrain& terrain, World::TerrainLighting& dynamicLight,
                                               FaunaEnvironmentInputs inputs)
    : m_world(world), m_cloth(cloth), m_terrain(terrain), m_dynamicLight(dynamicLight), m_inputs(inputs)
{
}

std::int32_t FaunaEnvironmentAccess::Random()
{
    return m_cloth.random.Next();
}
std::int32_t FaunaEnvironmentAccess::Map() const
{
    return m_world.Runtime().map;
}
std::int32_t FaunaEnvironmentAccess::Mode() const
{
    return m_inputs.mode;
}
const std::uint8_t& FaunaEnvironmentAccess::HeroFlag() const
{
    return m_inputs.heroFlag;
}
Core::Expected<float, FaunaEnvironmentError> FaunaEnvironmentAccess::WorldTime() const
{
    const auto& time = m_world.Runtime().worldTime;
    if (!time)
        return Core::Unexpected(FaunaEnvironmentError::MissingWorldTime);
    return *time;
}
Core::Expected<std::reference_wrapper<const glm::vec3>, FaunaEnvironmentError>
FaunaEnvironmentAccess::ActorPosition() const
{
    const std::size_t index = m_world.Runtime().hero;
    const auto actors = m_world.Actors();
    if (index >= actors.size())
        return Core::Unexpected(FaunaEnvironmentError::InvalidHeroIndex);
    return std::cref(actors[index].position);
}
std::size_t FaunaEnvironmentAccess::TileIndex(std::int32_t x, std::int32_t y) const
{
    // 5f82a0-contract: yByte*256+xByte; CellIndex ya representa esa cuenta.
    return Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));
}
Core::Expected<std::reference_wrapper<const std::uint16_t>, FaunaEnvironmentError>
FaunaEnvironmentAccess::TileAttributes(std::size_t index) const
{
    if (!m_terrain.attributes)
        return Core::Unexpected(FaunaEnvironmentError::MissingAttributes);
    if (index >= m_terrain.attributes->cells.size())
        return Core::Unexpected(FaunaEnvironmentError::InvalidCellIndex);
    return std::cref(m_terrain.attributes->cells[index]);
}
Core::Expected<std::uint32_t, FaunaEnvironmentError> FaunaEnvironmentAccess::QueryAttributes(float x, float y) const
{
    if (!m_terrain.attributes)
        return Core::Unexpected(FaunaEnvironmentError::MissingAttributes);
    QuerySource source(m_inputs.queryScale, *m_terrain.attributes);
    const auto result = World::TerrainAttributeQueryAt(source, x, y);
    if (!result.HasValue())
        return Core::Unexpected(FaunaEnvironmentError::UnrepresentedQueryAddress);
    return result.Value();
}
Core::Expected<double, FaunaEnvironmentError> FaunaEnvironmentAccess::Height(float x, float y) const
{
    // Mismo contexto explícito de LeafTerrainWorld/LeafHeightAt: scene no es Mode ni map.
    const std::int32_t scene = m_inputs.scene;
    if (scene == 0 || scene == 1 || scene == 3)
        return 0.0;
    if (!m_terrain.attributes)
        return Core::Unexpected(FaunaEnvironmentError::MissingAttributes);
    if (!m_terrain.heights)
        return Core::Unexpected(FaunaEnvironmentError::MissingHeights);
    return World::TerrainHeightAt(*m_terrain.heights, *m_terrain.attributes, x, y, m_inputs.fixedHeight);
}
Core::Expected<void, FaunaEnvironmentError> FaunaEnvironmentAccess::PrefixLight(float x, float y)
{
    if (&m_dynamicLight == m_terrain.lighting.get())
        return Core::Unexpected(FaunaEnvironmentError::AliasedBaseLight);
    if (!LightCoordinateRepresented(x) || !LightCoordinateRepresented(y))
        return Core::Unexpected(FaunaEnvironmentError::InvalidLightCoordinates);
    World::AddTerrainLight(m_dynamicLight, x, y, PrefixColor, PrefixRadius);
    return {};
}
} // namespace Client::Scenes
