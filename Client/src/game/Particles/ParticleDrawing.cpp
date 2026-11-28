#include "Particles/ParticleDrawing.h"
#include "Particles/EffectParticleKernels.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/SelectionParticleKernels.h"
#include "Particles/SmokeParticleKernels.h"
#include "Particles/TrialsParticleKernels.h"
#include "Particles/BloodCastleParticleKernels.h"
#include "Particles/ParticleDrawParameters.h"
#include "Particles/IParticleDrawingServices.h"
#include "Particles/Particles.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "Core/Expected.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
namespace Client::Particles
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
    // La 6ac/2: el billboard va con GL_ADD, y después vuelve GL_MODULATE.
    bool addedTexture = false;
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
// 0x548bf0: 7 y 10 restan (0x620b10) y caen a 0, 3 y 8, que giran con el reloj; los demás subtipos (la
// 622/1 de los objetos de la selección) van directo al billboard común.
Result PrepareCloud(Particle& p, std::size_t index, std::reference_wrapper<const float> clock,
                    IParticleDrawingServices& services)
{
    const std::int32_t subtype = p.subtype;
    switch (subtype)
    {
    case 7:
    case LoginCloudSubtype:
    {
        const auto sub = services.Subtractive();
        if (!sub.HasValue())
            return sub;
        [[fallthrough]];
    }
    case 0:
    case 3:
    case 8:
        UpdateParticleCloudDrawRotation(p, clock.get(), index);
        return {};
    default:
        return {};
    }
}
Core::Expected<DrawSelection, ParticleDrawingError> SelectBranch(Particle& p, std::size_t index,
                                                                 std::reference_wrapper<const float> clock,
                                                                 IParticleDrawingServices& services)
{
    const std::int32_t type = p.type;
    // 0x548644: la 6f2/3 va al default (caseD_5c5) y la 5f2/22 sale del caso del 0x5f2 sin restar ni ser el
    // /6 (0x548a64): las dos al billboard común.
    if (IsKalimaParticle(type, p.subtype) || IsTrialsParticle(type, p.subtype) ||
        (type == SwellLifeParticle && p.subtype == SwellLifeParticleSubtype))
        return DrawSelection{ParticleDrawKind::Common, false};
    switch (type)
    {
    case BlueTrailFireParticle:
    case FireParticle:
    case DeathSpiritFireParticle:
        // 0x548803..0x54881f: 5d9, 5db y 5dc comparten el caso; estos seis subtipos van al atlas de fuego girado por
        // +0x44 (0x548850) y los demás al de los ángulos (0x548821), cada uno con la textura de su tipo.
        switch (p.subtype)
        {
        case 5:
        case FireSwaySubtype:
        case 8:
        case FireLongSwaySubtype:
        case FireTallSubtype:
        case FireFountainSubtype:
            return DrawSelection{ParticleDrawKind::RotatedFire, false};
        default:
            return DrawSelection{ParticleDrawKind::Fire, false};
        }
    case BlazeParticle: // 0x5486b6: los argumentos del fuego de 0x548821 (un cuarto del ancho, el cuadro % 4, +0x20).
        return DrawSelection{ParticleDrawKind::Fire, false};
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
    case FlameParticle:      // 0x548644: sin caso propio, al billboard común (caseD_5c5).
    case ChaseLightParticle: // El default del segundo switch, al mismo caseD_5c5, con la textura de la creación.
        return DrawSelection{ParticleDrawKind::Common, false};
    case DropParticle:
        return DrawSelection{ParticleDrawKind::Drop, false};
    case BubbleParticle: // 0x5488f6 → 0x54899a, sin leer el subtipo.
        return DrawSelection{ParticleDrawKind::Bubble, false};
    case ExplosionParticle: // 0x548644 → 0x54864b, sin leer el subtipo.
        return DrawSelection{ParticleDrawKind::Explosion, false};
    case BloodParticle: // 0x548644 → 0x5487b9, sin leer el subtipo.
        return DrawSelection{ParticleDrawKind::Blood, false};
    case MagicSparkParticle:    // 548644, sin caso propio: billboard común.
    case MissileFireParticle:   // 548cb1=8 →54889a; /8 y/9 no dividen atlas.
    case PoundingBallParticle:  // 0x548d3a=8 → 0x54889a, billboard común.
    case TorchSparkParticle:    // 0x5488f6: la 6ab va al default, 0x54889a.
    case SkillShineParticle:    // 0x5488f6 → 0x54889a (el byte 8 de 0x548cf0), sin leer el subtipo.
    case HandShineParticle:     // 0x5488f6: la 5fd también (el byte 8 de 0x548cf7), sin leer el subtipo.
    case SiegeBlessingParticle: // 5488f6: el billboard común, sin leer el subtipo.
    case CinderParticle:        // 0x5488f6 → 0x54889a (el byte 8 de 0x548da7), sin leer el subtipo.
    case ShockWaveParticle:     // 0x5488f6 → 0x54889a (el byte 8 de 0x548de5), sin leer el subtipo.
        return DrawSelection{ParticleDrawKind::Common, false};
    case DustParticle:
    case ArrivalDustParticle:
    case RisingPuffParticle: // 0x548895: la 5f5 también, sin leer el subtipo.
    {
        // 0x548a51: la 5f3 va a 0x620c10 y al billboard común, sin leer el subtipo.
        const auto blended = services.Blended();
        if (!blended.HasValue())
            return Core::Unexpected(blended.Error());
        return DrawSelection{ParticleDrawKind::Common, false};
    }
    case TorchFlameParticle:
    {
        // 0x5489eb: la /2 pone glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD) antes de 0x620c10 y dibuja
        // el billboard con los argumentos del común (0x548a0d..0x548a33); las demás, 0x620c10 y el billboard común
        // (0x548a51).
        const bool added = p.subtype == BrightFlameSubtype;
        if (added)
        {
            const auto env = services.TextureEnv(Render::TextureEnvMode::Add);
            if (!env.HasValue())
                return Core::Unexpected(env.Error());
        }
        const auto blended = services.Blended();
        if (!blended.HasValue())
            return Core::Unexpected(blended.Error());
        return DrawSelection{ParticleDrawKind::Common, false, added};
    }
    case RingParticle:      // 0x548644: el anillo 0x600 va al default (0x54889a), sin leer el subtipo.
    case ThunderParticle:   // 548ca1→54889a: billboard común sin lectura del subtipo.
    case LightningParticle: // 0x548644: el índice 8 de 0x548c9c, al mismo 0x54889a.
    case SparkParticle:
    case Particle67:
    case Particle68:
    case FirstFlowerParticle:     // 0x5488f6 → 0x54889a (los bytes 0x12..0x14 de 0x548cf0), sin leer el subtipo.
    case FirstFlowerParticle + 1: // 0x609
    case LastFlowerParticle:
    case FlareColumnParticle: // 0x5488f6 → 0x54889a, sin leer el subtipo.
    case HoleParticle:        // 0x5488f6 → 0x54889a, sin leer el subtipo.
        // Destinos directos cerrados: no hay lectura de subtype en estas ramas.
        return DrawSelection{ParticleDrawKind::Common, false};
    default:
        return Core::Unexpected(BranchError(ParticleError::UnsupportedType));
    }
}
// 0x548a81..0x548bc7: 23+(slot%8) billboards, de la punta a la posición actual, con color cíclico.
Result DrawFirework(const Particle& p, glm::vec2 extents, std::size_t index, IParticleDrawingServices& services)
{
    const int length = static_cast<int>(index % 8) + 22;
    const int colorPhase = (p.subtype + p.life / 4) / 10;
    const int colorRest = (p.subtype + p.life / 4) % 10;
    const auto shape = ParticleDrawParametersOf(p, extents, ParticleDrawKind::Common);
    for (int segment = 0; segment <= length; ++segment)
    {
        glm::vec3 position;
        glm::vec3 light;
        for (int axis = 0; axis < 3; ++axis)
        {
            position[axis] =
                static_cast<float>(static_cast<double>(p.position[axis]) -
                                   (static_cast<double>(length - segment) * static_cast<double>(p.velocity[axis])) *
                                       static_cast<double>(0.1f));
            const double first =
                static_cast<double>(10 - colorRest) * static_cast<double>(p.light[(colorPhase + axis) % 3]);
            const double second =
                static_cast<double>(colorRest) * static_cast<double>(p.light[(colorPhase + axis + 1) % 3]);
            light[axis] = static_cast<float>(
                ((first + second) * (static_cast<double>(std::min(p.life, 10)) * static_cast<double>(0.001f))) *
                static_cast<double>(std::min(segment, 10)));
        }
        const Render::ParticleBillboardRequest request{p.texture,    std::cref(position), shape.width,
                                                       shape.height, std::cref(light),    shape.rotation,
                                                       shape.uv,     shape.uvSize};
        if (auto drawn = services.Billboard(request); !drawn.HasValue())
            return drawn;
    }
    return {};
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
    if (p.type == FireworkParticle)
        return DrawFirework(p, extents, index, services);
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
    const auto drawn = services.Billboard(request);
    if (!drawn.HasValue() || !selected.Value().addedTexture)
        return drawn;
    // 0x548a3b..0x548a4a: glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE) y al lugar siguiente.
    return services.TextureEnv(Render::TextureEnvMode::Modulate);
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
} // namespace Client::Particles
