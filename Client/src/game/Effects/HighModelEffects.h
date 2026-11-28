#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// CL11, the high types' batch: the 0x518cf0 creations of the types that leave the 9..0x113 switch by other comparisons
// (0x518e3c..0x518f31, 0x51e349 and 0x51f47f) and their 0x5208a0 steps (0x525e6e, 0x52a688 and 0x52b56d), with the
// prefix of Effects::ModelEffects::Begin. 06, "Los efectos con modelo de los tipos altos". These types' draw goes
// through 0x52d620's common path (0x52da84 → 0x602e90, GameDrawTail) or is not drawn: those from 0x151 on, except
// 0x5e8..0x5ea and 0xc1a, have no case in 0x52d620 or in the floor pass (0x52dc20).
namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models

namespace Client::Effects
{
struct ModelEffect;
struct ModelEffectStepServices;
} // namespace Client::Effects

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ModelEffectArguments;
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Effects
{

// What these types' creations read and write outside their slot: the effects table (Effects::ModelEffects), the joints
// (0x530860, GameDrawTailState::joints), the particles (0x53fe70), the sound (0x41ed10, which stays in WorldRuntime),
// the hero ([0x743a170]) and the own record, all from `world`, and the world clock (0x5767b88). `send` is what 0x517a20
// sends (the C1 10) from the 0x5e8's creation; if empty, nothing goes out (its rand() still run). `models`, if present,
// gives the BMD that 0x4ea030 does not find in `faunaModels` (the map's models), as MonsterSkeletonOf.
struct HighEffectContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    float worldClock = 0.0f;
    std::function<void(Protocol::Outgoing)> send{};
    const Models::IModelLibrary* models = nullptr;
    // The current map's terrain, whose cells 0x517a20 reads in the siege; without it, an empty one.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna); without them, none.
    Assets::FaunaModels* faunaModels = nullptr;
};

// From a character's step (0x4f5b70, 0x4fd5f0, 0x4f97b0 and their helpers).
[[nodiscard]] HighEffectContext HighContextOf(const Actors::CharacterStepContext& context);

// 0x4f5b10(character, x, y, z, point): `offset` rotated by the character's angles (+0x1c) plus its position, one fstp
// per axis.
[[nodiscard]] glm::vec3 PointAround(const Kernel::ActorState& actor, const glm::vec3& offset);

// 0x4ec110(the type's model, own bones (+0x120) + bone · 0x30, offset, point, 1): the world point of a character's
// bone, with its model's scale and origin (the ones 0x4fd69b copies to it before the step). Without own bones (before
// the first draw), the matrix at zero (Q-C318), as OwnBonePoint in CharacterStep.cpp.
[[nodiscard]] glm::vec3 ActorBonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                                       const Kernel::WorldState& world);

// The types whose creation goes through CreateHighModelEffect (those of the 0x114/2..6, the 0x115 and the 0x621/0 are
// ModelEffects').
[[nodiscard]] bool IsHighModelEffect(std::int16_t type);

// 0x518cf0(type, position, angles, light, subtype, owner, key, +0x89, +0x88, +0x1bd, scale) with one of those types:
// the prefix (Effects::ModelEffects::Begin) and the type's case, with its rand(), joints, particles and sounds. With
// the table full, nothing and no rand(). It cuts, with what came before and its rand(), where the case asks for
// something MuRet does not have (a joint or a particle from another CL11 batch). Returns the slot.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHighModelEffect(const Kernel::ModelEffectArguments& arguments, const HighEffectContext& context);

// What these types' step asks for: the common step's services (the terrain height, the dynamic light, the sound, the
// model's published action) and the game's world and CRT. `models`, as in HighEffectContext, gives the BMD that
// Assets::AssetsState::fauna lacks (that of a player owning the 0x61e/6).
struct HighEffectStepContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const Effects::ModelEffectStepServices& services;
    float worldClock = 0.0f;
    const Models::IModelLibrary* models = nullptr;
    // The current map's terrain, for the fire of Kundun's fall (0x527a6f); without it, an empty one.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna), where the meteor's pose leaves its key. nullptr: that
    // pose stops.
    Assets::FaunaModels* faunaModels = nullptr;
};

[[nodiscard]] bool StepsAsHighModelEffect(const Effects::ModelEffect& effect);

// 0x5208a0 on a live effect of those types: the local light's rand() (0x5208c5), the type's case and the common tail
// (0x52cb98: the published action and the animation of the BMDs in [0xad, 0x151), 0x51fc80's movement except for the
// types that skip it, one life less and, at 0 or less, the slot freed). It cuts, with what came before and its rand(),
// where the step asks for something unported.
[[nodiscard]] Core::Expected<void, std::string> StepHighModelEffect(Effects::ModelEffect& effect,
                                                                    const Models::Skeleton* skeleton,
                                                                    const HighEffectStepContext& context);

// The position (+0x10) of a `world` character that owns (+0x108) an effect, for the floor pass (the 0x61e/7, 0x52ded0);
// nullopt if the owner is not one of its characters. `world` must outlive the function.
[[nodiscard]] std::function<std::optional<glm::vec3>(const void*)> ActorPositionOf(const Kernel::WorldState& world);
} // namespace Client::Effects
