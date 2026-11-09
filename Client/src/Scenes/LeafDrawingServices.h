#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ILeafDrawingServices.h"
#include "Scenes/ParticleDrawServices.h"

#include <cstdint>
#include <functional>
#include <system_error>

namespace Client::Render
{
class ModelBatch;
class ModelBatchParticleBackend;
} // namespace Client::Render
namespace Client::Scenes
{
class IModelLibrary;
struct LeafBillboardRequest;

// Provider durable de5306a0: genérico World con Push/Translate/Quad/Pop y Map2 Eye con621400.
// Comparte backend/biblioteca/vista, sin pools/clock/RNG. Cargas en GameModels::LoadTextures con
// LorenciaLeafTextureSet o DaviasLeafTextureSet en el punto real de carga, nunca durante Draw.
class LeafDrawingServices final : public ILeafDrawingServices
{
public:
    LeafDrawingServices(std::reference_wrapper<const IModelLibrary> models,
                        std::reference_wrapper<Render::ModelBatchParticleBackend> backend,
                        std::reference_wrapper<const Render::Models::OriginalMatrix> view);
    LeafDrawingServices(const LeafDrawingServices&) = delete;
    LeafDrawingServices& operator=(const LeafDrawingServices&) = delete;

    [[nodiscard]] Core::Expected<void, std::error_code> Blend(World::LeafBlend blend) override;
    [[nodiscard]] Core::Expected<void, std::error_code> WhiteColor() override;
    [[nodiscard]] Core::Expected<void, std::error_code> BindTexture(std::int32_t selector) override;
    [[nodiscard]] Core::Expected<void, std::error_code> PushMatrix() override;
    [[nodiscard]] Core::Expected<void, std::error_code> Translate(float x, float y, float z) override;
    [[nodiscard]] Core::Expected<void, std::error_code> Quad(Render::ModelBatch& batch,
                                                             const World::LeafQuad& quad) override;
    [[nodiscard]] Core::Expected<void, std::error_code> Billboard(Render::ModelBatch& batch,
                                                                  const LeafBillboardRequest& request) override;
    [[nodiscard]] Core::Expected<void, std::error_code> PopMatrix() override;

private:
    const std::reference_wrapper<Render::ModelBatchParticleBackend> m_backend;
    ParticleDrawServices m_particles;
};
} // namespace Client::Scenes
