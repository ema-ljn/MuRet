#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Actors/ActorPickingBox.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Models
{
struct ClothWorld;
class IModelLibrary;
struct SkinningBuffers;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
struct TerrainPolygon;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::MapScripts::Kalima
{
class KalimaWorld;
} // namespace Client::MapScripts::Kalima

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Actors
{
struct FullSetScan;
class IEffectHooks;
} // namespace Client::Actors

namespace Client::Kernel
{
struct PoseBuffer;
struct WorldRuntime;
class WorldState;
} // namespace Client::Kernel

namespace Client::Guild
{
class IGuildMarks;
} // namespace Client::Guild

// The characters in the game's draw (0x6374dd, 0x510d00), as far as MuRet has them. Contract: 06 § El dibujo del juego.
namespace Client::Actors::Drawing
{
class CharacterCloth;
class ICharacterDrawHooks;
struct DrawnCharacter;
struct ItemDrawContext;

// The resources of the 0x510d00 pass in the game. The light is the dynamic buffer, not its shaded base. The two picking
// slots start at −1 in the .data (0x6c8e30 and 0x6c8e2c); the game passes the ones 0x59ec80 left in the previous draw
// (PickInTheFrame). The cloths belong to each slot between frames. What hangs (GameHanging.h) is posed in the global
// pose buffer and drawn with the globals of 0x60b3d0.
struct GameCharacterPass
{
    const Terrain::TerrainLighting* dynamicLighting = nullptr;
    Models::ClothWorld* clothWorld = nullptr;
    Core::Duration frameClock{};
    int originalFrames = 1;
    std::int32_t hovered = -1;
    std::int32_t targeted = -1;
    std::span<CharacterCloth> cloths{};
    Particles::FrameSprites* sprites = nullptr;
    // The global pose buffer (0x68b181c) and the globals of 0x60b3d0 the frame's owner lends.
    Kernel::PoseBuffer* poseCarry = nullptr;
    // The frame's joints (0x530860) and particles (0x53fe70), for what hangs and the full set, and the model effects
    // (0x518cf0), for the 0x5ac/3 of the +0x35c counter.
    std::span<Kernel::JointState> joints{};
    Particles::Particles* particles = nullptr;
    Effects::ModelEffects* effects = nullptr;
    const ItemDrawContext* itemContext = nullptr;
    Models::OriginalFramePace pace = Models::OriginalFramePace::ByTheClock;
    std::function<void(std::uint16_t, std::span<const Math::BoneTransform>, const Models::ObjectTransform&, bool)>
        onPose{};
    // Publishes the intermediate draw's matrices to the buffer the caller lends, without advancing the state.
    bool publishProjectedPose = false;
    std::span<ActorPickingBox> pickingBoxes{};
    std::function<std::span<const Math::BoneTransform>(std::uint16_t)> poseBones{};
    // The world's models, where 0x618970 loads Chaos Castle's Angel (0x1f7): in the original, the same global array.
    // Without them, a player in maps 18 to 23 stops.
    const Models::IModelLibrary* worldModels = nullptr;
    // This draw's trapezoid (0x5fba90), which the raven looks at (0x4182f0), and the camera's fixed height (0x6c9d5c)
    // for the terrain height under its shadow (0x5f93c0).
    const Terrain::TerrainPolygon* polygon = nullptr;
    float fixedHeight = 1200.0f;
    // The terrain of that height and of the gates' TerrainWall; without it, an empty one. The selection has its own.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The guild marks (0x5f16a0) 0x50d533 draws on the players with a guild; without them, one with the mark stops.
    Guild::IGuildMarks* guildMarks = nullptr;
    // What the monsters' helpers touch (0x43c150, Scenes/KalimaSiegeHelpers.h): this frame's Kalima world (0x723c48,
    // which 0x437860 perturbs; without it, nothing happens) and the camera shake (0x7d325c8). In an intermediate draw,
    // the projection's.
    MapScripts::Kalima::KalimaWorld* kalima = nullptr;
    float* cameraShake = nullptr;
    // The models a creation loaded for the monsters and NPCs (Assets::AssetsState::fauna), where their pose leaves
    // its key. nullptr: no monster or NPC is drawn.
    Assets::FaunaModels* faunaModels = nullptr;
    // What the characters' draw poses and skins into, one character after another, kept by the frame's owner. Without
    // them, each character's draw uses buffers of its own.
    Models::SkinningBuffers* drawBuffers = nullptr;
    // What the draw asks of other modules (Session::CharacterDrawHooks). nullptr: a monster or an NPC stops, and so
    // does a joint of what hangs, of the full set or of the raven.
    const ICharacterDrawHooks* hooks = nullptr;
    // Who creates the model effects (0x518cf0) of the full set, the actions, the guild mark and the raven
    // (Effects::ActorEffectHooks). nullptr: those stop.
    const IEffectHooks* effectHooks = nullptr;
};

// The ordinary player's body, its terrain light and the hero's override, reusing DrawCharacter, then what hangs with
// DrawGameHanging (0x50db57..0x50e9fa): the weapon on the back, the wings, Helper02 and the hands; the player's effects
// with DrawGameCharacterEffects (0x50ea20..0x510ab6) and the raven with DrawGameRaven (0x420df0). Monsters and NPCs (a
// type other than the 0x20a) go through GameCharacterPass::hooks (DrawTheMonster), with their model from
// GameCharacterPass::faunaModels. What is not ported yet stops with its address: the cases by +0x376 and by type, those
// of 0x509340 with joints or particles and what the effects create. Between original frames, what hangs and the effects
// are drawn on a copy of the character and of the rand(): nothing advances or is spent.
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacters(Render::ModelBatch& batch,
                                                                   const Models::IModelLibrary& models,
                                                                   Kernel::WorldState& world,
                                                                   const GameCharacterPass& pass);

// 0x50ba30(character, character, 0) with a character outside the pool, the cards' portrait one (0x4959d5): the same as
// DrawGameCharacters with a slot, without an outline, with `cloth` as the cloth, without being the hero and with
// ActorPoolSize as the owner of what it creates. With its +0x3e4, without the shadow or 0x505820. `originalFrame` as in
// DrawGameCharacters.
[[nodiscard]] Core::Expected<void, std::string>
DrawCharacterOutsideThePool(Render::ModelBatch& batch, const Models::IModelLibrary& models, Kernel::WorldState& world,
                            Kernel::ActorState& actor, CharacterCloth& cloth, const GameCharacterPass& pass,
                            bool originalFrame);

// 0x50db57..0x510a7a on a 0x20a with the body, the parts and the cloth already drawn (DrawCharacter with
// CharacterDrawPass::Body), in the login (scene 2) and in the selection (scene 4), which run the same 0x50ba30 as the
// game: the body in its own bones and, with the alpha from 0.01, the scale and the position in the shared model
// (0x6001b0, Q-C412), what hangs (DrawGameHanging) and the player's effects (DrawGameCharacterEffects), without
// 0x510ef0, which only runs in scene 5, and the raven (0x510abd → 0x420df0) with the trapezoid of `pass` (without it,
// the caller draws it, like the login). `actor.drawnLight` already has the light of what hangs (+0x3b4, 0x50db94);
// `scan` is what 0x505820 returned in this draw. It reads from `pass` the global flow (clothWorld), the global pose
// buffer, the sprites, the joints, the particles, the effects, the items and, for the raven, the dynamic light, the
// terrain and its fixed height. Between original frames, on a copy of the character and of the rand().
[[nodiscard]] Core::Expected<void, std::string>
DrawCharacterTail(Render::ModelBatch& batch, const Models::IModelLibrary& models, Kernel::WorldState& world,
                  Kernel::ActorState& actor, std::size_t slot, const DrawnCharacter& character,
                  const GameCharacterPass& pass, const FullSetScan& scan, bool originalFrame);

// 0x50ba9e..0x50bae0: the +0x376 that 0x50ba30 sends to the cloth on the mesh (0x50bae7, the table of 0x510adc over
// 0x510ad4): 0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82 and 0x8f. With any type, the 0x20a too.
[[nodiscard]] bool WearsClothOnTheMesh(std::uint16_t monster);

// 0x50bf45..0x50c904: what 0x50ba30 adds to the body by the +0x376, with any type (the 0x20a too): the shine passes of
// 0x50c671 (0x26, 0x2b, 0x34, 0x3b, 0x43 and 0x4e to 0x53), the sprites and particles of the 0x45 (0x50bf98), the 0x46
// (0x50c0fb), the cloths of the 0x47, the 0x4a and the 0x12d (0x50c59f), the 0x49 and the 0x4b (0x50c36b), the 0x4d
// (0x50c177), the shine of the 0x35 and the 0x36 (0x50c71b), the weapons hung by the 0x2a (0x50c762) and by the 0x84 to
// 0x86 (0x50c83b) and the shine of the 0x87 (0x50c8ce). With one of them, it stops with its address. A monster's draw
// (Monsters::Drawing::DrawGameMonster) draws the ported ones before and calls this with the others; the 0x20a's path
// stops with all of them.
[[nodiscard]] Core::Expected<void, std::string> CheckMonsterDecorations(std::uint16_t monster);

// 0x50db57..0x50dc58 without its rand(): the light of what hangs (+0x3b4). With +0x375 from 6, (1, 0.1, 0.1).
// Otherwise the terrain's (that of 0x50cc55, or 0.4 in scene 4) plus its own (+0xf4), and in Blood Castle (maps 10 to
// 17: map − 9 between 1 and 8) with the +0x376 in 0x56..0x59, 0x5c..0x5f, 0x73..0x76, 0x79..0x7c, 0x7f..0x82 or
// 0x8a..0x8f, if (map − 9) / 3 (signed) is not 0, (that quotient · 0.5, 0.1, 0.1).
[[nodiscard]] glm::vec3 HangingLightOf(const Kernel::ActorState& actor, const glm::vec3& terrainLight,
                                       std::int32_t map);

// The shadow's floor in 0x50ba30: 0x50bc77..0x50bcbd with a player (the 0x20a, after its shadow's guards and before
// 0x60cf70 at 0x50bcec) and 0x50be15..0x50be52 with a monster or an NPC. In Blood Castle (maps 0xb to 0x11), with
// +0x1a1 of the object and dying (+0x389), the terrain height (0x5f93c0 on the terrain of `pass`, or an empty one, with
// its fixed height) stored as a float, unless the terrain is at the z or above (the fcom with `test ah, 1`: with the z
// or the height NaN it writes it too). Otherwise the z of `actor`. Without the heights, it stops at `address` (the
// call to 0x5f93c0).
[[nodiscard]] Core::Expected<float, std::string>
ShadowGroundOf(const GameCharacterPass& pass, const Kernel::ActorState& actor, std::int32_t map, std::uint32_t address);

// The shadow guards of 0x50ba30 that look at the object and the map (0x50bbff..0x50bc71, DrawnCharacter::castsShadow):
// the alpha (+0x174) from 0.5 (not with NaN), without +0x3e4 (the cards' portrait) and outside map 10, Kalima (24 to
// 29) and 36. The game (CharacterToDraw), the login (LoginScene::TheCharacterToDraw, 0x635206 → 0x510d00) and the
// selection (0x634851 → 0x510d00) ask for them, with the pool's actor; the one for the pets that are ridden is
// DrawsTheShadow. Without Chaos Castle's fade of 0x6001bc..0x6001e1 (P-C535).
[[nodiscard]] bool CastsShadow(const Kernel::ActorState& actor, std::int32_t map);

// 0x510d00: the CtlCode bit (+0x1d8) with which the hero is not drawn in scene 5.
inline constexpr std::uint8_t UndrawnHeroControlBit = 0x04;

// Variant without drawing resources: walks the pool without rand and keeps the stop with one alive and seen. With one
// other than the hero, the siege branch (0x4294b0 reads 0x7239ec, which MuRet leaves at 0, P-C41 c) does not run. The
// hero (`hero`) with +0x1d8 & 4 is not drawn: 0x510e57 sets its +0x13c to 1,000 and +0x148, +0x158 and +0x168 to 1. The
// others, alive and seen (+0x16c), need the resources of the previous overload: it stops there (0x510ea1), with the
// earlier ones already looked at. At the end, 0x42da60 (FinishTheCastleFrame): 0x7239dc and 0x7239e0 at 0 and, with
// 0x7239d9 at 0 (only the 277 of map 30 sets it to 1, 0x42db10), Pathfinding::OpenTheGates on `walls` (Q-C276); then
// 0x7239d9 at 0. Without `runtime`, as with the three at 0.
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacters(std::span<Kernel::ActorState> actors,
                                                                   std::size_t hero,
                                                                   Data::Terrain::TerrainLayer<std::uint16_t>& walls,
                                                                   Kernel::WorldRuntime* runtime = nullptr,
                                                                   std::span<ActorPickingBox> pickingBoxes = {});

// 0x42da60, at the end of 0x510d00.
void FinishTheCastleFrame(Kernel::WorldRuntime* runtime, Data::Terrain::TerrainLayer<std::uint16_t>& walls);
} // namespace Client::Actors::Drawing
