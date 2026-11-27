#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Actors/ActorCooldownServices.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Protocol/Outgoing.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SceneId.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <span>
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
namespace Client::Render::Text
{
class ITextMeasure;
}
namespace Client::Tables
{
class TextTable;
}

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::World
{
class FrameSprites;
class Particles;
} // namespace Client::World

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// The characters' step (0x508f00, from the game at 0x63710f, the selection at 0x6342f9 and the login at
// 0x634d13): the TerrainWall, whether each one is seen and, for each one, 0x508ea0: 0x508ba0 with those that are not
// the hero, the body (0x4fd5f0) through the path of the still 0x20a and 0x505de0 to its end, with the head that
// draws on those that are not the hero. It stops with an error where the original reads something MuRet does not
// have. Contract: Client/docs/research/06-render-escenas-y-ui.md, "El paso de los personajes", "El paso de los
// personajes en la selección y el login", "El cuerpo de los personajes (0x4fd5f0)" and "Los efectos de los
// personajes (0x505de0)".
namespace Client::Actors
{
class PlayerActionSpeeds;

// 0x5fba90 as 0x508f00 (0x50916d..0x50918c) and 0x508ea0 (0x508eac..0x508ecb) call it: the character's place
// times 0.01f (0x6b88d4), stored as float, with the margin of −20 (0xc1a00000).
[[nodiscard]] bool CharacterSeen(const Terrain::TerrainPolygon& polygon, const glm::vec3& position);

// A character's logical cell: the X of +0x41c and the Y of +0x420.
struct LogicalCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// The logical cell through the protection table, as 0x508f00, 0x505de0 and the camera (0x635ae0) read it:
// the Y and then the X, each in an access that closes and spends 4 rand() if it leaves it at 0.
[[nodiscard]] LogicalCell ReadLogicalCell(Kernel::ActorState& actor, Crt::CrtRandom& random);

struct CharacterStepContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    // DAT_006d5dc8: the selection (4) decides the end of the animation with the frame + 2 (Q-C21), and the 0x607
    // of map 7 of 0x505de0 belongs to scene 5 only (0x506cf8).
    Scenes::SceneId scene = Scenes::SceneId::Game;
    // Player.bmd, the model 0x5769ad8 + 0x20a · 0xe0 of the actions 0x4f9580 and 0x4ec200 read. Without it,
    // the body stops at 0x4f9580.
    const Models::Skeleton* playerSkeleton = nullptr;
    // *[0x7758550], Item.bmd in memory: the helpers of 0x4fc450 and the standing action (0x4f2250) read it
    // with a weapon or an item on. nullptr if it was not loaded: they stop where they read it.
    const Tables::ItemTable* items = nullptr;
    // *[0x76bd1d8], Skill.bmd in memory: the range (0x5526f0) of the loop of skill 0x4c of the attack step
    // (0x50243d). nullptr: it stops there with a target to look at.
    const Tables::SkillTable* skills = nullptr;
    // The skeletons of the other models, for 0x4f5660 (SetActorDie) with the dying one from step 15. nullptr: a
    // model that is not the 0x20a stops there.
    const Models::IModelLibrary* models = nullptr;
    // The load of scene 2 or 4; without it, an empty one. The TerrainWall is written right there.
    const Terrain::WorldTerrain* terrain = nullptr;
    const Models::Skeleton* darkSpiritSkeleton = nullptr;
    World::Particles* particles = nullptr;
    // The frame's sprite table (0x5491e0) that the cases of 0x505de0 ask for (0x166). Without it, they stop there.
    World::FrameSprites* sprites = nullptr;
    // The game's joints (0x74a8d78, GameDrawTailState::joints), for the 0x618/0 and /1 of the killer (0x505169).
    // Empty, the attack step stops there.
    std::span<Scenes::JointState> joints{};
    float worldClock = 0.0f;
    std::int32_t counter = 0;
    // The loose object of 0x7437050 (NewFace 0xa8..0xac), outside the pool's 400.
    const Models::Skeleton* newFaceSkeleton = nullptr;
    // If empty, world's durable overlay. Each Walk writes it before animating that actor.
    PlayerActionSpeeds* actionSpeeds = nullptr;
    float fixedHeight = 1200.0f;
    // Sound requests: the consumer decides how to play them.
    std::function<void(std::int32_t)> soundRequest{};
    std::function<void(std::int32_t, const glm::vec3&)> soundAt{};
    // What the step sends through send() (the raven's A7, 0x416a20). Empty, without the session: it does not go out.
    std::function<void(Protocol::Outgoing)> send{};
    // GetAsyncKeyState(0x11) with 0x80 in the high byte: Ctrl down, which 0x558eb0 reads in the hero's skill
    // (0x558fd6).
    bool controlDown = false;
    // What the NPCs say (0x4fc790, 0x4fc8c0 and 0x4fcda2): the texts of Text.bmd and the balloon's measurer and
    // screen (0x557170). Without them, whoever has to speak stops.
    const Tables::TextTable* texts = nullptr;
    const Render::Text::ITextMeasure* textMeasure = nullptr;
    Core::PixelSize screen{640, 480};
    // timeGetTime() (0x6b8144), in ms, which the name message of 0x6c88b0 reads (0x4fcd82 and 0x4fcd8e). Empty, the
    // one with that name stops.
    std::function<std::uint32_t()> systemTime{};
    // The C1 06 AF 02 x y of 0x4fbee8 (Chaos Castle, the hero 10 s on a cell with bit 8), through the session. Empty,
    // that send stops.
    std::function<Core::Expected<void, std::string>(std::uint8_t, std::uint8_t)> sendChaosCastleReposition{};
    // The ground marks (Terrain::TerrainState::groundMarks) the remains (0x52e96c) and the 0x182 (0x507486) add to.
    // nullptr: whoever adds one stops there.
    Terrain::GroundMarkTable* groundMarks = nullptr;
    // The global trapezoid the last 0x5fb570 left (Camera::CameraState::trapezoid): before the game's first drawing,
    // that of the selection's last drawing (Q-C216). nullptr: the .bss's zeros, as before any 0x5fb570.
    const Terrain::TerrainPolygon* trapezoid = nullptr;
};

// 0x508f00. It stops with an error, in the original's order, without the map loaded
// (0x508f14), in the branches of 0x508ba0 MuRet does not have, in those of 0x4fd5f0 outside the still 0x20a (06, its
// table of stops) and in those of 0x505de0 that create effects or particles MuRet does not have (06, "Los efectos
// de los personajes (0x505de0)"); the hands' lights go to the dynamic buffer of `world`. What it did before
// the stop stays, with the rand() calls it drew.
[[nodiscard]] Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context);

// 0x5069ae..0x506ac6: the creators of the +0x3e5 brake (Actors::StepActorCooldown) with the context's particle pool
// and effect table: the 0x5f3/1 (0x506a73) and the 0xc8/0xc9 /0 pebble without an owner (0x506ab7). They keep
// references to `context`.
[[nodiscard]] ActorCooldownServices CharacterCooldownServices(const CharacterStepContext& context);

// 0x508ea0 for an object outside the pool: it does not erase or mark TerrainWall and it is never the published hero.
[[nodiscard]] Core::Expected<void, std::string> StepCharacter(Kernel::ActorState& actor,
                                                              const CharacterStepContext& context);

// 0x4fd5f0(object, object) and 0x505de0(object, object) in a row, without 0x508ea0: it looks neither at the live byte
// nor at whether it is seen (+0x16c stays as it is) and does not run 0x508ba0. The letters' portrait calls it
// (0x495a20, 0x495dad..0x495db8, and its drawing, 0x495760), with a character outside the pool that is not the hero.
[[nodiscard]] Core::Expected<void, std::string> StepTheBodyAndEffects(Kernel::ActorState& actor,
                                                                      const CharacterStepContext& context);
} // namespace Client::Actors
