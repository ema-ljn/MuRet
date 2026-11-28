#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace Client::Data::Models
{
struct Model;
}
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models
namespace Client::World
{
class FrameSprites;
class ModelEffects;
class Particles;
} // namespace Client::World
namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct JointState;
} // namespace Client::Scenes

namespace Client::Actors
{
class IEffectHooks;
class IJointHooks;
} // namespace Client::Actors

namespace Client::Actors::Drawing
{
struct DrawnObject;

// What 0x4ee770 receives besides the model (ECX): the mesh, the type, the subtype, the angles of the fourth argument
// (only 0x5ac/1 reads their z) and the character of the fifth (for 0x5ac/1: the target and the owner of its joints).
struct VertexEffectCall
{
    std::int32_t mesh = 0;
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    glm::vec3 angles{};
    Kernel::ActorState* target = nullptr;
};

// The tables it releases into: 0x518cf0 (the model effects, born through `effectHooks` except the siege's), 0x53fe70
// (the particles, with their clock), 0x5491e0 (the sprites) and 0x530860 (the joints, born through `jointHooks`). Only
// the one the type asks for is needed; without it, it stops at its address.
struct VertexEffectTables
{
    World::ModelEffects* effects = nullptr;
    World::Particles* particles = nullptr;
    float worldClock = 0.0f;
    World::FrameSprites* sprites = nullptr;
    std::span<Scenes::JointState> joints{};
    const IJointHooks* jointHooks = nullptr;
    const IEffectHooks* effectHooks = nullptr;
};

// 0x4ee770: with the mesh between 0 and the model's (+0x24) and with triangles (+0xa), each corner of each polygon (the
// count's byte signed; without deduplicating) on the scratch of 0x4ebc80 (0x5786f9c + (mesh · 15,000 + vertex) · 12,
// `meshes`), with the angles at 0 and the light at 1 at the start:
// - 0x10a: /0, signed rand() & 1 and with 0 the 0x10b/0, and rand() % 10 and with 0 the 0x10a/0; /1 the 0x10b/2 and
//   /2 the 0x10b/3, without rand(); /3, the 0x10a + (rand() & 1) with subtype 4.
// - 0x10c, with the light at 0.2f: /1, rand() % 5 (0x10d/2) and rand() % 10 (0x10c/2); /0, rand() % 12 (0x10d/0) and
//   rand() % 50 (0x10c/0).
// - 0x12d, 0x12e and 0x12f: rand() % 3 and with 0 the 0x12d (0x12f for 0x12f) + (rand() & 1), with subtype 1 for 0x12d
//   and 0 for the other two.
// - 0x131, with the light at 0.2f: rand() % 12 (0x132), rand() % 40 (0x131) and rand() % 40 (0x133).
// - 0x137: signed rand() & 7 and with 0 the subtype's 0x137 shell (World/SkinShellEffects.h; 0x632/0 and /1 ask for it
//   with an owner on reaching the floor, Scenes/SkinShells.h).
// - 0x13c: rand() % 45; with 0 and fewer than 20 counted, /0 the 0x13c/0 and /1 the 0x12d/2 and the 0x12e/2; any
//   subtype counts one.
// - 0x5ac: /0, the 0x5ac sprite with the model's scale (+0x68) and the light at 0.08f; /1, on the triangles whose index
//   is a multiple of 22, rand() % 90 (the angles' x at minus that, y at 0), rand() % 120 − 60 plus the fourth
//   argument's z (the z) and the 0x613/0xd joint from the vertex toward the character (+0x10), with it as owner and
//   width 20.
// - 0x607, with the light at 1: rand() % 30 and with 0 the 0x607/2 particle of scale 1.
// Another type or subtype does nothing. All the additions without an owner and with the key at −1 (the effects); with
// the table full, none, and without their rand() calls. The rand() calls of 0x4ee770 come out the same.
[[nodiscard]] Core::Expected<void, std::string>
EmitVertexEffectsOf(const Data::Models::Model& model, float modelScale, std::span<const Models::SkinnedMesh> meshes,
                    const VertexEffectCall& call, const VertexEffectTables& tables, Crt::CrtRandom& random);

// EmitVertexEffectsOf with the model of `object` (without it, nothing), its scale and only the model effects' table and
// hooks.
[[nodiscard]] Core::Expected<void, std::string>
EmitVertexEffects(const Models::IModelLibrary& models, const DrawnObject& object,
                  std::span<const Models::SkinnedMesh> meshes, std::int32_t mesh, std::int32_t type,
                  std::int32_t subtype, World::ModelEffects& effects, const IEffectHooks* effectHooks,
                  Crt::CrtRandom& random);
} // namespace Client::Actors::Drawing
