#include "Effects/RingEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Math/OriginalAngles.h"
#include "Kernel/ActorState.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/HighModelEffects.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// Cotejado con el PE c32bbd5f: 0x51c782 (el alta del 0x11b) y 0x526507 (su paso).
namespace Client::Effects
{
namespace
{
using Math::D;
using Math::F32;

constexpr std::int32_t SparkSubtype = 0, RingSubtype = 1;

// (rand() & 31) − 16 (AND 0x8000001f con el ajuste del signo; rand() no es negativo).
int Shake(Crt::CrtRandom& random)
{
    constexpr int ShakeMask = 31, ShakeCenter = 16;
    return (random.Next() & ShakeMask) - ShakeCenter;
}

// 0x526649..0x526691: 0x5f93c0 en la x87; cerca del terreno sube, lejos baja.
Core::Expected<void, std::string> KeepAboveTheGround(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (!context.services.terrainHeight)
        return Core::Unexpected(std::string("0x526650: el 0x11b/1 pide la altura del terreno (0x5f93c0)"));
    constexpr double Low = 100.0;  // 0x6b86b4
    constexpr double High = 400.0; // 0x6b87b0
    constexpr float Climb = -5.0f; // 0xc0a00000
    constexpr float Dive = 5.0f;   // 0x40a00000
    const double ground = context.services.terrainHeight(effect.transform.position.x, effect.transform.position.y);
    if (ground + Low > D(effect.transform.position.z))
    {
        effect.copiedAngles.x = 0.0f;
        effect.transform.angles.x = Climb;
    }
    // 0x526682: JE con C0 en 0; sin orden (NaN) también baja.
    if (!(ground + High >= D(effect.transform.position.z)))
    {
        effect.copiedAngles.x = 0.0f;
        effect.transform.angles.x = Dive;
    }
    return {};
}

// 0x51c789..0x51c79d, la /0.
void LightTheSpark(Effects::ModelEffect& effect)
{
    effect.life = 1;
    effect.transform.scale = std::bit_cast<float>(0x40133333u); // 0x51c791, 2,3f
}
} // namespace

std::optional<std::size_t> CreateTheRingSpark(Effects::ModelEffects& effects, const glm::vec3& position,
                                              const glm::vec3& angles, const glm::vec3& light)
{
    const auto slot = effects.Begin(
        {.type = RingEffect, .position = position, .angles = angles, .light = light, .subtype = SparkSubtype});
    if (slot)
        LightTheSpark(effects.Effects()[*slot]);
    return slot;
}

Core::Expected<void, std::string> BeginTheRing(Effects::ModelEffect& effect, const HighEffectContext& context)
{
    if (effect.subtype == SparkSubtype)
    {
        LightTheSpark(effect);
        return {};
    }
    if (effect.subtype != RingSubtype)
        return {};
    constexpr std::int32_t RingLife = 49;
    constexpr float RingScale = 2.0f;
    constexpr float RingLight = 0.5f;
    constexpr float Sink = -45.0f; // 0xc2340000
    constexpr std::int32_t BeamJoint = 0x618, FirstBeam = 0xa, SecondBeam = 0xb;
    constexpr float BeamWidth = 30.0f; // 0x41f00000
    effect.life = RingLife;
    effect.transform.scale = RingScale;
    effect.objectState.blendMesh = Models::EveryMeshBlends;
    effect.objectState.light = RingLight;
    effect.copiedAngles = glm::vec3(0.0f);
    effect.velocity = {0.0f, Sink, 0.0f};
    const glm::vec3 position = effect.transform.position;
    effect.start = position;
    effect.siegeTarget = position;
    effect.point40 = position;
    effect.point4c = position;
    for (const auto& [beam, address] : {std::pair{FirstBeam, 0x51c857u}, std::pair{SecondBeam, 0x51c875u}})
        if (auto made = CreateHighJoint(context.world, context.random, BeamJoint, position, position,
                                        effect.transform.angles, beam, &effect, BeamWidth, address);
            !made.HasValue())
            return made;
    return {};
}

Core::Expected<void, std::string> StepTheRing(Effects::ModelEffect& effect, float luminosity,
                                              const HighEffectStepContext& context)
{
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (effect.subtype != RingSubtype || owner == nullptr)
        return {};
    constexpr std::int32_t MarkEvery = 10;
    if (effect.life % MarkEvery == 0)
        effect.siegeTarget = effect.transform.position;
    constexpr double Hover = 200.0;   // 0x6b8928
    constexpr float TurnStep = 10.0f; // 0x41200000
    effect.start = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Hover)};
    static_cast<void>(Math::MoveToward(effect.transform.position, effect.transform.angles, effect.start, TurnStep));
    constexpr double PitchShake = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0,2f
    constexpr double YawShake = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));   // 0x6b8a38, 0,8f
    constexpr double PitchBrake = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0,6f
    const int pitch = Shake(context.random);
    effect.copiedAngles.x = F32(static_cast<double>(pitch) * PitchShake + D(effect.copiedAngles.x));
    const int yaw = Shake(context.random);
    effect.copiedAngles.z = F32(static_cast<double>(yaw) * YawShake + D(effect.copiedAngles.z));
    effect.transform.angles.x = F32(D(effect.transform.angles.x) + D(effect.copiedAngles.x));
    effect.transform.angles.z = F32(D(effect.copiedAngles.z) + D(effect.transform.angles.z));
    effect.copiedAngles.x = F32(D(effect.copiedAngles.x) * PitchBrake);
    effect.copiedAngles.z = F32(D(effect.copiedAngles.z) * YawShake);
    constexpr std::int32_t FadingLife = 10;
    if (effect.life < FadingLife)
    {
        constexpr double AlphaFade = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)); // 0x6ba26c, 0,909f
        constexpr double LightFade = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98, 0,833f
        constexpr float Ceiling = 80.0f;                                                     // 0x6b9ffc
        constexpr double Rise = 5.0;                                                         // 0x6b87f8
        effect.alpha = F32(D(effect.alpha) * AlphaFade);
        effect.objectState.light = F32(D(effect.objectState.light) * LightFade);
        if (!(effect.transform.angles.x > Ceiling))
            effect.transform.angles.x = F32(D(effect.transform.angles.x) + Rise);
    }
    else if (auto kept = KeepAboveTheGround(effect, context); !kept.HasValue())
    {
        return kept;
    }
    constexpr std::int32_t Spark = 0x5d9, RingSparkSubtype = 5;
    constexpr float SparkScale = std::bit_cast<float>(0x3f99999au); // 0x526696, 1,2f
    if (auto made =
            CreateHighParticle(context.world, context.random, context.worldClock, Spark, effect.transform.position,
                               effect.transform.angles, glm::vec3(1.0f), RingSparkSubtype, SparkScale, 0x5266a8);
        !made.HasValue())
        return Core::Unexpected(made.Error());
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));  // 0x6b87b8, 0,1f
    const glm::vec3 color{luminosity, F32(D(luminosity) * Green), F32(D(luminosity) * Blue)};
    constexpr std::int32_t Radius = 4;
    return WorldObjects::AddDynamicLight(context.services.lighting, effect.transform.position, color, Radius,
                                         "0x5266dd");
}
} // namespace Client::Effects
