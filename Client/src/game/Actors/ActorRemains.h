#pragma once

#include "Core/Expected.h"
#include "Terrain/GroundMarks.h"

#include <glm/ext/vector_float3.hpp>

#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::World
{
class ModelEffects;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The remains of one who finishes dying: 0x52e810, called by 0x4fcb10 (the 0x20a with action 0xc3 or 0xc4,
// 0x4fcb9c) and 0x4fcbb0 (the monsters with action 6, 0x4fcc3c) with +0x38a at 0, which they set to 1 first.
// *Static*. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las marcas del suelo" and "Los restos
// (0x52e810)".
namespace Client::Actors
{

// What 0x52e810 reads and writes besides the character: the model of its type (0x5769ad8 + type · 0xe0, its +0x54)
// with the scale (+0x68) and position (+0x6c) of the last one that copied them into it (Q-C1178, like Q-C412:
// WorldState::ModelPlacement), the table of model effects (0x518cf0), that of the ground marks (0x548fe0) and
// rand().
struct ActorRemainsContext
{
    const Models::Skeleton* skeleton = nullptr;
    float modelScale = 0.0f;
    glm::vec3 modelOrigin{};
    World::ModelEffects& effects;
    Crt::CrtRandom& random;
    // nullptr: the first blood mark stops at 0x52e96c.
    Terrain::GroundMarkTable* marks = nullptr;
};

// 0x52e810(c): with the model's +0x54 bone at −1, nothing. 0x161 (the ice monster of Devias) turns off its live byte
// and creates ten 0xca ice shards (0x518cf0, ModelEffects::CreateIceShard, five rand() each); 0x159, 0x160 and 0x164
// leave nothing. The rest, twice: rand() % 100 − 50 for the x and another for the y of the offset (z at 0), the point
// of the +0x54 bone of the own bones (+0x120, 0x4ec110 with Translate at 1; without them, the matrix at zero,
// Q-C318), the side ((rand() & 3) + 8) · 0.1f, the turn rand() % 360 and the blood mark 0x5e3 with the +0xf4 light
// (0x548fe0, Terrain::AddGroundMark, with the life's rand() if there is room). Without the model loaded it stops at
// 0x52e834; if AddGroundMark stops, too.
[[nodiscard]] Core::Expected<void, std::string> LeaveTheRemains(Kernel::ActorState& actor,
                                                                const ActorRemainsContext& context);
} // namespace Client::Actors
