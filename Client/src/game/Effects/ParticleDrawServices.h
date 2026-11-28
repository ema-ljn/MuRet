#pragma once
#include "Particles/IParticleDrawingServices.h"
#include "Core/Expected.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
#include "Math/OriginalRotation.h"
#include "Render/TextureEnvMode.h"
#include <functional>
namespace Client::Render
{
class IParticleDrawBackend;
class ModelBatch;
struct ParticleBillboardRequest;
} // namespace Client::Render
namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Effects
{
// Borrows from the same CPU owner and the current context/view; owns no metadata or GL caches.
class ParticleDrawServices final : public Particles::IParticleDrawingServices
{
public:
    ParticleDrawServices(std::reference_wrapper<const Models::IModelLibrary> models,
                         std::reference_wrapper<Render::IParticleDrawBackend> backend,
                         std::reference_wrapper<const Math::OriginalMatrix> view);
    [[nodiscard]] Core::Expected<glm::vec2, Particles::ParticleError> Dimensions(std::int32_t image) override;
    [[nodiscard]] Core::Expected<std::uint8_t, Particles::ParticleError> MaterialFlag(std::int32_t type) override;
    [[nodiscard]] Result Material(std::uint8_t rawFlag) override;
    [[nodiscard]] Result Subtractive() override;
    [[nodiscard]] Result Blended() override;
    [[nodiscard]] Result Type620850() override;
    [[nodiscard]] Result Type620870() override;
    [[nodiscard]] Result TextureEnv(Render::TextureEnvMode mode) override;
    [[nodiscard]] Result Billboard(Render::ParticleBillboardRequest request) override;
    [[nodiscard]] bool HasOpenTextureEnvOverride() const;

private:
    const std::reference_wrapper<const Models::IModelLibrary> m_models;
    const std::reference_wrapper<Render::IParticleDrawBackend> m_backend;
    const std::reference_wrapper<const Math::OriginalMatrix> m_view;
    bool m_textureEnvOverride = false;
};

// 0x548550(mode), in the same eye pass, after the sprites: the single contract of Particles::DrawParticlesChecked,
// with the scene's services (ParticleDrawServices) over the batch adapter (Render::ModelBatchParticleBackend). The
// dimensions, the material and the color come from the metadata of `textures` (the resource's +20/+24/+28) and the
// binder from its TextureSlotOf; the square of 0x621400, from the view `view`. The mist's rotation changes with
// `worldClock` in ms and the real pool index. A place that fails cuts the pass after its prefix, without rollback: the
// earlier ones stay in the batch and the error says which one.
[[nodiscard]] Core::Expected<void, Particles::ParticleDrawingFailure>
DrawParticles(Render::ModelBatch& batch, Particles::Particles& particles, const Models::IModelLibrary& textures,
              const Math::OriginalMatrix& view, float worldClock = 0.0f, std::uint8_t mode = 0);
} // namespace Client::Effects
