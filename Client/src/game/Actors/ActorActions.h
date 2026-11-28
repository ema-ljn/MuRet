#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Actors/ActionSend.h"
#include "Kernel/SceneId.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

// A character's actions: the standing one, 0x4f2250 (MuMain's SetPlayerStop).
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Caminar" (SetPlayerStop) and P-C14 c.
namespace Client::Actors
{

// What 0x4f2250 reads and writes outside the character. The caller lends it for the call.
struct ActorStopContext
{
    // The current world (DAT_006c9dd0) and the Fenrir latch (0x7d3a0d0, movementSpeed.itemLatch).
    Kernel::WorldRuntime& runtime;
    // DAT_006d5dc8: the flying maps look at the cell only in scene 5 (0x4f23a2).
    Kernel::SceneId scene;
    // The actions of Player.bmd: 0x4ea030 does not set one the model lacks.
    const Models::Skeleton& playerSkeleton;
    // *[0x7758550], Item.bmd in memory: the weapon matrix reads byte +0x1e of the weapon's record
    // (0x4f2c52). nullptr if it was not loaded: the matrix stops with an error when it reads it.
    const Tables::ItemTable* items = nullptr;
    // The CRT's rand(): the suffix always draws one, and Fenrir up to two more (0x4f36be..0x4f3731).
    Crt::CrtRandom& random;
    // 0x4f23c1: reads the visible cell on the flying maps of scene 5.
    const Data::Terrain::TerrainLayer<std::uint16_t>* terrainWalls = nullptr;
    // The C1 18 of sitting down (0x4f2416..0x4f2bea). Empty, sitting down stops with an error after setting 0xca.
    ActionSender sendAction{};
    // This actor's skeleton; nullptr stands for a model record without actions.
    const Models::Skeleton* actorSkeleton = nullptr;
    // +0xaa of the model record, the first sounds the suffix reads for non-players and subtypes 209 to 211.
    std::span<const std::int16_t> modelSounds{};
};

// The ID 0x41ed10(id, character, 0) emits in the suffix of 0x4f2250 (0x4f37a0).
// It stays available for diagnostics after SetActorStop dispatched it.
struct ActorStopResult
{
    std::optional<Audio::SoundId> sound;
};

// 0x4f2250: always sets Run to 0. The player picks its action by pet, safe zone, weapons, wings and map; the others
// read the logical cell Y and then X, and type 0x172 checks TerrainWall to pick 7 or 0. Then each path runs the
// rand() suffix. Sitting (+0x7c with 0x200000), the player asks for 0xca and the C1 05 18 with its direction and
// action 0x82, or nothing if it already has it; then the suffix runs. Error if a dependency it consults is missing:
// TerrainWall for the cases that read it, sendAction when sitting down, or the sound table when the suffix's gate
// opens.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> SetActorStop(Kernel::ActorState& actor,
                                                                        const ActorStopContext& context);
} // namespace Client::Actors
