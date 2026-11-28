#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Math/HalfLifeMath.h"
#include "Actors/FullSetScan.h"
#include "Scenes/GameDrawTail.h"
#include "Social/SocialState.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Terrain
{
struct TerrainLighting;
struct TerrainPolygon;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::World
{
class FrameSprites;
struct ModelEffect;
class ModelEffects;
class Particles;
} // namespace Client::World

namespace Client::Models
{
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models

namespace Client::Kernel
{
struct ActorState;
struct DarkSpirit;
struct PoseBuffer;
class WorldState;
} // namespace Client::Kernel

// A player's effects in the game's draw: the tail of 0x50ba30 after what hangs, from the 0x20a case of the switch of
// 0x50e9fa (0x50ea20) to the return, and the raven of 0x420df0. What asks for an effect, a joint, a particle or a sound
// MuRet does not have stops with its address, and only when the branch would really run. Contract: 06 § El dibujo del
// juego, "Los efectos del jugador".
namespace Client::Actors
{
class IEffectHooks;
class IJointHooks;
} // namespace Client::Actors

namespace Client::Actors::Drawing
{
struct DrawnObject;

struct GameCharacterEffectsPass
{
    // The body's bones for this frame: the character's own buffer (+0x120).
    std::span<const Math::BoneTransform> body;
    // The global rand() of 0x69fd70.
    Crt::CrtRandom& random;
    // The frame's sprite table (0x5491e0). Needed only if a branch puts sprites.
    World::FrameSprites* sprites = nullptr;
    // The frame's particles (0x53fe70), joints (0x530860, born through `jointHooks`) and model effects (0x518cf0), for
    // what the full set creates. Needed only if a branch creates, in an original frame.
    World::Particles* particles = nullptr;
    std::span<Scenes::JointState> joints{};
    const IJointHooks* jointHooks = nullptr;
    World::ModelEffects* effects = nullptr;
    // Who creates those model effects (0x518cf0). Without it, they stop there.
    const IEffectHooks* effectHooks = nullptr;
    // The world, for the 0x518cf0 additions of the Dark Lord's actions 0x51 and 0x5b (the 0x621/2 of 0x50f9b3 and the
    // 0x117/2 of 0x50fb64) and their sound 0x1cc. Without it, those windows stop.
    Kernel::WorldState* world = nullptr;
    // The character's place in the pool: the owner's identity (+0x40) of the 0x60f/0 orbit.
    std::size_t slot = 0;
    // What 0x505820 returned in this draw (0x50c904) and what it left in [0x743a178].
    FullSetScan scan;
    // The party's places up to its count (PartyMembers): the Dark Lord's actions 0x51, 0x5b and 0x94 read them with the
    // frame below 2 (0x50f9c9).
    std::span<const Scenes::PartySlot> party{};
    // The 400 characters (0x743a16c): the party members the 0x618 joints of those actions go to (0x50fa25). Without
    // them, they stop.
    std::span<const Kernel::ActorState> actors{};
    // DAT_05767b88: the world clock in ms, as a float.
    float worldClock = 0.0f;
    std::int32_t map = 0;        // 0x6c9dd0
    std::int32_t logicSteps = 0; // 0x7d3a0f0
    // 0x510a80: the scene (0x6d5dc8) is 5. In the login and the selection, 0x510ef0 does not run.
    bool gameScene = true;
    // In an original frame the counters run and what is born in the durable tables; between frames, only the sprites,
    // on a copy of the character and of the rand().
    bool originalFrame = true;
    // +0x6c..+0x74 of the 0x20a ([ebp−0x14] of 0x50ba30): the position 0x6001b0 copied to it at 0x50bbf7, before the
    // guards and the floor of Blood Castle's shadow (0x50bcaf). 0x4ec110 translates the bones' points with it
    // (0x50eab0, 0x50f89b, 0x50fb16, 0x50fc37.., 0x50fe4c.., 0x510071.. and the guild's 0x51e121); the joints and
    // effects born on the character go from the object's (+0x10), already on the floor. Without it, the character's.
    std::optional<glm::vec3> modelOrigin{};
};

// 0x50ea20..0x510ab6 on a 0x20a with what hangs already drawn, in the ASM's order: the sprites of the wizard's special
// skin (0x50ea38, which leaves the offset (−4, 11, 0) for the hands, Q-C350), those of action 0x94 outside the Dark
// Lord and the effects of the windows of actions 0x94, 0x51 and 0x5b (0x50f828; with the frame below 2, a party and the
// character outside it, 0x50f9f8 jumps to 0x510a7a), those of the flags +0x78 & 4 or 0x2000000 (0x50fba4); outside
// Chaos Castle, the counter of +0x35c with +0x358 (0x50fd74, with the 0x5ac/3 effect every 100) and, with the full set
// of 0x505820, the 0x5ac sprites of the hands, the 0x6f1/3 particles of the boots from 0x182f to 0x1833 and, above +9,
// the 0x60f/0 orbit and the 0x60f/0, 0x60f/0x12 and 0x610/7 joints (0x50fddb, with their rand() in order); and at the
// end, only in scene 5 (0x510a80), 0x510ef0 with +0x369. There is no other scene gate: the game, the login and the
// selection call it. Between frames, the sprites, the counter and the rand() calls that choose, on the copy of the
// character and of the rand() the caller gives; what is born in the durable tables, only in an original frame.
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacterEffects(Kernel::ActorState& actor,
                                                                         const GameCharacterEffectsPass& pass);

// What the game's raven reads (0x420df0 → 0x4182f0) outside its owner.
struct GameRavenPass
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    // This draw's trapezoid (0x5fba90) and the dynamic buffer of the terrain's light (0x5ffce0).
    const Terrain::TerrainPolygon& polygon;
    const Terrain::TerrainLighting* lighting = nullptr;
    // The terrain heights of 0x5f93c0, for the shadow, with the camera's fixed height.
    const Terrain::WorldTerrain& terrain;
    float fixedHeight = 0.0f;
    // The global pose buffer (0x68b181c), where 0x5ffdf0 leaves the raven's. Optional.
    Kernel::PoseBuffer* carry = nullptr;
    Core::Duration frameClock{};
    std::int32_t map = 0;
    // The hero's +0x1a1, which 0x5ffdf0 reads in maps 18 to 23.
    std::uint8_t heroByte1a1 = 0;
    bool originalFrame = true;
    // The CRT's rand(), for 0x418920 (states 5 and 6). Without it, those states stop.
    Crt::CrtRandom* random = nullptr;
    // The world's particles (0x743a198) and their clock (DAT_05767b88), for the 0x5ac/6 of the shout (0x418a26).
    // Without them, the shout stops at its first particle.
    World::Particles* particles = nullptr;
    float worldClock = 0.0f;
    // The world's tables for state 5 (0x418aab..0x418c0a: the 0x5ac/1 joints of 0x530860 and the 0x117/3 and 0x113/0
    // effects of 0x518cf0) and for the end of the shout (0x418a8c: the 0x613/0xd joints toward the character of +0x3b0,
    // one of its 400). Without them, they stop at the first addition.
    Kernel::WorldState* world = nullptr;
    // Who creates those joints (0x530860) and effects (0x518cf0). Without them, they stop there too.
    const IJointHooks* jointHooks = nullptr;
    const IEffectHooks* effectHooks = nullptr;
};

// 0x4182f0: whether the raven passes the trapezoid of 0x5fba90 at its (x, y) times 0.01f, with −20. If not, the draw
// sets its speed to 3 and, except in state 4 (0x418375), its state to 0 (MissTheView).
[[nodiscard]] bool DarkSpiritSeen(const Kernel::DarkSpirit& spirit, const Terrain::TerrainPolygon& polygon);
void MissTheView(Kernel::DarkSpirit& spirit);

// What is drawn of the raven (DrawDarkSpirit) between two steps (`stepAlpha` of one more step, D-C4): the 0xc0 with its
// scale, its angles and its position plus what it moved in the last step times `stepAlpha`, the interpolated animation,
// the terrain's light at its place (`terrainLight`) plus its own, the vertex light and its alpha. With `stepAlpha` at
// 0, the step's state: the one the original poses.
[[nodiscard]] DrawnObject DrawnDarkSpirit(const Kernel::DarkSpirit& spirit, const Models::Skeleton* skeleton,
                                          const glm::vec3& terrainLight, float stepAlpha);

// 0x420df0: with a raven in +0x430, 0x4182f0. Without passing the trapezoid (its (x, y) times 0.01f, with −20),
// MissTheView. If it passes, 0x602e90: 0x5ffdf0 (Chaos Castle's alpha and, above 0.01, the pose with the terrain's
// light) and 0x6002e0 (meshes 3 and 2, and the shadow except in maps 10, 24 to 29 and 36); with the owner's +0x78 &
// 0x4000, 0x6002e0 receives 10 and goes through 0x60033c: DrawChromeShell, without the shadow or the terrain height
// (the raven's +0x108 stays at 0 from 0x415c1e, so the siege loop of 0x600343 does not find it in the pool). Then, in
// the original's frames, 0x418920, which only does something in states 5 and 6 (those the A8 sets): 5 releases the
// 0x5ac/1 joints, the 0x117/3 and the 0x113/0 in the tables of `world`, the shout of 6 the 0x5ac/6 particles of its
// bones (DrawTheRavenAttack), and its end, the 0x613/0xd joints of the vertices of its mesh 1 (0x4ee770, EndTheShout).
// It is drawn as the step left it, like the character. In an original frame it keeps the pose's bones for the step's
// spark.
[[nodiscard]] Core::Expected<void, std::string> DrawGameRaven(Kernel::ActorState& owner, const GameRavenPass& pass);
} // namespace Client::Actors::Drawing
