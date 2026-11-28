#pragma once

#include "Core/Expected.h"
#include "Kernel/ModelEffectArguments.h"
#include "Protocol/Outgoing.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

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
class IModelLibrary;
} // namespace Client::Models
namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
class WorldState;
struct JointState;
} // namespace Client::Kernel

// CL11, wizards batch: Effects::WizardModelEffects' services from the scene's world. The effects' owner (+0x108) is a
// pool character (ActorState: the live byte, +0x2, the position, +0x78, the action +0x111, +0x16c and +0x1bc), the hero
// is [0x743a170], the own record is WorldRuntime's and OwnCharacterRecord (+0xf04), the siege is
// CastleSiegeState::siegeActive (0x4294b0), the joints go to the world's table (0x530860, Joints/FaunaJointCreation)
// and 0x517a20 is Actors::HitTheTargetsAround.
namespace Client::Effects
{

// What the scene provides: the frame's tables, the step's terrain (the height and the attributes), the camera shake,
// the sound and the sending of the C1 10.
struct WizardSceneParts
{
    Particles::Particles* particles = nullptr;
    float worldClock = 0.0f;
    Particles::FrameSprites* sprites = nullptr;
    const Terrain::WorldTerrain* terrain = nullptr;
    float fixedHeight = 1200.0f;
    float* cameraShake = nullptr;
    std::function<void(Audio::SoundId)> sound{};
    std::function<void(Protocol::Outgoing)> send{};
    // The joints table (0x530860); empty, the world's (WorldState::DrawTail).
    std::span<Kernel::JointState> joints{};
    // The game's skeletons that 0x4ea030 reads after the map's (`faunaModels`), for the 0xc2/4's push (0x524a8d).
    // Without them, a 0x20a push cuts.
    const Models::IModelLibrary* models = nullptr;
    // The dynamic light (Terrain::TerrainState::dynamicLight) the effects add to (0x5f9780). nullptr: adding one stops.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
};

[[nodiscard]] Effects::WizardEffectServices WizardEffectServicesOf(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                   const WizardSceneParts& parts);

// Those of a caller of the characters' step (0x4fd5f0, 0x4f97b0, 0x4e6380…): without the camera shake.
[[nodiscard]] Effects::WizardEffectServices WizardEffectServicesOf(const Actors::CharacterStepContext& context);

// 0x518cf0(type, position, angles, light, subtype, owner, key, 0, 0, 0, 0): a wizard effect's arguments.
[[nodiscard]] Kernel::ModelEffectArguments WizardArguments(std::int16_t type, const glm::vec3& position,
                                                           const glm::vec3& angles, const glm::vec3& light,
                                                           std::int32_t subtype, void* owner = nullptr,
                                                           std::uint16_t key = 0xffff);

// A wizard effect's creation (Effects::CreateWizardModelEffect) from a caller of the characters' step, with its
// services (WizardEffectServicesOf). With the table full, nothing and no rand(). `callerLight` is the caller's light
// that the 0xc1/1 and /2 write.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateWizardEffect(const Actors::CharacterStepContext& context, const Kernel::ModelEffectArguments& arguments,
                   glm::vec3* callerLight = nullptr);
} // namespace Client::Effects
