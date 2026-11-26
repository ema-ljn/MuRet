#pragma once

#include "EngineApi.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>

namespace Client::Render
{
// The adapter's persistent service/knowledge state. ModelBatch is the raster authority; SynchronizeBatch imports its
// foreign changes. These are not observed native globals.
struct ENGINE_API ModelBatchParticleState
{
    std::int32_t blendMode;
    std::uint8_t cullEnabled;
    std::uint8_t depthWrite;
    std::uint8_t alphaTest;
    std::uint8_t texture2d;
    std::uint8_t fogPolicy;
    bool depthTest;
    Color color;
    glm::vec2 uv;
    std::optional<std::int32_t> textureSelector;
    std::optional<TextureSlot> texture;
    std::optional<std::uint32_t> rawTextureName;

    // The current renderer's BeginPass: cull/depth/write on, shader without alpha/fog. The ported profile starts with
    // white color/UV0 and NO knowledge of the previous binding; it is only a construction profile. The first service
    // and each foreign change query the SAME ModelBatch. Do not rebuild it per frame or credit Begin with a GL restore.
    [[nodiscard]] static ModelBatchParticleState ForModelPass();
};
} // namespace Client::Render
