#include "World/ParticleDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleDrawParameters.h"
#include "World/IParticleDrawingServices.h"
#include "World/Particles.h"
#include "Render/ParticleBillboardRequest.h"
#include "Core/Expected.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
namespace Client::World
{
namespace
{
using Phase = ParticleDrawingPhase;
using Result = IParticleDrawingServices::Result;
constexpr std::int32_t CullOverrideType = 0x5ac;
constexpr std::int32_t DepthOverrideType = 0x5ed;
constexpr float LowerPassHeight = 350.0f;
constexpr float UpperPassHeight = 300.0f;
ParticleDrawingError BranchError(ParticleError reason)
{
    return {reason, Phase::Branch, {}};
}
struct DrawSelection
{
    ParticleDrawKind kind;
    bool cloud;
};
Core::Expected<ParticleDrawKind, ParticleDrawingError> PrepareSmoke(Particle& p, IParticleDrawingServices& services)
{
    const std::int32_t subtype = p.subtype;
    // Primer conjunto literal de548707..732; el resto continúa a la segunda guarda.
    switch (subtype)
    {
    case 2:
    case 5:
    case 12:
    case 14:
    case 15:
    case 20:
    case 21:
    case 29:
    {
        const auto sub = services.Subtractive();
        if (!sub.HasValue())
            return Core::Unexpected(sub.Error());
        break;
    }
    default:
        break;
    }
    // 548737 y548749 son lecturas separadas después de los respectivos servicios.
    const std::int32_t repeated = p.subtype;
    if (repeated == 37 || repeated == 38)
    {
        const auto sub = services.Subtractive();
        if (!sub.HasValue())
            return Core::Unexpected(sub.Error());
    }
    return p.subtype == 6 ? ParticleDrawKind::Unrotated : ParticleDrawKind::Common;
}
Result PrepareCloud(Particle& p, std::size_t index, std::reference_wrapper<const float> clock,
                    IParticleDrawingServices& services)
{
    const std::int32_t subtype = p.subtype;
    if (subtype != 8 && subtype != LoginCloudSubtype)
        return Core::Unexpected(BranchError(ParticleError::UnsupportedSubtype));
    if (subtype == LoginCloudSubtype)
    {
        const auto sub = services.Subtractive();
        if (!sub.HasValue())
            return sub;
    }
    UpdateParticleCloudDrawRotation(p, clock.get(), index);
    return {};
}
Core::Expected<DrawSelection, ParticleDrawingError> SelectBranch(Particle& p, std::size_t index,
                                                                 std::reference_wrapper<const float> clock,
                                                                 IParticleDrawingServices& services)
{
    const std::int32_t type = p.type;
    if (IsKalimaParticle(type, p.subtype))
        return DrawSelection{ParticleDrawKind::Common, false};
    switch (type)
    {
    case FireParticle:
        // 548803..1f: sólo estos seis subtipos salen del default acreditado.
        switch (p.subtype)
        {
        case 5:
        case 7:
        case 11:
        case 12:
        case 13:
            return Core::Unexpected(BranchError(ParticleError::UnsupportedSubtype));
        case 8:
            return DrawSelection{ParticleDrawKind::RotatedFire, false};
        default:
            return DrawSelection{ParticleDrawKind::Fire, false};
        }
    case BreathParticle:
    {
        const auto kind = PrepareSmoke(p, services);
        if (!kind.HasValue())
            return Core::Unexpected(kind.Error());
        return DrawSelection{kind.Value(), false};
    }
    case CloudParticle:
    {
        const auto cloud = PrepareCloud(p, index, clock, services);
        if (!cloud.HasValue())
            return Core::Unexpected(cloud.Error());
        return DrawSelection{ParticleDrawKind::Common, true};
    }
    case ShinyParticle:
    case FairySparkParticle:
        return DrawSelection{ParticleDrawKind::Common, false};
    case DropParticle:
        return DrawSelection{ParticleDrawKind::Drop, false};
    case ThunderParticle: // 548ca1→54889a: billboard común sin lectura del subtipo.
    case SparkParticle:
    case Particle67:
    case Particle68:
        // Destinos directos cerrados: no hay lectura de subtype en estas ramas.
        return DrawSelection{ParticleDrawKind::Common, false};
    default:
        return Core::Unexpected(BranchError(ParticleError::UnsupportedType));
    }
}
Result DrawBranch(Particle& p, glm::vec2 extents, std::size_t index, std::reference_wrapper<const float> clock,
                  IParticleDrawingServices& services)
{
    // 548bcf..be3: el subtipo se relee tras material y antes de consultar vida.
    if (p.type == OrbitParticle)
    {
        if (p.subtype != 4 && p.life == OrbitLife)
            return {};
        const auto shape = ParticleDrawParametersOf(p, extents, ParticleDrawKind::Common);
        const Render::ParticleBillboardRequest request{p.texture,    std::cref(p.position), shape.width,
                                                       shape.height, std::cref(p.light),    shape.rotation,
                                                       shape.uv,     shape.uvSize};
        return services.Billboard(request);
    }
    const auto selected = SelectBranch(p, index, clock, services);
    if (!selected.HasValue())
        return Core::Unexpected(selected.Error());
    const auto shape = ParticleDrawParametersOf(p, extents, selected.Value().kind);
    std::optional<glm::vec3> scratch;
    if (selected.Value().cloud)
    {
        const std::int32_t subtype = p.subtype;
        if (subtype == 8 || subtype == 9)
            scratch = ScaledParticleDrawLight(p);
    }
    const glm::vec3& light = scratch ? *scratch : p.light;
    const std::int32_t selector = p.texture;
    const Render::ParticleBillboardRequest request{selector,         std::cref(p.position), shape.width, shape.height,
                                                   std::cref(light), shape.rotation,        shape.uv,    shape.uvSize};
    return services.Billboard(request);
}
Result DrawOne(Particle& p, std::size_t index, std::reference_wrapper<const float> clock,
               IParticleDrawingServices& services)
{
    const std::int32_t image = p.texture;
    const auto dimensions = services.Dimensions(image);
    if (!dimensions.HasValue())
        return Core::Unexpected(ParticleDrawingError{dimensions.Error(), Phase::Dimensions, {}});
    const float width = static_cast<float>(static_cast<double>(dimensions.Value().x) * static_cast<double>(p.scale));
    const float resourceHeight = dimensions.Value().y;
    const std::int32_t type = p.type;
    const double heightWide = static_cast<double>(resourceHeight) * static_cast<double>(p.scale);
    const auto flag = services.MaterialFlag(type);
    if (!flag.HasValue())
        return Core::Unexpected(ParticleDrawingError{flag.Error(), Phase::MaterialFlag, {}});
    const float height = static_cast<float>(heightWide);
    const auto material = services.Material(flag.Value());
    if (!material.HasValue())
        return material;
    if (p.type == CullOverrideType && p.subtype == 6)
    {
        const auto special = services.Type620850();
        if (!special.HasValue())
            return special;
    }
    if (p.type == DepthOverrideType && p.subtype == 5)
    {
        const auto special = services.Type620870();
        if (!special.HasValue())
            return special;
    }
    return DrawBranch(p, {width, height}, index, clock, services);
}
} // namespace
Core::Expected<void, ParticleDrawingFailure> DrawParticlesChecked(Particles& particles, std::uint8_t mode,
                                                                  std::reference_wrapper<const float> clock,
                                                                  IParticleDrawingServices& services)
{
    for (std::size_t index = 0; index < ParticleCount; ++index)
    {
        Particle& p = particles.All()[index];
        if (!p.alive)
            continue;
        if (mode == 1 && p.position.z > LowerPassHeight)
            continue;
        if (mode == 2 && !(p.position.z > UpperPassHeight))
            continue;
        const auto result = DrawOne(p, index, clock, services);
        if (!result.HasValue())
            return Core::Unexpected(ParticleDrawingFailure{index, result.Error()});
    }
    return {};
}
} // namespace Client::World
