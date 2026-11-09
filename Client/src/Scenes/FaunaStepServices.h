#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Client::Render::Models
{
struct ClothWorld;
}
namespace Client::World
{
struct WorldTerrain;
struct TerrainLighting;
} // namespace Client::World

namespace Client::Scenes
{
class WorldState;

enum class FaunaEnvironmentError
{
    MissingWorldTime,
    InvalidHeroIndex,
    MissingAttributes,
    MissingHeights,
    InvalidCellIndex,
    UnrepresentedQueryAddress,
    InvalidLightCoordinates,
    AliasedBaseLight,
};

// Entradas externas obligatorias: no tienen productor acreditado en WorldRuntime.
// Mode:00723c50; heroFlag:Hero+3e2. Scene es distinto de ambos y del mapa.
struct FaunaEnvironmentInputs
{
    std::int32_t& mode;
    std::uint8_t& heroFlag;
    std::int32_t& scene;
    float& queryScale;
    float& fixedHeight;
};

// Subconjunto productivo de servicios de fauna; NO implementa StepBirds/StepFish.
// Todos los owners y los campos de inputs deben vivir más que este objeto. El caller serializa
// las mutaciones y mantiene terreno/actores/buffers vivos hasta terminar cada paso y sus préstamos.
// terrain es el WorldTerrain activo y dynamicLight el buffer mutable7b4ccc4, no la base sombreada.
// No retiene la identidad de inputs ni crea pools, modelos, poses, reloj o RNG.
class FaunaEnvironmentAccess final
{
public:
    FaunaEnvironmentAccess(WorldState& world, Render::Models::ClothWorld& cloth, World::WorldTerrain& terrain,
                           World::TerrainLighting& dynamicLight, FaunaEnvironmentInputs inputs);
    FaunaEnvironmentAccess(const FaunaEnvironmentAccess&) = delete;
    FaunaEnvironmentAccess& operator=(const FaunaEnvironmentAccess&) = delete;
    FaunaEnvironmentAccess(FaunaEnvironmentAccess&&) = delete;
    FaunaEnvironmentAccess& operator=(FaunaEnvironmentAccess&&) = delete;

    [[nodiscard]] std::int32_t Random();
    [[nodiscard]] std::int32_t Map() const;
    [[nodiscard]] std::int32_t Mode() const;
    [[nodiscard]] const std::uint8_t& HeroFlag() const;
    [[nodiscard]] Core::Expected<float, FaunaEnvironmentError> WorldTime() const;
    // Devuelve la posición canónica: cambiar Runtime.hero después no invalida ese préstamo.
    [[nodiscard]] Core::Expected<std::reference_wrapper<const glm::vec3>, FaunaEnvironmentError> ActorPosition() const;
    [[nodiscard]] std::size_t TileIndex(std::int32_t x, std::int32_t y) const;
    [[nodiscard]] Core::Expected<std::reference_wrapper<const std::uint16_t>, FaunaEnvironmentError>
    TileAttributes(std::size_t index) const;
    // 5f82c0 resta el índice; no sustituye al indexador5f82a0 anterior. Conserva EAX completo.
    [[nodiscard]] Core::Expected<std::uint32_t, FaunaEnvironmentError> QueryAttributes(float x, float y) const;
    [[nodiscard]] Core::Expected<double, FaunaEnvironmentError> Height(float x, float y) const;
    // 446120 prefijo: color(-.3,-.3,-.2),radio16. Buffer finito según AddTerrainLight.
    [[nodiscard]] Core::Expected<void, FaunaEnvironmentError> PrefixLight(float x, float y);

private:
    WorldState& m_world;
    Render::Models::ClothWorld& m_cloth;
    World::WorldTerrain& m_terrain;
    World::TerrainLighting& m_dynamicLight;
    FaunaEnvironmentInputs m_inputs;
};
} // namespace Client::Scenes
