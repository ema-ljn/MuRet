#include "Render/ModelBatchParticleBackend.h"

#include "Core/Expected.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/ITextureSlots.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureEnvMode.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <system_error>

namespace Client::Render
{
namespace
{
constexpr std::int32_t MultiplicativeMode = 1;       // 620c90: ZERO / SRC_COLOR.
constexpr std::int32_t AlphaMode = 2;                // 620a00: raw32 mode, distinct from ModelBlend.
constexpr std::int32_t AdditiveMode = 3;             // 620a90.
constexpr std::int32_t SubtractiveMode = 4;          // 620b10: ZERO / ONE_MINUS_SRC_COLOR.
constexpr std::int32_t InverseColorAdditiveMode = 5; // 620b90.
constexpr std::int32_t BlendedMode = 6;              // 620c10: SRC_ALPHA / ONE_MINUS_SRC_ALPHA.
// 621320 takes no UV: the L5 package always holds these four literals.
constexpr std::array<glm::vec2, 4> WorldQuadUv{glm::vec2{0, 1}, glm::vec2{1, 1}, glm::vec2{1, 0}, glm::vec2{0, 0}};
IParticleDrawBackend::Result Unsupported()
{
    return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
}

struct RasterState
{
    std::int32_t blend;
    std::uint8_t cull;
    std::uint8_t write;
    std::uint8_t alpha;
};

std::uint8_t PreserveCompatibleByte(std::uint8_t previous, std::uint8_t requested)
{
    return (previous != 0) == (requested != 0) ? previous : requested;
}

// Only what GlModelRenderer::SetDrawState/the alphaTest uniform proves in the current profile.
Core::Expected<RasterState, std::error_code> RasterOf(ModelBlend blend)
{
    switch (blend)
    {
    case ModelBlend::Opaque:
        return RasterState{0, 1, 1, 0};
    case ModelBlend::Alpha:
        return RasterState{AlphaMode, 0, 1, 1};
    case ModelBlend::Multiplicative:
        return RasterState{MultiplicativeMode, 0, 0, 0};
    case ModelBlend::Additive:
        return RasterState{AdditiveMode, 0, 0, 0};
    case ModelBlend::Shadow:
        return RasterState{0, 1, 0, 0};
    case ModelBlend::BlendedShadow:
        return RasterState{AlphaMode, 0, 0, 0};
    case ModelBlend::Subtractive:
        return RasterState{SubtractiveMode, 0, 0, 0};
    case ModelBlend::InverseColorAdditive:
        return RasterState{InverseColorAdditiveMode, 0, 0, 0};
    }
    return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
}

Core::Expected<ModelBlend, std::error_code> BlendOf(const ModelBatchParticleState& state)
{
    switch (state.blendMode)
    {
    case 0:
        return ModelBlend::Opaque;
    case MultiplicativeMode:
        return ModelBlend::Multiplicative;
    case AlphaMode:
        return ModelBlend::Alpha;
    case AdditiveMode:
        return ModelBlend::Additive;
    case SubtractiveMode:
        return ModelBlend::Subtractive;
    case InverseColorAdditiveMode:
        return ModelBlend::InverseColorAdditive;
    case BlendedMode:
        // 0x620c10's factors are BlendedShadow's; cull, mask, alpha test and texture travel separately in
        // ModelDrawState.
        return ModelBlend::BlendedShadow;
    }
    return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
}
} // namespace

ModelBatchParticleBackend::ModelBatchParticleBackend(std::reference_wrapper<ModelBatch> batch,
                                                     std::reference_wrapper<const ITextureSlots> textures,
                                                     ModelBatchParticleState initialState)
    : m_batch(batch), m_textures(textures), m_state(initialState), m_primitiveSpace(ModelCoordinateSpace::Eye)
{
}

ModelBatchParticleBackend::~ModelBatchParticleBackend()
{
    // The scratch was never published: dropping its mark neither fabricates End nor erases complete geometry.
    if (m_ownsPrimitive)
        m_batch.get().EndPrimitive();
}

IParticleDrawBackend::Result ModelBatchParticleBackend::PrepareMaterial()
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    return SynchronizeBatch();
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Alpha(bool writeDepth)
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.blendMode != AlphaMode)
    {
        m_state.blendMode = AlphaMode;
        const auto changed = PublishOperation(ModelStateOperation::Blend);
        if (!changed.HasValue())
            return changed;
    }
    const auto culled = Helper6208f0();
    if (!culled.HasValue())
        return culled;
    if (writeDepth && m_state.depthWrite == 0)
    {
        m_state.depthWrite = 1;
        const auto changed = PublishOperation(ModelStateOperation::DepthWrite);
        if (!changed.HasValue())
            return changed;
    }
    return MaterialTail(true, true);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::AlphaWriteDepth()
{
    return Alpha(true);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::AlphaInheritDepth()
{
    return Alpha(false);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Helper6208f0()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.cullEnabled == 0)
        return {};
    m_state.cullEnabled = 0;
    return PublishOperation(ModelStateOperation::Cull);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Additive()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.blendMode != AdditiveMode)
    {
        m_state.blendMode = AdditiveMode;
        const auto changed = PublishOperation(ModelStateOperation::Blend);
        if (!changed.HasValue())
            return changed;
    }
    const auto culled = Helper6208f0();
    if (!culled.HasValue())
        return culled;
    const auto masked = Helper6208b0();
    if (!masked.HasValue())
        return masked;
    return MaterialTail(false, false);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::SubtractivePrefix()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.blendMode == SubtractiveMode)
        return {};
    m_state.blendMode = SubtractiveMode;
    return PublishOperation(ModelStateOperation::Blend);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::BlendedPrefix()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.blendMode == BlendedMode)
        return {};
    m_state.blendMode = BlendedMode;
    return PublishOperation(ModelStateOperation::Blend);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Helper6208b0()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.depthWrite == 0)
        return {};
    m_state.depthWrite = 0;
    return PublishOperation(ModelStateOperation::DepthWrite);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::MaterialTail(bool alphaTest, bool enableFog)
{
    if ((m_state.alphaTest != 0) != alphaTest)
    {
        m_state.alphaTest = alphaTest ? 1 : 0;
        const auto changed = PublishOperation(ModelStateOperation::AlphaTest);
        if (!changed.HasValue())
            return changed;
    }
    if (m_state.texture2d == 0)
    {
        m_state.texture2d = 1;
        const auto changed = PublishOperation(ModelStateOperation::Texture2d);
        if (!changed.HasValue())
            return changed;
    }
    // 620a00/620b10 query a separate regional fog policy after these stores. The analyzed binary's direct stores to
    // 0x07d368a0 leave it at zero.
    if (enableFog && m_state.fogPolicy != 0)
        return Unsupported();
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::SubtractiveTail()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    return MaterialTail(false, true);
}
IParticleDrawBackend::Result ModelBatchParticleBackend::BlendedTail()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    return MaterialTail(false, true);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Type620850()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (m_state.depthTest)
        return {};
    m_state.depthTest = true; // 62085e: cache before glEnable(GL_DEPTH_TEST).
    return PublishOperation(ModelStateOperation::DepthTest);
}
IParticleDrawBackend::Result ModelBatchParticleBackend::Type620870()
{
    const auto prepared = PrepareMaterial();
    if (!prepared.HasValue())
        return prepared;
    if (!m_state.depthTest)
        return {};
    m_state.depthTest = false; // 62087e: cache before glDisable(GL_DEPTH_TEST).
    return PublishOperation(ModelStateOperation::DepthTest);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::TextureEnv(TextureEnvMode mode)
{
    // glTexEnvi between glBegin and glEnd is a GL error; 0x5489eb calls it outside the quad. Not part of the material:
    // it publishes its own axis without changing the material serial.
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    m_batch.get().SetTextureEnv(mode);
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::BindTexture(std::int32_t selector)
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    const auto synchronized = SynchronizeBatch();
    if (!synchronized.HasValue())
        return synchronized;
    if (m_state.textureSelector == selector)
        return {};
    // 620780 writes the key BEFORE resolving name/bind. A failure keeps that store and the previous binding; a later
    // hit neither retries the lookup nor invalidates by metadata.
    m_state.textureSelector = selector;
    if (selector < 0)
    {
        m_state.texture.reset();
        m_state.rawTextureName = std::uint32_t{0} - static_cast<std::uint32_t>(selector);
        return PublishOperation(ModelStateOperation::Binding);
    }
    // The public table keeps the full raw32; negatives already took the NEG32 boundary.
    const auto slot = m_textures.get().TextureSlotOf(static_cast<std::uint32_t>(selector));
    if (!slot)
        return Core::Unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    m_state.rawTextureName.reset();
    m_state.texture = *slot;
    return PublishOperation(ModelStateOperation::Binding);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::PublishOperation(ModelStateOperation operation)
{
    const auto blend = BlendOf(m_state);
    if (!blend.HasValue())
        return Core::Unexpected(blend.Error());
    ModelDrawState draw{m_state.texture, blend.Value(), m_state.depthTest, ModelCoordinateSpace::Eye,
                        m_state.rawTextureName};
    draw.cullEnabled = m_state.cullEnabled != 0;
    draw.depthWrite = m_state.depthWrite != 0;
    draw.alphaTest = m_state.alphaTest != 0;
    draw.texture2d = m_state.texture2d != 0;
    if (!draw.HasValidTextureBinding())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    m_batch.get().ApplyOperation(draw, operation);
    m_observedSerial = m_batch.get().StateSerial();
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::SynchronizeBatch()
{
    const std::uint64_t serial = m_batch.get().StateSerial();
    if (m_observedSerial == serial)
        return {};
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::state_not_recoverable));
    // Another producer or the batch's Begin does not prove which raw selector 620780 used. Knowledge is lost; no native
    // value is assigned and no identity is inferred from a physical slot.
    m_state.textureSelector.reset();
    m_state.texture.reset();
    m_state.rawTextureName.reset();
    const ModelDrawState& draw = m_batch.get().State();
    if (!draw.HasValidTextureBinding())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto raster = RasterOf(draw.blend);
    if (!raster.HasValue())
        return Core::Unexpected(raster.Error());
    m_state.blendMode = raster.Value().blend;
    // A descriptor publishes enablement, not a native byte. If the explicit byte already represents that state, keep
    // its non-canonical bits; only fix a capability contradiction.
    m_state.cullEnabled = PreserveCompatibleByte(
        m_state.cullEnabled, static_cast<std::uint8_t>(draw.cullEnabled.value_or(raster.Value().cull != 0)));
    m_state.depthWrite = PreserveCompatibleByte(
        m_state.depthWrite, static_cast<std::uint8_t>(draw.depthWrite.value_or(raster.Value().write != 0)));
    m_state.alphaTest = PreserveCompatibleByte(
        m_state.alphaTest, static_cast<std::uint8_t>(draw.alphaTest.value_or(raster.Value().alpha != 0)));
    if (draw.texture2d.has_value())
        m_state.texture2d = PreserveCompatibleByte(m_state.texture2d, *draw.texture2d ? 1 : 0);
    m_state.depthTest = draw.depthTest;
    m_state.texture = draw.texture;
    m_state.rawTextureName = draw.rawTextureName;
    // ModelDrawState does not publish color/UV/fogPolicy or the native texture2d cache: do not invent them.
    m_observedSerial = serial;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::CheckPrimitiveState() const
{
    if (!IsDrawing())
        return {};
    if (!m_ownsPrimitive)
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (m_observedSerial != m_batch.get().StateSerial())
        return Core::Unexpected(std::make_error_code(std::errc::state_not_recoverable));
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Begin()
{
    return BeginQuad(ModelCoordinateSpace::Eye);
}

IParticleDrawBackend::Result ModelBatchParticleBackend::BeginQuad(ModelCoordinateSpace space)
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (space == ModelCoordinateSpace::Eye && m_savedModel.has_value())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (space == ModelCoordinateSpace::Eye && m_model != IdentityModel)
        return Unsupported();
    const auto synchronized = SynchronizeBatch();
    if (!synchronized.HasValue())
        return synchronized;
    const auto opened = m_batch.get().BeginPrimitive();
    if (!opened.HasValue())
        return opened;
    m_ownsPrimitive = true;
    m_cornerCount = 0;
    m_primitiveSpace = space;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Color3(const glm::vec3& light)
{
    const auto checked = CheckPrimitiveState();
    if (!checked.HasValue())
        return checked;
    // glColor3fv also sets alpha 1. No clamping: the existing shader does it.
    m_state.color = {light.x, light.y, light.z, 1};
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Color4(const Color& color)
{
    const auto checked = CheckPrimitiveState();
    if (!checked.HasValue())
        return checked;
    m_state.color = color;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::TexCoord(glm::vec2 uv)
{
    const auto checked = CheckPrimitiveState();
    if (!checked.HasValue())
        return checked;
    m_state.uv = uv;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Vertex(const glm::vec3& position)
{
    const auto checked = CheckPrimitiveState();
    if (!checked.HasValue())
        return checked;
    if (!IsDrawing() || m_cornerCount == m_corners.size())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    m_corners[m_cornerCount] = ModelVertex{position, m_state.uv, m_state.color};
    ++m_cornerCount;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::End()
{
    const auto checked = CheckPrimitiveState();
    if (!checked.HasValue())
        return checked;
    if (!IsDrawing() || m_cornerCount != m_corners.size())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    const bool alpha = m_state.blendMode == AlphaMode && m_state.alphaTest != 0 && m_state.fogPolicy == 0;
    const bool additive = m_state.blendMode == AdditiveMode && m_state.depthWrite == 0 && m_state.alphaTest == 0;
    const bool subtractive = m_state.blendMode == SubtractiveMode && m_state.depthWrite == 0 &&
                             m_state.alphaTest == 0 && m_state.fogPolicy == 0;
    const bool blended =
        m_state.blendMode == BlendedMode && m_state.depthWrite == 0 && m_state.alphaTest == 0 && m_state.fogPolicy == 0;
    const auto blend = BlendOf(m_state);
    if (!blend.HasValue())
        return Core::Unexpected(blend.Error());
    if (m_primitiveSpace == ModelCoordinateSpace::Eye &&
        ((!alpha && !additive && !subtractive && !blended) || m_state.cullEnabled != 0 || m_state.texture2d == 0))
        return Unsupported();
    if (m_primitiveSpace == ModelCoordinateSpace::World && m_state.fogPolicy != 0 && !additive)
        return Unsupported();
    ModelDrawState draw{m_state.texture, blend.Value(), m_state.depthTest, m_primitiveSpace, m_state.rawTextureName};
    if (!draw.HasValidTextureBinding())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    if (m_primitiveSpace == ModelCoordinateSpace::Eye && !draw.texture && !draw.rawTextureName)
        return Core::Unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    draw.cullEnabled = m_state.cullEnabled != 0;
    draw.depthWrite = m_state.depthWrite != 0;
    draw.alphaTest = m_state.alphaTest != 0;
    draw.texture2d = m_state.texture2d != 0;
    m_batch.get().SetState(draw);
    m_observedSerial = m_batch.get().StateSerial();
    m_batch.get().Triangle({m_corners[0], m_corners[1], m_corners[2]});
    m_batch.get().Triangle({m_corners[0], m_corners[2], m_corners[3]});
    m_batch.get().EndPrimitive();
    m_ownsPrimitive = false;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::PushMatrix()
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (m_savedModel.has_value())
        return Unsupported();
    m_savedModel = m_model;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::Translate(float x, float y, float z)
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    // M*T for this translation profile: reuses the only proven transform kernel.
    const glm::vec3 position = Math::OriginalVectorTransform(glm::vec3{x, y, z}, m_model);
    m_model[3] = position.x;
    m_model[7] = position.y;
    m_model[11] = position.z;
    return {};
}

IParticleDrawBackend::Result ModelBatchParticleBackend::DrawWorldQuad(std::span<const glm::vec3, 4> corners,
                                                                      std::span<const glm::vec2, 4> uv)
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    const auto synchronized = SynchronizeBatch();
    if (!synchronized.HasValue())
        return synchronized;
    std::array<glm::vec3, 4> world{};
    std::array<glm::vec2, 4> capturedUv{};
    for (std::size_t i = 0; i < world.size(); ++i)
    {
        if (uv[i] != WorldQuadUv[i])
            return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
        world[i] = Math::OriginalVectorTransform(corners[i], m_model);
        capturedUv[i] = uv[i];
    }
    const auto begun = BeginQuad(ModelCoordinateSpace::World);
    if (!begun.HasValue())
        return begun;
    for (std::size_t i = 0; i < world.size(); ++i)
    {
        const auto mapped = TexCoord(capturedUv[i]);
        if (!mapped.HasValue())
            return mapped;
        const auto emitted = Vertex(world[i]);
        if (!emitted.HasValue())
            return emitted;
    }
    return End();
}

IParticleDrawBackend::Result ModelBatchParticleBackend::PopMatrix()
{
    if (IsDrawing())
        return Core::Unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (!m_savedModel.has_value())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    m_model = *m_savedModel;
    m_savedModel.reset();
    return {};
}

const Math::OriginalMatrix& ModelBatchParticleBackend::ModelMatrix() const
{
    return m_model;
}
bool ModelBatchParticleBackend::HasPushedMatrix() const
{
    return m_savedModel.has_value();
}

ModelBatch& ModelBatchParticleBackend::Batch() const
{
    return m_batch.get();
}
const ModelBatchParticleState& ModelBatchParticleBackend::State() const
{
    return m_state;
}
bool ModelBatchParticleBackend::IsDrawing() const
{
    return m_batch.get().HasOpenPrimitive();
}
} // namespace Client::Render
