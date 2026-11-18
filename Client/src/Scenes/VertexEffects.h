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
// Familias alcanzadas por Castle Siege: 10a/0,12d/0,12e/0,12f/0 y131/0.
// Pool lleno conserva los sorteos del caller.
[[nodiscard]] Core::Expected<void, std::string>
EmitVertexEffects(const IModelLibrary& models, const DrawnObject& object,
                  std::span<const Render::Models::SkinnedMesh> meshes, std::int32_t mesh, std::int32_t type,
                  std::int32_t subtype, World::ModelEffects& effects, Render::Models::CrtRandom& random);
} // namespace Client::Scenes
