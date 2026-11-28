#pragma once

#include "Core/Expected.h"
#include "Math/OriginalRotation.h"
#include "Kernel/DrawTailState.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
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
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Actors
{
struct SkillHitsRequest;
} // namespace Client::Actors

// What the joints' creation (0x530860), their step (0x535840) and the families that live in their own files share
// (FaunaJointCreation.cpp dispatches).
namespace Client::Joints::JointKernels
{
// 0x530afa..0x530c38 (and the end of 0x535520): the four points of the first segment, (∓width/2, 0, 0) and (0, 0,
// ∓width/2) rotated by the matrix and added to the position. At creation the width is the one retained from the
// previous use.
void FirstQuad(Kernel::JointState& joint, const Math::OriginalMatrix& matrix);
void FirstQuad(Kernel::JointState& joint);
// 0x535520(joint, matrix): the count + 1 up to points − 1, shifts the segments and rebuilds the first.
[[nodiscard]] Core::Expected<void, std::string> AppendJointTail(Kernel::JointState& joint,
                                                                const Math::OriginalMatrix& matrix);
[[nodiscard]] Core::Expected<void, std::string> AppendJointTail(Kernel::JointState& joint);
// 0x535520 without rebuilding the first segment: the count and the segment shift. Only for a tail that is not drawn and
// whose matrix is undefined (Q-C1623).
[[nodiscard]] Core::Expected<void, std::string> AdvanceJointTail(Kernel::JointState& joint);
// 0x535020(joint, matrix, crossed): the same as 0x535520, but with `crossed` the second pair of points goes in y,
// (0, ∓width/2, 0), instead of in z.
[[nodiscard]] Core::Expected<void, std::string>
AppendCrossedJointTail(Kernel::JointState& joint, const Math::OriginalMatrix& matrix, bool crossed);

// 0x4ec110(the player model, the owner's own bones + bone · 0x30, offset, point, 1): what the creation of some joints
// reads from an owner character (the 614/2, 0x5313fe). The scale and origin are those the last draw left in the player
// model (0x20a). Without the service, those creations stop.
using PlayerBonePoint =
    std::function<glm::vec3(const Kernel::ActorState& owner, std::size_t bone, const glm::vec3& offset)>;
struct CreationServices
{
    PlayerBonePoint playerBonePoint{};
    // 0x41ed10(sound, 0, 0): the 0x613/1 plays at creation (0x5330c5). Without it, no sound (CL10 decides how).
    std::function<void(Audio::SoundId)> soundRequest{};
    // The own record ([0x7681eb4]): the count of its open accesses and its +0xf04, which the 0x613/0 and /5 read with
    // an access (0x532675, 0x532bab; the close that leaves it at 0 draws 3848 rand()). Without them, those creations
    // stop.
    std::uint8_t* ownRecordDepth = nullptr;
    const std::uint8_t* ownVolley = nullptr;
    // The 400 characters (0x743a16c): the 614/0xf picks among them the ones it chains (0x531922). Without them, it
    // stops.
    std::span<const Kernel::ActorState> actors{};
};

// 0x520170(effect): the hit of the arrow that the 0x60f/0xb and /0x19 with skill 1 request from their step (0x53ae6d),
// with the arrow at the joint's position. Set by whoever has the arrows' context (Effects/ArrowEffects.h).
using EffectHit = std::function<Core::Expected<void, std::string>(Effects::ModelEffect&)>;

// What the 0x613/0, /5 and /0x13 of width 80 read after releasing their 0xd0 (0x539a50..0x539add): the siege of
// 0x4294b0 ([0x7239ec], CastleSiegeState::siegeActive), the attributes of 0x5f82c0 (0x7d0d348) and the hero's targets
// of 0x517a20 (Actors::HitTheTargetsAround).
struct VolleyServices
{
    bool siegeActive = false;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    std::function<Core::Expected<void, std::string>(const Actors::SkillHitsRequest&)> skillHits{};
};

// What a joint family's step asks of the frame: the whole pool (the creations the step makes land in it, 0x53703d), the
// joint's slot, and the services the common step already receives.
struct StepServices
{
    std::span<Kernel::JointState> joints;
    std::size_t index = 0;
    Crt::CrtRandom* random = nullptr;
    Models::ClothWorld* cloth = nullptr;
    Particles::Particles* particles = nullptr;
    Particles::FrameSprites* sprites = nullptr;
    Terrain::TerrainLighting* lighting = nullptr;
    float worldClock = 0;
    const std::function<void(Audio::SoundId)>* soundRequest = nullptr;
    // 0x53584f..0x53586c: [ebp−0x30] and [ebp+8], the position minus the target in x and y, as float, before the common
    // move. Read by the 0x613/0 and /5 (0x539b33).
    float startDeltaX = 0;
    float startDeltaY = 0;
    // 0x5f93c0(x, y): the terrain height, as it leaves it on the x87 stack. Requested by the 0x613/0 and /5 (0x539c19).
    const std::function<double(float, float)>* terrainHeight = nullptr;
    // The 400 characters (0x743a16c): the 0x10f/4 and /9 look at the one of their +0xa0c (0x535d63). Without them, they
    // stop.
    std::span<const Kernel::ActorState> actors{};
    // The owner arrow's hit (0x520170). Without it, the 0x60f/0xb and /0x19 with skill 1 stop.
    const EffectHit* effectHit = nullptr;
    // The family's step leaves 0x535840 (ret) without the tail and life of 0x53c393 (the 0x618 with a dead owner,
    // 0x538216).
    bool* returned = nullptr;
    // The table of effects with a model (0x7491fd8): the 0x613/0x13 releases the 0x11b/0 there (0x539b08). Without it,
    // it stops.
    Effects::ModelEffects* effects = nullptr;
    // The hero ([0x743a170]): the 0x61c/0 compares its owner with it (0x53b548). Without it, the 0x61c/0 with an owner
    // stops.
    const Kernel::ActorState* hero = nullptr;
    // The shared models (0x5769ad8 + type · 0xe0, their scale and position): the 0x614/0xc places itself in the owner's
    // model (0x536b1d), the 0x62c/5..7 with an owner writes the position of its own (0x53bc42) and the 0x632/0 and /1
    // with an owner read the 0x20b's (0x53c1eb). Without them, they stop.
    Kernel::WorldState* world = nullptr;
    // The 0x613/0, /5 and /0x13 of width 80 (0x539a50) and the hero's 0x61c/0 (0x53b569). Without them, they stop
    // there.
    const VolleyServices* volley = nullptr;
    // The models (0x5769ad8): the 0x632/0 and /1 with an owner pose the 0x20b (Player\Shadow01) on reaching the ground
    // (0x53c1eb). Without them, they stop there.
    const Models::IModelLibrary* models = nullptr;
};

// The owner of a joint of this file's families, when it is a character (ActorState): the original's object, with the
// alive byte at +0, the position at +0x10, the angles at +0x1c and the light at +0xf4.
[[nodiscard]] const Kernel::ActorState* ActorOwner(const Kernel::JointState& joint);
} // namespace Client::Joints::JointKernels
