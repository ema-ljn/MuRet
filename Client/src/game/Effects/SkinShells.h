#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
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
namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

// The 0x137 shells (Effects/SkinShellEffects.h): the ones the joint 0x632/0 or /1 with an owner drops on reaching the
// ground (StormJoints.cpp) and their draw in 0x6002e0.
namespace Client::Effects
{

// 0x53c1c6..0x53c246, with the character that owns the joint. 0x4eb860(0x68b181c, 0, 0, 0, +0x1c, +0x28, 0, 1) poses
// the 0x20b (Player\Shadow01, 0x5769ad8 + 0x1c9a0) with its shared action, scale and origin (+0xa0, +0x68 and +0x6c,
// those of a shadow's last draw: 0x60cf70 from 0x50bcec; WorldState::ModelPlacement) and the owner's angles and head
// angles, and 0x4ebc80(0x68b181c, +0x124, +0x130, +0x13c, 0) passes its vertices without scale or origin (0x5fccc0) and
// writes the owner's +0x13c box with its +0x124..+0x138 box and that origin. Then, 0x4ee770(0, 0x137, subtype, 0, 0)
// (Actors::Drawing::EmitVertexEffects): for each corner of mesh 0, with rand() % 8 at 0, a 0x137 of the subtype at the
// vertex. Without the 0x20b or its skeleton, or if the pose fails, it cuts at 0x53c1eb.
[[nodiscard]] Core::Expected<void, std::string>
ShedTheSkinShells(Kernel::ActorState& owner, std::int32_t subtype, const Models::IModelLibrary& models,
                  Kernel::WorldState& world, Effects::ModelEffects& effects, Crt::CrtRandom& random);

// 0x600557..0x6005e0, the 0x137's case in 0x6002e0 (after posing): the /0 with the model's color at its light (+0xf4)
// and RenderBody(2) with Chrome01 (0x5c0); the /1 with the color at (0.1f, 0.5, 1) and RenderBody(0x42) with Chrome01;
// with another subtype, nothing (0x602e7d). Always true: the common body is not drawn.
[[nodiscard]] Core::Expected<bool, std::string>
DrawTheSkinShellBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, const Effects::ModelEffect& effect,
                     Actors::Drawing::DrawnObject& object, Core::Duration frameClock);
} // namespace Client::Effects
