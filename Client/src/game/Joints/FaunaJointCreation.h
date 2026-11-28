#pragma once

#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct ClothWorld;
class IModelLibrary;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

// 0x530860: ownerContext is a durable FaunaObject except in the 613/4, 614/10 and 614/14, where it stays opaque, and in
// the 618/0x11 and 610/4 of what hangs from a player, the full set's 60f/0, 60f/0x12 and 610/7, the quest reward's
// 60f/0 and 60f/0xa (0x4ddb80), its 5ac/3's 619/9, the pet decal's 619/1 (0x518900) and the charged skill's 619/8
// (0x5189f0), where it is the character's ActorState, as in the Crywolf altar's 618/2 and /3. With `ownerKind` at
// Helper it is the dark horse's HelperState (0x444740), and at Raven the raven's DarkSpirit (0x418390 and 0x418400),
// only for the 60f/0 and 60f/0x12: it reads its light (+0xf4) at creation and its action (+0x111) in the step; with
// another type it stops before writing (0x530860). ModelEffect identifies the durable owner of the 619/1 and 610/13,
// without an ActorState alias. A full pool reads neither the arguments nor the RNG.
namespace Client::Joints
{

// The 618/0 and 618/1 that the killer casts from the target (0x505169, 0x5051a4): owned by the attacker's ActorState,
// they turn for 20 steps and then return to it (0x538313), with the dynamic light, the 0x5d5/0 ray each step and, on
// arrival, the 0x5d5/1 and sound 0x23.
inline constexpr std::int32_t KillBeamJoint = 0x618;

[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateFaunaJoint(std::span<Kernel::JointState> joints, const Kernel::JointRequest& request, Crt::CrtRandom& random,
                 Kernel::JointOwnerKind ownerKind = Kernel::JointOwnerKind::ByType,
                 const JointKernels::CreationServices* services = nullptr);
// Kernel::CreateJointFunction, what the composition root lends the model effects (WorldState::LendJointCreation).
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string> CreateJoint(std::span<Kernel::JointState> joints,
                                                                                  const Kernel::JointRequest& request,
                                                                                  Crt::CrtRandom& random,
                                                                                  Kernel::JointOwnerKind ownerKind);
// 0x535840: the 0x697 and 618/4 follow the owner's +0x40; the 610/8 follows position with z 300; the 10f/3 uses
// life/RNG, the sine and the owner's color. The 614/10 and 614/14 turn and iterate their tail without reading the
// owner; the 618/0x11 follows the character's +0x40 and the 610/4 goes from its +0x17c to its position; the 60f/0, the
// 60f/0xa (which falls) and the 60f/0x12 turn around their target and, depending on the owner's action (the character's
// or the horse's), become the 60f/1, which only fades; with the width at 40 or more they do not read it (the horse's
// 60f/0 and the reward's are created with 40); the 610/7 sways from its start and the 619/9 turns around the
// character's position, read live; the 619/1 speeds up and turns toward the hero, 120 higher, and in its last life
// leaves the 0x5fd sprite (it needs the RNG and the sprites); the 619/8 speeds up, turns toward its target with more
// than 10 life, fades with less than 6 and leaves the 0x5ac sprite each step (it needs the sprites, not the RNG); only
// the arrival with width 50 needs particles, and the smoke uses worldClock. The overload without RNG keeps the 0x535f18
// boundary after the move and the owner check. Chaos Castle's 615/2 needs the frame sprites (0x5491e0) and the
// particles. The arrival's 615/7 shares the geometry without those emissions. The 618/2 and /3 follow the owner's two
// points and emit 0x5d5/0 with the clock's sine.
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<Kernel::JointState> joints);
// The 618/0 and /1 also need the dynamic light (`lighting`, 0x7b4ccc4) and sound 0x23 (0x41ed10) on arrival; the
// 613/0 and /5, the terrain height (`terrainHeight`, 0x5f93c0); the 0x10f/4 and /9, the 400 characters (`actors`,
// 0x743a16c); the 0x60f/0xb and /0x19 with skill 1, the owner arrow's hit (`effectHit`, 0x520170); the 0x613/0x13, the
// table of effects with a model (`effects`, 0x7491fd8). The 0x610/6 of Kalima's 0x95 needs the dynamic light; the
// siege pulse's 0x615/6, the terrain height and the particles; the 0x632/5, the sprites, the terrain height and the
// effect table (the 0x6eb/4 wave on reaching the ground); the 0x60f/4, /0xc and the 0x62b/4, /0xc take another whole
// step while life is not a multiple of 12 (0x53c3c6); the 0x6e8/0 and /1 do not move (the 0x53585e list) and the
// 0x61c/1 follows its owner effect. The 0x5ac appends its segments without moving with the common step; the 0x6ee/0x12
// writes the alpha and position of its 0x125 owner (0x539494); the /0xe and /0xf of the 0x60f and 0x62b with an owner
// need the RNG; the 0x61c/0, the RNG and the particles (and stops with an owner where it looks for the hero,
// 0x53b548); the 0x14f, the RNG, the dynamic light with width 50 or more and the particles on arrival with 50. The
// 0x613 and 0x6ee /0xb, /0xc, /0xe, /0xf, /0x10 and /0x12 need the RNG and the particles, the 0x613/0xf writes the
// angles, alpha and position of its 0x126 owner (0x539249) and the 0x61c/0 with an owner needs the hero (`hero`,
// [0x743a170], 0x53b548): it stops only with the hero as owner. The 0x614/0xc and the 0x62c/5..7 with an owner need the
// shared models (`world`, 0x5769ad8), and the 0x62c/5..7, the sprites. The 0x613/0, /5 and /0x13 of width 80 release
// the 0xd0 into the effect table and need `volley` (the siege, the attributes of 0x5f82c0 and the hero's targets,
// 0x539a50). The hero's 0x61c/0 also needs `volley` (0x53b569); the 0x632/0 and /1 with an owner, on reaching the
// ground, pose the 0x20b from `models` with the shared model from `world` and release the 0x137 shells of its vertices
// into the effect table (0x53c1c6).
[[nodiscard]] Core::Expected<void, std::string>
StepJoints(std::span<Kernel::JointState> joints, Crt::CrtRandom& random, Models::ClothWorld* cloth = nullptr,
           Particles::Particles* particles = nullptr, float worldClock = 0, Particles::FrameSprites* sprites = nullptr,
           Terrain::TerrainLighting* lighting = nullptr, const std::function<void(Audio::SoundId)>& soundRequest = {},
           const std::function<double(float, float)>& terrainHeight = {},
           std::span<const Kernel::ActorState> actors = {}, const JointKernels::EffectHit& effectHit = {},
           Effects::ModelEffects* effects = nullptr, const Kernel::ActorState* hero = nullptr,
           Kernel::WorldState* world = nullptr, const JointKernels::VolleyServices* volley = nullptr,
           const Models::IModelLibrary* models = nullptr);
} // namespace Client::Joints
