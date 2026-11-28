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

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render
namespace Client::Particles
{
class FrameSprites;
}
namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models

namespace Client::Actors::Drawing
{
struct DrawnObject;

// The arguments and public globals 0x609640 reads; pulse keeps 0x6c9de4/0x6c9de8.
struct ItemSpecialDrawContext
{
    std::uint32_t flags = 2;
    std::optional<float> worldClock{};
    Crt::CrtRandom* random = nullptr;
    glm::vec2* pulse = nullptr;
    std::uint32_t objectFlags = 0;
    std::function<Core::Expected<glm::vec3, std::string>(std::size_t)> bonePoint{};
    // object+0x120 lent to 0x4ec110 with position 0 and Translate 1; it is not the global pose.
    std::function<Core::Expected<glm::vec3, std::string>()> privateBoneOrigin{};
    Particles::FrameSprites* sprites = nullptr;
    glm::vec3* objectLight = nullptr;
    // Receives each store before drawing: it also keeps the prefix if a resource is missing later.
    DrawnObject* publishedObject = nullptr;
};

// 0x609640 in full, including the default's RenderBody, on the caller's retained pose.
[[nodiscard]] Core::Expected<void, std::string>
DrawSpecialItemMeshes(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                      Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes,
                      const ItemSpecialDrawContext& context = {});
} // namespace Client::Actors::Drawing
