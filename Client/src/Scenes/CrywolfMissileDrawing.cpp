#include "Scenes/CrywolfMissileDrawing.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
using Math::F32;
} // namespace

Core::Expected<void, std::string> DrawCrywolfMissileTail(World::ModelEffect& effect, std::uint64_t physicalSlot,
                                                         World::Particles& particles, World::FrameSprites& sprites,
                                                         Crt::CrtRandom& random,
                                                         std::span<const Math::BoneTransform> globalPose,
                                                         float worldClock)
{
    // 602626 transforma(0,1.5,0) con el hueso2 en un local muerto. Las altas leen la posición del efecto.
    World::PortableParticleTrigonometry trigonometry;
    const World::ParticleOwner owner{World::ParticleOwnerKind::ModelEffect, physicalSlot};
    const auto emit = [&](std::int32_t type, std::int32_t subtype, float scale) -> Core::Expected<void, std::string>
    {
        const auto created =
            particles.CreateChecked(type, effect.transform.position, effect.transform.angles, effect.light, subtype,
                                    scale, std::cref(worldClock), random, 0, trigonometry, owner);
        if (!created.HasValue())
            return Core::Unexpected(std::string("0x602642: alta de la cola ED/EE no portada"));
        return {};
    };
    constexpr float Shrink = 0.4f;
    effect.light = glm::vec3(1);
    for (int index = 0; index < 2; ++index)
        if (auto made = emit(World::MissileFireParticle, 8,
                             F32(static_cast<double>(effect.transform.scale) - static_cast<double>(Shrink)));
            !made.HasValue())
            return made;
    if (auto made = emit(World::BreathParticle, World::MissileSmokeSubtype, effect.transform.scale); !made.HasValue())
        return made;
    effect.light = {1, 0.4f, 0};
    if (auto made = emit(World::MissileFireParticle, 9,
                         F32(static_cast<double>(effect.transform.scale) - static_cast<double>(Shrink)));
        !made.HasValue())
        return made;
    // 6026cf..602764: cada canal conserva el producto x87 hasta su store. La matriz ausente es BSS0.
    constexpr int Glows = 10;
    constexpr float RedStep = 0.1f, GreenBase = 0.4f, GreenStep = 0.04f, SizeStep = 0.9f;
    for (int index = 0; index < Glows; ++index)
    {
        const double sample = static_cast<double>(index);
        effect.light = {F32(1.0 - sample * static_cast<double>(RedStep)),
                        F32(static_cast<double>(GreenBase) - sample * static_cast<double>(GreenStep)), 0};
        const glm::vec3 offset{F32(sample * 15.0), 1.5f, 0};
        const auto matrix = globalPose.size() > 1 ? globalPose[1] : Math::BoneTransform(0.0f);
        const auto point = Math::OriginalVectorTransform(offset, Math::OriginalMatrixOf(matrix));
        static_cast<void>(
            sprites.Create(0x5c6, point, F32(sample * static_cast<double>(SizeStep) + 3.0), effect.light, 0, 0));
    }
    return {};
}
} // namespace Client::Scenes
