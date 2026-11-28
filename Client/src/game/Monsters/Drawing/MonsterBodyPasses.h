#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/Skinning.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct ClothWorld;
} // namespace Client::Models

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Kernel
{
struct ActorState;
struct JointState;
} // namespace Client::Kernel

namespace Client::Actors
{
class IEffectHooks;
} // namespace Client::Actors

namespace Client::Monsters::Drawing
{

// What 0x4ef0c0 (RenderBody) and 0x4ecc30 (RenderMesh) receive after the flags: the alpha, the blendMesh, its light,
// the U, the V, the hidden mesh (RenderBody only) and the texture (−1: each mesh's own; otherwise, that slot).
struct BodyPassArgs
{
    float alpha = 1.0f;
    std::int32_t blendMesh = -1;
    float light = 1.0f;
    glm::vec2 uv{};
    std::int32_t hiddenMesh = -1;
    std::int32_t texture = -1;
};

// A pass's values from the character: +0x174, +0x64, +0x68, +0x6c (ActorState::textureU), +0x70 (at 0: only the spawn
// writes it), +0x58 and texture −1.
[[nodiscard]] BodyPassArgs ActorPassArgs(const Kernel::ActorState& actor);

// The shared model while 0x6002e0 draws it: the color (+0x48..+0x50), the +0x88 (unlit and with the UV scrolled; 0xff
// is nullopt), the per-vertex light (+0x44) and the meshes 0x4ebc80 left on the character's bones.
struct MonsterBodyModel
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    Actors::Drawing::DrawnObject object;
    std::span<const Models::SkinnedMesh> meshes;
    Core::Duration frameClock{};
    // The world clock (0x5767b88) in ms, read by the wave of 0x4ecc30 (flag 0x400, 0x4ed775).
    float worldClock = 0.0f;
    // The meshes of another 0x4ebc80 in the middle of 0x6002e0 (0x60cd60 from 0x43fc9d, at another scale): `meshes`
    // looks at them from there on, as the original reads 0x5786f9c in what follows.
    std::vector<Models::SkinnedMesh> rescaled{};
    // The RGB of 0x5ffce0 before the tints of 0x6002e0: 0x42e458 rebuilds it for the 0x12b projectile.
    glm::vec3 preludeColor{1.0f};
    // The current 0x6c881c: the 0x12b accessory of 0x42e3d0 keeps the actor's mode (1, 1.2, 1.4).
    float boneScale = 1.0f;
};

// 0x4ef0c0: RenderBody(flags, args) with the model's state. The base pass (2 with each mesh's texture) is
// DrawObjectFromMeshes's; the others, DrawItemMeshPass's, which leaves what it stores in the model's color. A negative
// texture other than −1 or flags outside DrawItemMeshPass's are an error.
[[nodiscard]] Core::Expected<void, std::string> DrawBodyPass(MonsterBodyModel& model, std::uint32_t flags,
                                                             const BodyPassArgs& args);

// 0x4ecc30: RenderMesh(mesh, flags, args) (without the hidden mesh, which it does not look at). A mesh outside the
// model draws nothing.
[[nodiscard]] Core::Expected<void, std::string> DrawMeshPass(MonsterBodyModel& model, std::int16_t mesh,
                                                             std::uint32_t flags, const BodyPassArgs& args);

// What 0x6002e0 and its chain read and write outside the model, for a monster or an NPC of 0x602e90.
struct MonsterBodyFrame
{
    MonsterBodyModel& model;
    Kernel::ActorState& actor;
    // The bones 0x5ffdf0 posed in the character's own buffer (+0x120).
    std::span<const Math::BoneTransform> bones;
    // The map (0x6c9dd0) and the fourth argument of 0x602e90 (SpecialOf).
    std::int32_t map = 0;
    std::uint16_t special = 0;
    // The world clock (0x5767b88) in ms.
    float worldClock = 0.0f;
    // The CRT rand() and the protected step counter (0x7d3a0f0) with its water texture (0x7d2d360).
    Crt::CrtRandom& random;
    Models::ClothWorld& cloth;
    // 0x4294b0: the siege byte.
    bool siegeActive = false;
    // The sound requested from 0x41ed10 (CL10); only in the original's frames.
    std::optional<Audio::SoundId>* lastSoundId = nullptr;
    std::function<void(Audio::SoundId)> soundRequest{};
    bool originalFrame = true;
    // 0x7d325c8, the camera shake (GameCharacterPass::cameraShake). nullptr: cuts if a case writes it.
    float* cameraShake = nullptr;
    // 0x4ee770 uses the same 0x518cf0 pool as the rest of the frame.
    Effects::ModelEffects* effects = nullptr;
    // Who creates the stones and shells of 0x4ee770 (GameCharacterPass::effectHooks). nullptr: they stop.
    const Actors::IEffectHooks* effectHooks = nullptr;
    // The frame's particles (0x53fe70), for the 0x607/2 of 0x4ee770 when types 0x191..0x197 die (0x43ed6c).
    Particles::Particles* particles = nullptr;
    // 0x5f93c0, the terrain height the spawns of Kundun's falling pieces ask for (0x51e26e). Empty, those spawns cut.
    std::function<double(float, float)> terrainHeight{};
    // 0x530860: the joint table (0x74a8d78, WorldState::DrawTail), for Kundun's rays on death (0x43f20d), which touch
    // it only in an original frame; between frames, the projected copy (GameCharacterPass::joints). Empty: it cuts
    // there.
    std::span<Kernel::JointState> joints{};
};

// 0x41ed10(id, object, 0) in an original frame: records it for CL10.
void RequestBodySound(MonsterBodyFrame& frame, Audio::SoundId sound);

// 0x7d3a0f0: the logic step counter, with the 4 rand() of its close if no other access is open.
[[nodiscard]] std::int32_t ReadLogicSteps(MonsterBodyFrame& frame);
} // namespace Client::Monsters::Drawing
