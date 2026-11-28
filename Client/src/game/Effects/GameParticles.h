#pragma once

#include "Core/Expected.h"
#include "Math/OriginalRotation.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models
namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::WorldObjects
{
struct WorldObject;
} // namespace Client::WorldObjects

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Kernel
{
struct ActorState;
struct PoseBuffer;
class WorldState;
} // namespace Client::Kernel

// The game's particles over the shared table at 0x743a198. Static contract: 06 § Después de los objetos, P-C55 (a).
namespace Client::Effects
{

// 0x4ec110 with Translate=0, after each drop's two offset draws.
// The caller lends the global buffer of the current pose and keeps its previous/dummy bones.
using WaterspoutBonePoint =
    std::function<Core::Expected<glm::vec3, std::string>(std::size_t bone, const glm::vec3& offset)>;

// 0x603011..0x60313f, after 0x602ef4's common rand(): light and V, the half-of-the-frames guard, and two 0x5f2/0 with
// their interleaved rand() and bones 1 and 4. A full pool consumes no Create rand().
[[nodiscard]] Core::Expected<void, std::string> StepGameWaterspoutFrame(WorldObjects::WorldObject& object,
                                                                        Particles::Particles& particles,
                                                                        Crt::CrtRandom& random, const float& worldClock,
                                                                        const WaterspoutBonePoint& bonePoint);

// With the loan, 0x4ec110(Translate=0) reads the exact global matrix, even dummy/low alpha.
// Without it, only the bone written by this model is credited; a missing matrix stops after X/Z.
[[nodiscard]] Core::Expected<void, std::string>
StepGameWaterspoutFrame(WorldObjects::WorldObject& object, Particles::Particles& particles, Crt::CrtRandom& random,
                        const float& worldClock, const Models::IModelLibrary& models,
                        const Actors::Drawing::DrawnObject& drawn, const Kernel::PoseBuffer* retainedPose = nullptr);

// 0x637128 → 0x5446e0: even with the pool empty it consumes the wind's two rand(). worldClock lends the caller's
// lvalue during the pass; it neither keeps nor copies that owner. `owners` resolves the 0x622/1's owner among the map's
// objects (Particles::ParticleObjectOwners), `actorActions` gives the action of the character owning the orbit 0x60f/0
// (Particles::ParticleActorActions), `textureHeight`, the texture height the 0x5f3 reads
// (Particles::ParticleTextureHeight), `lighting`, the 0x5ed's dynamic light (0x7b4ccc4), `actorHands`, the hand of the
// 0x5fd's owner (0x53fdb0, Particles::ParticleActorHands), and `ownerBones`, the bone of the 0x5fe's owner
// (Particles::ParticleOwnerBones, 0x53fdb0).
[[nodiscard]] Core::Expected<void, std::string>
StepGameParticles(Particles::Particles& particles, Crt::CrtRandom& random, const std::int32_t& map,
                  const std::function<double(float, float)>& terrainHeight = {}, const float& worldClock = 0.0f,
                  const std::function<WorldObjects::WorldObject*(std::uint64_t)>& owners = {},
                  const std::function<std::optional<std::uint8_t>(std::uint64_t)>& actorActions = {},
                  const std::function<std::optional<float>(std::int32_t)>& textureHeight = {},
                  Terrain::TerrainLighting* lighting = nullptr,
                  const std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>& actorHands = {},
                  const std::function<std::optional<glm::vec3>(const Particles::Particle&)>& ownerBones = {});

// `sprites` is the frame's sprite table (0x5491e0) where the 0x5fc/2 leaves its flash each step; without it, a live
// 0x5fc/2 stops (Particles::ParticleError::MissingSprites).
[[nodiscard]] Core::Expected<void, std::string> StepGameParticles(
    Particles::Particles& particles, Crt::CrtRandom& random, const std::int32_t& map,
    const std::function<double(float, float)>& terrainHeight, const float& worldClock,
    const std::function<WorldObjects::WorldObject*(std::uint64_t)>& owners,
    const std::function<std::optional<std::uint8_t>(std::uint64_t)>& actorActions,
    const std::function<std::optional<float>(std::int32_t)>& textureHeight, Terrain::TerrainLighting* lighting,
    const std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>& actorHands,
    const std::function<std::optional<glm::vec3>(const Particles::Particle&)>& ownerBones,
    const std::function<void(Audio::SoundId)>& sound,
    const std::function<std::optional<Particles::ParticleOwnerState>(const Particles::ParticleOwner&)>& ownerStates =
        {},
    Particles::FrameSprites* sprites = nullptr);

// The action (+0x111) of the character in a pool slot among `actors`, which must outlive the resolver: what the orbit
// 0x60f/0's step reads through the pointer +0x40 (0x54735f).
[[nodiscard]] std::function<std::optional<std::uint8_t>(std::uint64_t)>
ParticleActorActionsAmong(std::span<const Kernel::ActorState> actors);

// The texture height of each slot (+0x24 of 0x7d3a198) among those `textures` uploaded, as the 0x5f3's step reads it;
// `textures` must outlive the resolver.
[[nodiscard]] std::function<std::optional<float>(std::int32_t)>
ParticleTextureHeightsOf(const Models::IModelLibrary& textures);

// A particle's owner by its object's identity (WorldObject::identity) among `objects`, which must outlive the
// resolver: what the original reads through the pointer +0x40.
[[nodiscard]] std::function<WorldObjects::WorldObject*(std::uint64_t)>
ParticleOwnersAmong(std::span<WorldObjects::WorldObject> objects);

// 0x5ac/4 (0x544a9d): owner type 0x20a and byte88=1, bone ftol(rotation) of the own pose, local vector `angles` plus
// the owner's position, without scale or model publication. An invalid owner returns nullopt to turn off the light.
// The others: 0x53fdb0(particle) with an owner from the character pool (ParticleOwnerKind::Actor): the bone is byte +5
// of the published hero's part ([0x743a170] + 0x2bd + (subtype % 2, signed) · 0x20: the right hand with 0, the left
// with 1 and the body's fifth with −1) and the local point (0; −120; 0) with the 0x5fe and the 0x62d, zero with the
// others. 0x4ec110 with the owner's model (+2): its origin (+0x6c) becomes the owner's position and the scale (+0x68)
// is whatever stayed in the model; with the own bones (+0x120) of the last drawing, a zero matrix before the first
// (Q-C318). MuRet keeps the Player.bmd model (WorldState::PlayerModel): with another type, nullopt, which the particle
// returns as MissingOwner. It is the cut of the transformation rings (08, P-C358): the 0x5fe of 0x55be23 stops earlier
// with its address (Skills::ShineOnTheHero).
[[nodiscard]] std::optional<glm::vec3> OwnerBonePointOf(Kernel::WorldState& world, const Particles::Particle& particle);
// What the step reads through +0x40 of the particles that follow their owner (0x5c6/4, 0x5d5/2, the fire /9 and
// 0x5de/4): from a pool character, its live byte and its position; from a model effect, also its start (+0x17c). From
// both, the action (+0x111), which the orbit 0x60f/0 also reads from the 0x61d that drops it (0x54735f). nullopt if
// the identity belongs to neither pool. `world` must outlive the resolver.
[[nodiscard]] std::function<std::optional<Particles::ParticleOwnerState>(const Particles::ParticleOwner&)>
ParticleOwnerStatesOf(Kernel::WorldState& world);
// The 0x53fdb0 resolver over `world`, which must outlive it.
[[nodiscard]] std::function<std::optional<glm::vec3>(const Particles::Particle&)>
ParticleOwnerBonesOf(Kernel::WorldState& world);

// 0x637558 → 0x548550(0), in the eye pass after the sprites. Keeps the geometry and the operations before a failure
// and returns the address and the slot that failed.
[[nodiscard]] Core::Expected<void, std::string>
DrawGameParticles(Render::ModelBatch& batch, Particles::Particles& particles, const Models::IModelLibrary& textures,
                  const Math::OriginalMatrix& view, float worldClock, std::uint8_t mode = 0);
} // namespace Client::Effects
