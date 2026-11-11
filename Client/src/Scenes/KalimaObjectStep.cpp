#include "Scenes/KalimaObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/SceneId.h"
#include "World/Particles.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t AmbientSound = 0x1d3;
constexpr std::int32_t PeriodicSound = 0x1d4;
constexpr std::int32_t RegionSound = 0x1d9;
constexpr int AmbientPeriod = 100;
constexpr int RegionPeriod = 50;
constexpr int SoundAlternatives = 2;
constexpr int SelectorGate = 10;
constexpr int ParticleGate = 5;
constexpr int ParticleSpread = 800;

void RegionSoundOf(WorldState& world, Render::Models::ClothWorld& cloth)
{
    ActorState& hero = world.Actors()[world.Runtime().hero];
    const auto x = [&]
    {
        const Render::Models::LogicalAccess access(hero.cellXAccesses, cloth.random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        return static_cast<std::int32_t>(hero.cellX);
    }();
    const auto y = [&]
    {
        const Render::Models::LogicalAccess access(hero.cellYAccesses, cloth.random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        return static_cast<std::int32_t>(hero.cellY);
    }();
    constexpr std::int32_t MinX = 25, MaxX = 51, MinY = 44, MaxY = 119;
    if (x >= MinX && x <= MaxX && y >= MinY && y <= MaxY && cloth.logicSteps % RegionPeriod == 0)
        world.Fauna().Sound(RegionSound + cloth.random.Next() % SoundAlternatives, nullptr);
}

void OwnRecordSound(WorldState& world, Render::Models::ClothWorld& cloth)
{
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
    constexpr std::uint8_t LastLevel = 7;
    const auto& hero = world.Actors()[world.Runtime().hero];
    if (KalimaLevelOf(hero.characterClass, world.OwnCharacter().level) == LastLevel)
        RegionSoundOf(world, cloth);
}
} // namespace

std::uint8_t KalimaLevelOf(std::uint8_t characterClass, std::uint16_t level)
{
    // 6b8b18..6b8b80: los siete superiores son999, incluso cuando se solapan.
    // El primer rango que acepta retorna su ordinal; sin ninguno,4379dd retorna7.
    constexpr std::array<std::uint16_t, 14> Lower{40, 131, 181, 231, 281, 331, 350, 20, 111, 161, 211, 261, 311, 350};
    constexpr std::uint16_t Upper = 999;
    constexpr std::uint8_t ClassMask = 7;
    constexpr std::uint8_t Half = 7;
    const auto base =
        static_cast<std::size_t>((characterClass & ClassMask) == 3 || (characterClass & ClassMask) == 4 ? Half : 0);
    for (std::uint8_t index = 0; index < Half; ++index)
        if (level >= Lower[base + index] && level <= Upper)
            return static_cast<std::uint8_t>(index + 1);
    return Half;
}

Core::Expected<std::int32_t, std::string> StepKalimaObjectPrelude(WorldState& world, Render::Models::ClothWorld& cloth)
{
    world.Fauna().Sound(AmbientSound, nullptr);
    if (cloth.logicSteps % AmbientPeriod == 0)
        world.Fauna().Sound(PeriodicSound + cloth.random.Next() % SoundAlternatives, nullptr);
    OwnRecordSound(world, cloth);
    // 438904: cierre propio antes del gate. El count se lee sólo si éste acepta.
    if (cloth.random.Next() % SelectorGate == 0 && world.Runtime().objectSelectionCount != 0)
        return cloth.random.Next() % world.Runtime().objectSelectionCount;
    if (cloth.random.Next() % ParticleGate != 0)
        return std::int32_t{0};
    // 438947..4389ca: ambos sorteos preceden al creador, aun con pool lleno.
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    constexpr double SpreadCenter = 400, ParticleHeight = 50, EffectHeight = 800;
    constexpr int EffectGate = 15;
    const glm::vec3 light{0.3f, 0.8f, 1};
    glm::vec3 position;
    position.x = static_cast<float>((static_cast<double>(cloth.random.Next() % ParticleSpread) + hero.position.x) -
                                    SpreadCenter);
    position.y = static_cast<float>((static_cast<double>(cloth.random.Next() % ParticleSpread) + hero.position.y) -
                                    SpreadCenter);
    position.z = static_cast<float>(static_cast<double>(hero.position.z) + ParticleHeight);
    const auto created = world.Particles().CreateKalimaChecked(0x5ac, position, hero.angles, light, 7, 1.0f, 0.0f,
                                                               cloth.random, static_cast<std::int32_t>(SceneId::Game),
                                                               {World::ParticleOwnerKind::Actor, world.Runtime().hero});
    if (!created.HasValue())
        return Core::Unexpected(std::string("0x4389ca: falló el alta5ac/7 del héroe"));
    if (cloth.random.Next() % EffectGate == 0)
    {
        position.z = static_cast<float>(static_cast<double>(hero.position.z) + EffectHeight);
        static_cast<void>(world.Map().effects.CreateKalimaEffect(position, glm::vec3(0), light, cloth.random));
        constexpr std::int32_t EffectSound = 0x1d7;
        world.Fauna().Sound(EffectSound, nullptr);
    }
    return std::int32_t{0};
}
} // namespace Client::Scenes
