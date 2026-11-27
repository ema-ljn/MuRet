#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Sound/MonsterSounds.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models
namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Scenes
{
struct WorldEntryResources;

// La tabla de sonidos de un modelo (ModelSounds, de 0x612c80), para los tests: sin ella, la de
// WorldRuntime::modelSounds, que escribe el alta de cada monstruo (0x612d90 desde 0x5141e0, MonsterSounds.h).
using ModelSoundLookup = std::function<std::optional<Sound::ModelSounds>(std::uint16_t model)>;

// Lo que leen 0x4f4a10 y 0x4f5660 (CL9 hito 3): el mundo (el héroe de 0x743a170, el registro propio, el mapa y el
// último sonido pedido), el rand() de la CRT, los esqueletos para 0x4ea030 (Player.bmd aparte, como lo tiene MoveHero)
// e Item.bmd para la marca +0x1e del arma.
struct ActorCombatContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const Models::Skeleton* playerSkeleton = nullptr;
    const Models::IModelLibrary* models = nullptr;
    const Tables::ItemTable* items = nullptr;
    // La tabla de sonidos del modelo (ModelSounds); vacía, la de WorldRuntime::modelSounds. Sin ninguna de las dos, las
    // diez WORD en 0 de 0x60dc80 (SoundsOrZeros): 0x4f4a10 saca su rand() y pide el 0xd2 (08, P-C253).
    ModelSoundLookup modelSounds{};
    // The current map's terrain (Terrain::TerrainState::current), whose TerrainWall the deaths and the effects read;
    // without it, an empty one, and they stop where they read it.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The dynamic light (Terrain::TerrainState::dynamicLight) the effects add to; nullptr: adding one stops.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
};
[[nodiscard]] ActorCombatContext CombatContextOf(const WorldEntryResources& resources);

// 0x4f4a10, SetPlayerAttack: la acción de pegar según el modelo, las armas y la montura, el sonido con el frame en 0
// (en WorldRuntime::lastSoundId, para la CL10) y +0x38f + 1. La llaman el ataque del héroe al llegar (0x5650c0) y las
// acciones 0x78/0x79 del 0x18 (0x4cb750, hito 1). Corta en lo que es de otra ola o de otro hito, con su dirección.
[[nodiscard]] Core::Expected<void, std::string> SetActorAttack(Kernel::ActorState& actor,
                                                               const ActorCombatContext& context);

// 0x4f5660, SetPlayerDie: la vida propia en 0 si es el héroe, la acción de morir (0xc3 o 6) y, con el frame en 0, el
// sonido (lastSoundId) y el frame en 0,001.
[[nodiscard]] Core::Expected<void, std::string> SetActorDie(Kernel::ActorState& actor,
                                                            const ActorCombatContext& context);
} // namespace Client::Scenes
