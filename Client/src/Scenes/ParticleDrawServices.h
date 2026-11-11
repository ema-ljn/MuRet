#pragma once
#include "World/IParticleDrawingServices.h"
#include "Core/Expected.h"
#include "World/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
#include "Render/Models/OriginalRotation.h"
#include <functional>
namespace Client::Render
{
class IParticleDrawBackend;
struct ParticleBillboardRequest;
} // namespace Client::Render
namespace Client::Scenes
{
class IModelLibrary;
// Borrow del mismo owner CPU y del contexto/view vigentes; no posee metadata ni caches GL.
class ParticleDrawServices final : public World::IParticleDrawingServices
{
public:
    ParticleDrawServices(std::reference_wrapper<const IModelLibrary> models,
                         std::reference_wrapper<Render::IParticleDrawBackend> backend,
                         std::reference_wrapper<const Render::Models::OriginalMatrix> view);
    [[nodiscard]] Core::Expected<glm::vec2, World::ParticleError> Dimensions(std::int32_t image) override;
    [[nodiscard]] Core::Expected<std::uint8_t, World::ParticleError> MaterialFlag(std::int32_t type) override;
    [[nodiscard]] Result Material(std::uint8_t rawFlag) override;
    [[nodiscard]] Result Subtractive() override;
    [[nodiscard]] Result Blended() override;
    [[nodiscard]] Result Type620850() override;
    [[nodiscard]] Result Type620870() override;
    [[nodiscard]] Result Billboard(Render::ParticleBillboardRequest request) override;

private:
    const std::reference_wrapper<const IModelLibrary> m_models;
    const std::reference_wrapper<Render::IParticleDrawBackend> m_backend;
    const std::reference_wrapper<const Render::Models::OriginalMatrix> m_view;
};
} // namespace Client::Scenes
