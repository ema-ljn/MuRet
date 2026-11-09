#include "Scenes/LeafDrawingServices.h"

#include "Core/Expected.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/ParticleBillboardRequest.h"
#include "Scenes/ParticleDrawServices.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "World/Leaves.h"
#include "World/ParticleDrawing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <system_error>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::error_code>;
Result Unsupported()
{
    return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
}
} // namespace

LeafDrawingServices::LeafDrawingServices(std::reference_wrapper<const IModelLibrary> models,
                                         std::reference_wrapper<Render::ModelBatchParticleBackend> backend,
                                         std::reference_wrapper<const Render::Models::OriginalMatrix> view)
    : m_backend(backend), m_particles(models, std::ref(backend.get()), view)
{
}

Result LeafDrawingServices::Blend(World::LeafBlend blend)
{
    if (blend == World::LeafBlend::Alpha)
        return m_backend.get().AlphaWriteDepth();
    if (blend == World::LeafBlend::Additive)
        return m_backend.get().Additive();
    return Unsupported();
}
Result LeafDrawingServices::WhiteColor()
{
    return m_backend.get().Color3(glm::vec3{1, 1, 1});
}
Result LeafDrawingServices::BindTexture(std::int32_t selector)
{
    return m_backend.get().BindTexture(selector);
}
Result LeafDrawingServices::PushMatrix()
{
    return m_backend.get().PushMatrix();
}
Result LeafDrawingServices::Translate(float x, float y, float z)
{
    return m_backend.get().Translate(x, y, z);
}
Result LeafDrawingServices::Quad(Render::ModelBatch& batch, const World::LeafQuad& quad)
{
    if (&batch != &m_backend.get().Batch())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    // L5 ya rotó los corners. quad.translation copia lo YA ejecutado por Translate:
    // se usa el modelo vigente del backend y no se suma esa traslación una segunda vez.
    return m_backend.get().DrawWorldQuad(quad.corners, quad.uv);
}
Result LeafDrawingServices::PopMatrix()
{
    return m_backend.get().PopMatrix();
}

Result LeafDrawingServices::Billboard(Render::ModelBatch& batch, const LeafBillboardRequest& request)
{
    if (&batch != &m_backend.get().Batch())
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    // Captura todos los escalares ANTES del binder interno de D2. P/C permanecen prestados y
    // aliasables: D2 prepara geometría una vez tras bind y consume metadata/luz después de Begin.
    const Render::ParticleBillboardRequest captured{request.selector,
                                                    std::cref(request.position),
                                                    request.width,
                                                    request.height,
                                                    std::cref(request.light),
                                                    request.rotation,
                                                    request.uv,
                                                    request.uvSize};
    const auto drawn = m_particles.Billboard(captured);
    if (drawn.HasValue())
        return {};
    if (drawn.Error().backendError)
        return Core::Unexpected(drawn.Error().backendError);
    return Core::Unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
}
} // namespace Client::Scenes
