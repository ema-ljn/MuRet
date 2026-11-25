#pragma once

#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>

namespace Client::Render
{
// Estado persistente de servicios/conocimiento del adapter. ModelBatch es la autoridad raster;
// SynchronizeBatch importa sus cambios ajenos. No son globals nativos observados.
struct ModelBatchParticleState
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

    // BeginPass del renderer actual: cull/depth/write activos, shader sin alpha/fog.
    // El perfil portado empieza color blanco/UV0 y SIN conocimiento del binding anterior.
    // Es sólo un perfil de construcción. El primer servicio y cada cambio ajeno consultan el
    // MISMO ModelBatch. No reconstruirlo por frame ni atribuir a Begin una restauración GL.
    [[nodiscard]] static ModelBatchParticleState ForModelPass();
};
} // namespace Client::Render
