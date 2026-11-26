#pragma once

#include "EngineApi.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/ITextureSlots.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureEnvMode.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace Client::Render
{
class ModelBatch;
enum class ModelCoordinateSpace : std::uint8_t;
enum class ModelStateOperation : std::uint8_t;

// Adapter onto the existing ModelBatch → GlModelRenderer. Production profile: alpha 620a00(raw0/raw1), additive 620a90,
// subtractive 620b10, blended 620c10 and 621400; the other D2 families fail at their boundary, without empty callbacks.
// Before Release/loads: check ModelBatch::InvalidateTextureKnowledge (it fails with pending work) and do not use this
// backend between those effects; never silently resolve a replaced object during Draw. One backend/primitive per batch.
class ENGINE_API ModelBatchParticleBackend : public IParticleDrawBackend
{
public:
    ModelBatchParticleBackend(std::reference_wrapper<ModelBatch> batch,
                              std::reference_wrapper<const ITextureSlots> textures,
                              ModelBatchParticleState initialState);
    ~ModelBatchParticleBackend() override;
    ModelBatchParticleBackend(const ModelBatchParticleBackend&) = delete;
    ModelBatchParticleBackend& operator=(const ModelBatchParticleBackend&) = delete;

    [[nodiscard]] Result AlphaWriteDepth(); // 620a00(raw1), including 6208f0/620890.
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
    [[nodiscard]] Result TextureEnv(TextureEnvMode mode) override;
    [[nodiscard]] Result BindTexture(std::int32_t selector) override;
    [[nodiscard]] Result Begin() override;
    [[nodiscard]] Result Color3(const glm::vec3& light) override;
    [[nodiscard]] Result Color4(const Color& color) override;
    [[nodiscard]] Result TexCoord(glm::vec2 uv) override;
    [[nodiscard]] Result Vertex(const glm::vec3& position) override;
    [[nodiscard]] Result End() override;

    // 5306a0's logical World model: one Push/Pop, accumulated translation and corners ALREADY rotated by L5. Does not
    // replicate GL matrix modes or a native stack of arbitrary depth.
    [[nodiscard]] Result PushMatrix();
    [[nodiscard]] Result Translate(float x, float y, float z);
    [[nodiscard]] Result DrawWorldQuad(std::span<const glm::vec3, 4> corners, std::span<const glm::vec2, 4> uv);
    [[nodiscard]] Result PopMatrix();
    [[nodiscard]] const Math::OriginalMatrix& ModelMatrix() const;
    [[nodiscard]] bool HasPushedMatrix() const;

    [[nodiscard]] ModelBatch& Batch() const;
    [[nodiscard]] const ModelBatchParticleState& State() const;
    [[nodiscard]] bool IsDrawing() const;
    // Reads the SAME batch. A foreign intervention loses the known raw key; it never derives a selector from a slot nor
    // observes GL caches. Own writes acknowledge their new serial.
    [[nodiscard]] Result SynchronizeBatch();

private:
    [[nodiscard]] Result Alpha(bool writeDepth);
    [[nodiscard]] Result PublishOperation(ModelStateOperation operation);
    [[nodiscard]] Result PrepareMaterial();
    [[nodiscard]] Result MaterialTail(bool alphaTest, bool enableFog);
    [[nodiscard]] Result CheckPrimitiveState() const;
    [[nodiscard]] Result BeginQuad(ModelCoordinateSpace space);

    const std::reference_wrapper<ModelBatch> m_batch;
    const std::reference_wrapper<const ITextureSlots> m_textures;
    ModelBatchParticleState m_state;
    std::optional<std::uint64_t> m_observedSerial;
    // The single scratch of the current quad; a failure keeps the prefix until its owner is destroyed. Only the adapter
    // that opened the quad may drop its mark when the pass ends.
    bool m_ownsPrimitive = false;
    std::array<ModelVertex, 4> m_corners{};
    std::size_t m_cornerCount = 0;
    // World base of the ported profile; the view is applied per command in the existing renderer.
    static constexpr Math::OriginalMatrix IdentityModel{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    Math::OriginalMatrix m_model = IdentityModel;
    std::optional<Math::OriginalMatrix> m_savedModel;
    ModelCoordinateSpace m_primitiveSpace;
};
} // namespace Client::Render
