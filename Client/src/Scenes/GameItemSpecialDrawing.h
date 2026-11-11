#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <glm/ext/vector_float2.hpp>
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
namespace Models
{
class CrtRandom;
struct SkinnedMesh;
} // namespace Models
} // namespace Client::Render
namespace Client::World
{
class FrameSprites;
}
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;

// Los argumentos y globales públicos que lee 609640; pulse conserva 6c9de4/6c9de8.
struct ItemSpecialDrawContext
{
    std::uint32_t flags = 2;
    std::optional<float> worldClock{};
    Render::Models::CrtRandom* random = nullptr;
    glm::vec2* pulse = nullptr;
    std::uint32_t objectFlags = 0;
    std::function<Core::Expected<glm::vec3, std::string>(std::size_t)> bonePoint{};
    // object+120 prestado a4ec110 con posición0 yTranslate1; no es la pose global.
    std::function<Core::Expected<glm::vec3, std::string>()> privateBoneOrigin{};
    World::FrameSprites* sprites = nullptr;
    glm::vec3* objectLight = nullptr;
    // Recibe cada store antes de dibujar: conserva también el prefijo si después falta un recurso.
    DrawnObject* publishedObject = nullptr;
};

// 609640 completo, incluido RenderBody del default, sobre la pose retenida del caller.
[[nodiscard]] Core::Expected<void, std::string>
DrawSpecialItemMeshes(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                      Core::Duration frameClock, std::span<const Render::Models::SkinnedMesh> meshes,
                      const ItemSpecialDrawContext& context = {});
} // namespace Client::Scenes
