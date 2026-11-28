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
} // namespace Client::Scenes

namespace Client::Combat
{

// A model's sound table (ModelSounds, from 0x612c80), for the tests: without it, WorldRuntime::modelSounds, which each
// monster's creation writes (0x612d90 from 0x5141e0, MonsterSounds.h).
using ModelSoundLookup = std::function<std::optional<Sound::ModelSounds>(std::uint16_t model)>;

// What 0x4f4a10 and 0x4f5660 read: the world (the hero of 0x743a170, the own record, the map and the last requested
// sound), the CRT's rand(), the skeletons for 0x4ea030 (Player.bmd apart, as MoveHero has it) and Item.bmd for the
// weapon's +0x1e mark.
struct ActorCombatContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const Models::Skeleton* playerSkeleton = nullptr;
    const Models::IModelLibrary* models = nullptr;
    const Tables::ItemTable* items = nullptr;
    // Empty: WorldRuntime::modelSounds. Without either, the ten zero WORDs of 0x60dc80 (SoundsOrZeros): 0x4f4a10 draws
    // its rand() and requests 0xd2 (08, P-C253).
    ModelSoundLookup modelSounds{};
    // The current map's terrain (Terrain::TerrainState::current), whose TerrainWall the deaths and the effects read;
    // without it, an empty one, and they stop where they read it.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The dynamic light (Terrain::TerrainState::dynamicLight) the effects add to; nullptr: adding one stops.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
};
[[nodiscard]] ActorCombatContext CombatContextOf(const Scenes::WorldEntryResources& resources);

// 0x4f4a10, SetPlayerAttack: the strike action by the model, the weapons and the mount, the sound with the frame at 0
// (in WorldRuntime::lastSoundId) and +0x38f + 1. Called by the hero's attack on arrival (0x5650c0) and the 0x18's
// actions 0x78/0x79 (0x4cb750). It cuts, with its address, at what belongs to another wave or milestone.
[[nodiscard]] Core::Expected<void, std::string> SetActorAttack(Kernel::ActorState& actor,
                                                               const ActorCombatContext& context);

// 0x4f5660, SetPlayerDie: the own life at 0 for the hero, the death action (0xc3 or 6) and, with the frame at 0, the
// sound (lastSoundId) and the frame at 0.001.
[[nodiscard]] Core::Expected<void, std::string> SetActorDie(Kernel::ActorState& actor,
                                                            const ActorCombatContext& context);
} // namespace Client::Combat
