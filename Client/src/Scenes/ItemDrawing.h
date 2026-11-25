#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"
#include "Scenes/CharacterDrawing.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
// What 0x60cf70 reads from the object that owns the part, and the two fields it writes.
struct ItemPartOwner
{
    std::uint16_t type = 0;                         // +2: 0xc5 outlines only its first mesh
    Models::ObjectTransform transform;              // +0xc, +0x10, +0x1c
    std::span<const Math::BoneTransform> bones;     // +0x120
    glm::vec3 light{1.0f};                          // +0x3b4
    float alpha = 1.0f;                             // +0x174
    std::int16_t hiddenMesh = Models::NoHiddenMesh; // +0x58, the outline only
    std::int16_t blendMesh = Models::NoBlendMesh;   // +0x64
    float* blendMeshLight = nullptr;                // +0x68: read, and written by the class 4 helm
    glm::vec3* storedLight = nullptr;               // the light the 0x121b case writes (0x60ad23)
    glm::vec2 uv{};                                 // +0x6c, +0x70
    std::uint32_t stateFlags = 0;                   // +0x78: no outline with 0x4000
    bool vertexLight = false;                       // +0xe8
    std::span<const ObjectOutlinePass> outline;     // empty when the mode is not 1
    float boneScale = 1.0f;                         // 0x6c881c by the mode
};

struct ItemPart
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
    std::uint8_t excellent = 0;
    std::uint8_t ancient = 0;
    std::uint8_t skin = 0; // model + 0x98
};

// 0x60cf70
[[nodiscard]] Core::Expected<void, std::string> DrawItemPart(Render::ModelBatch& batch,
                                                             const Models::IModelLibrary& models,
                                                             const ItemPartOwner& owner, const ItemPart& part,
                                                             Core::Duration frameClock, const ItemDrawContext& context,
                                                             const std::function<void(const glm::vec3&)>& onPartBox);

// +0x1f8 + k * 0x20: the head, the helm, the armor, the pants, the gloves and the boots. Without a model, no part.
using WornParts = std::array<std::optional<ItemPart>, 6>;

// 0x50d3dc..0x50d4f5: from the boots to the head, the class skin in each model and the class 4 helm.
[[nodiscard]] Core::Expected<void, std::string>
DrawWornParts(Render::ModelBatch& batch, const Models::IModelLibrary& models, const ItemPartOwner& owner,
              const WornParts& parts, std::uint8_t characterClass, Core::Duration frameClock,
              const ItemDrawContext& context, const std::function<void(const glm::vec3&)>& onPartBox);
} // namespace Client::Scenes
