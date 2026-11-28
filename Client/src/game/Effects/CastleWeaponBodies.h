#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models

namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

// The bodies of the siege's shots in the effects' draw (0x6002e0 → 0x601c62..0x60236f): the stones 0x12b and 0x12c,
// their fragments 0x12d..0x133, the debris 0x10a and 0x10b and the 0xed and 0xee, drawn with the characters' item pass
// (Actors/Drawing/CharacterDrawing.h).
namespace Client::Scenes
{
// 0x4ee770 on the vertices of the freshly posed body: the type, the object and its meshes.
using VertexEffectEmitter = std::function<Core::Expected<void, std::string>(
    std::int32_t, const Actors::Drawing::DrawnObject&, std::span<const Models::SkinnedMesh>)>;

// 0x601c62..0x60236f: bodies 0x12b/0x12c, fragments 0x12d..0x133 and 0x10a/0x10b, chrome and 0x4ee770 on impact.
[[nodiscard]] Core::Expected<void, std::string>
DrawCastleWeaponBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, World::ModelEffect& effect,
                     const Actors::Drawing::DrawnObject& object, Core::Duration frameClock,
                     const VertexEffectEmitter& emit = {});
} // namespace Client::Scenes
