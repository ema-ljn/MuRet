#pragma once
#include "Particles/IParticleDrawingServices.h"
#include "Core/Expected.h"
#include "Particles/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
#include "Math/OriginalRotation.h"
#include "Render/TextureEnvMode.h"
#include <functional>
namespace Client::Render
{
class IParticleDrawBackend;
struct ParticleBillboardRequest;
} // namespace Client::Render
namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Scenes
{
// Borrow del mismo owner CPU y del contexto/view vigentes; no posee metadata ni caches GL.
class ParticleDrawServices final : public World::IParticleDrawingServices
{
public:
    ParticleDrawServices(std::reference_wrapper<const Models::IModelLibrary> models,
                         std::reference_wrapper<Render::IParticleDrawBackend> backend,
                         std::reference_wrapper<const Math::OriginalMatrix> view);
    [[nodiscard]] Core::Expected<glm::vec2, World::ParticleError> Dimensions(std::int32_t image) override;
    [[nodiscard]] Core::Expected<std::uint8_t, World::ParticleError> MaterialFlag(std::int32_t type) override;
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
} // namespace Client::Scenes
