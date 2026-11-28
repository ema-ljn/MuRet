#include "Effects/ParticleDrawServices.h"
#include "Models/IModelLibrary.h"
#include "Models/TextureMetadata.h"
#include "Core/Expected.h"
#include "Render/Billboard.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureEnvMode.h"
#include "Math/OriginalRotation.h"
#include "Particles/IParticleDrawingServices.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
#include <functional>
namespace Client::Effects
{
namespace
{
using Phase = Particles::ParticleDrawingPhase;
using Result = Particles::IParticleDrawingServices::Result;
Result Checked(Render::IParticleDrawBackend::Result result, Phase phase)
{
    if (!result.HasValue())
        return Core::Unexpected(
            Particles::ParticleDrawingError{Particles::ParticleError::MissingDrawingService, phase, result.Error()});
    return {};
}
} // namespace
ParticleDrawServices::ParticleDrawServices(std::reference_wrapper<const Models::IModelLibrary> models,
                                           std::reference_wrapper<Render::IParticleDrawBackend> backend,
                                           std::reference_wrapper<const Math::OriginalMatrix> view)
    : m_models(models), m_backend(backend), m_view(view)
{
}
Core::Expected<glm::vec2, Particles::ParticleError> ParticleDrawServices::Dimensions(std::int32_t image)
{
    const auto metadata = m_models.get().TextureMetadataOf(static_cast<std::uint32_t>(image));
    if (!metadata)
        return Core::Unexpected(Particles::ParticleError::MissingTextureSize);
    return metadata->size;
}
Core::Expected<std::uint8_t, Particles::ParticleError> ParticleDrawServices::MaterialFlag(std::int32_t type)
{
    const auto metadata = m_models.get().TextureMetadataOf(static_cast<std::uint32_t>(type));
    if (!metadata)
        return Core::Unexpected(Particles::ParticleError::MissingMaterialFlag);
    return metadata->components;
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::Material(std::uint8_t rawFlag)
{
    if (rawFlag == 3)
        return Checked(m_backend.get().Additive(), Phase::Material);
    return Checked(m_backend.get().AlphaInheritDepth(), Phase::Material);
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::Subtractive()
{
    const auto prefix = Checked(m_backend.get().SubtractivePrefix(), Phase::SubtractivePrefix);
    if (!prefix.HasValue())
        return prefix;
    const auto first = Checked(m_backend.get().Helper6208f0(), Phase::Helper6208f0);
    if (!first.HasValue())
        return first;
    const auto second = Checked(m_backend.get().Helper6208b0(), Phase::Helper6208b0);
    if (!second.HasValue())
        return second;
    return Checked(m_backend.get().SubtractiveTail(), Phase::SubtractiveTail);
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::Blended()
{
    const auto prefix = Checked(m_backend.get().BlendedPrefix(), Phase::BlendedPrefix);
    if (!prefix.HasValue())
        return prefix;
    const auto first = Checked(m_backend.get().Helper6208f0(), Phase::Helper6208f0);
    if (!first.HasValue())
        return first;
    const auto second = Checked(m_backend.get().Helper6208b0(), Phase::Helper6208b0);
    if (!second.HasValue())
        return second;
    return Checked(m_backend.get().BlendedTail(), Phase::BlendedTail);
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::Type620850()
{
    return Checked(m_backend.get().Type620850(), Phase::Type620850);
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::Type620870()
{
    return Checked(m_backend.get().Type620870(), Phase::Type620870);
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::TextureEnv(Render::TextureEnvMode mode)
{
    const auto changed = Checked(m_backend.get().TextureEnv(mode), Phase::TextureEnv);
    if (changed.HasValue())
        m_textureEnvOverride = mode == Render::TextureEnvMode::Add;
    return changed;
}
bool ParticleDrawServices::HasOpenTextureEnvOverride() const
{
    return m_textureEnvOverride;
}
Particles::IParticleDrawingServices::Result ParticleDrawServices::Billboard(Render::ParticleBillboardRequest request)
{
    const auto bound = Checked(m_backend.get().BindTexture(request.selector), Phase::Bind);
    if (!bound.HasValue())
        return bound;
    // P and view are consumed AFTER the binder.
    const auto corners = Render::MakeBillboardGeometry(m_view.get(), request.position.get(), request.width,
                                                       request.height, request.rotation, request.uv, request.uvSize);
    const auto begun = Checked(m_backend.get().Begin(), Phase::Begin);
    if (!begun.HasValue())
        return begun;
    const auto metadata = m_models.get().TextureMetadataOf(static_cast<std::uint32_t>(request.selector));
    if (!metadata)
        return Core::Unexpected(
            Particles::ParticleDrawingError{Particles::ParticleError::MissingColorFlag, Phase::ColorFlag, {}});
    Result colored;
    if (metadata->components == 3)
        colored = Checked(m_backend.get().Color3(request.light.get()), Phase::Color);
    else
    {
        const Render::Color color =
            Render::MakeBillboardColor(request.light.get(), request.selector, metadata->components);
        colored = Checked(m_backend.get().Color4(color), Phase::Color);
    }
    if (!colored.HasValue())
        return colored;
    for (const auto& corner : corners)
    {
        const auto uv = Checked(m_backend.get().TexCoord(corner.uv), Phase::TexCoord);
        if (!uv.HasValue())
            return uv;
        const auto vertex = Checked(m_backend.get().Vertex(corner.position), Phase::Vertex);
        if (!vertex.HasValue())
            return vertex;
    }
    return Checked(m_backend.get().End(), Phase::End);
}
} // namespace Client::Effects
