#pragma once

#include "Render/IParticleDrawBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace Client::Scenes
{
class IModelLibrary;
}
namespace Client::Render
{
class ModelBatch;
enum class ModelCoordinateSpace : std::uint8_t;
enum class ModelStateOperation : std::uint8_t;

// Adapter CPU→ModelBatch→GlModelRenderer existente, sin renderer alternativo ni metadata propia.
// Perfil productivo: alpha620a00(raw0/raw1), additive620a90, subtractive620b10, blended620c10 y621400. Las otras
// familias de D2 que no representa este perfil fallan en su frontera, sin callbacks vacíos.
// Antes de Release/cargas: comprobar ModelBatch::InvalidateTextureKnowledge y no usar este
// backend entre esos efectos. Con trabajo pendiente la notificación falla; nunca resolver un
// objeto reemplazado silenciosamente durante Draw. Sólo un backend/primitive por lote.
class ModelBatchParticleBackend : public IParticleDrawBackend
{
public:
    ModelBatchParticleBackend(std::reference_wrapper<ModelBatch> batch,
                              std::reference_wrapper<const Scenes::IModelLibrary> models,
                              ModelBatchParticleState initialState);
    ModelBatchParticleBackend(const ModelBatchParticleBackend&) = delete;
    ModelBatchParticleBackend& operator=(const ModelBatchParticleBackend&) = delete;

    [[nodiscard]] Result AlphaWriteDepth(); // 620a00(raw1), incluidos6208f0/620890.
    [[nodiscard]] Result AlphaInheritDepth() override;
    [[nodiscard]] Result Additive() override;
    [[nodiscard]] Result SubtractivePrefix() override;
    [[nodiscard]] Result Helper6208f0() override;
    [[nodiscard]] Result Helper6208b0() override;
    [[nodiscard]] Result SubtractiveTail() override;
    [[nodiscard]] Result BlendedPrefix() override;
    [[nodiscard]] Result BlendedTail() override;
    [[nodiscard]] Result Type620850() override;
    [[nodiscard]] Result Type620870() override;
    [[nodiscard]] Result BindTexture(std::int32_t selector) override;
    [[nodiscard]] Result Begin() override;
    [[nodiscard]] Result Color3(const glm::vec3& light) override;
    [[nodiscard]] Result Color4(const Color& color) override;
    [[nodiscard]] Result TexCoord(glm::vec2 uv) override;
    [[nodiscard]] Result Vertex(const glm::vec3& position) override;
    [[nodiscard]] Result End() override;

    // Modelo lógico World de5306a0: un Push/Pop, traslación acumulada y corners YA rotados por L5.
    // No replica matrix modes GL ni una pila nativa de profundidad arbitraria.
    [[nodiscard]] Result PushMatrix();
    [[nodiscard]] Result Translate(float x, float y, float z);
    [[nodiscard]] Result DrawWorldQuad(std::span<const glm::vec3, 4> corners, std::span<const glm::vec2, 4> uv);
    [[nodiscard]] Result PopMatrix();
    [[nodiscard]] const Models::OriginalMatrix& ModelMatrix() const;
    [[nodiscard]] bool HasPushedMatrix() const;

    [[nodiscard]] ModelBatch& Batch() const;
    [[nodiscard]] const ModelBatchParticleState& State() const;
    [[nodiscard]] bool IsDrawing() const;
    // Lee el MISMO lote. Intervención ajena pierde la clave raw conocida, nunca deriva selector
    // de un slot ni observa caches GL. Las escrituras propias reconocen su nuevo serial.
    [[nodiscard]] Result SynchronizeBatch();

private:
    [[nodiscard]] Result Alpha(bool writeDepth);
    [[nodiscard]] Result PublishOperation(ModelStateOperation operation);
    [[nodiscard]] Result PrepareMaterial();
    [[nodiscard]] Result MaterialTail(bool alphaTest, bool enableFog);
    [[nodiscard]] Result CheckPrimitiveState() const;
    [[nodiscard]] Result BeginQuad(ModelCoordinateSpace space);

    const std::reference_wrapper<ModelBatch> m_batch;
    const std::reference_wrapper<const Scenes::IModelLibrary> m_models;
    ModelBatchParticleState m_state;
    std::optional<std::uint64_t> m_observedSerial;
    // Scratch único del quad en curso; un fallo no publica triángulos ni inventa End/rollback.
    std::array<ModelVertex, 4> m_corners{};
    std::size_t m_cornerCount = 0;
    // Base World del perfil portado; la vista se aplica por comando en el renderer existente.
    static constexpr Models::OriginalMatrix IdentityModel{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    Models::OriginalMatrix m_model = IdentityModel;
    std::optional<Models::OriginalMatrix> m_savedModel;
    ModelCoordinateSpace m_primitiveSpace;
};
} // namespace Client::Render
