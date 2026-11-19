#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct SkinnedMesh;
} // namespace Client::Render::Models
namespace Client::World
{
class ModelEffects;
}
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;

// 4ee770: cada esquina de cada polígono de la malla, sobre el scratch de4ebc80 (sin deduplicar).
// Familias alcanzadas por Castle Siege: 10a/0,12d/0,12e/0,12f/0 y131/0; y el 0x13c (las piedras de 0x441400 al morir:
// el /0 con 0x13c/0, el /1 con 0x12d/2 y 0x12e/2); y el 0x137 con cualquier subtipo (las cáscaras del joint 0x632/0 y
// /1 al llegar al suelo, Scenes/SkinShells.h).
// Pool lleno conserva los sorteos del caller.
[[nodiscard]] Core::Expected<void, std::string>
EmitVertexEffects(const IModelLibrary& models, const DrawnObject& object,
                  std::span<const Render::Models::SkinnedMesh> meshes, std::int32_t mesh, std::int32_t type,
                  std::int32_t subtype, World::ModelEffects& effects, Render::Models::CrtRandom& random);
} // namespace Client::Scenes
