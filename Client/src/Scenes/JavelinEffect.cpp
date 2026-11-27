#include "Scenes/JavelinEffect.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51dacb (el alta del 0x139) y 0x529b86 (su paso).
namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;

constexpr double Lift = 150.0; // 0x6b87ac

// 0x529c8e..0x529cc5 y 0x529d2b..0x529d62: la velocidad girada por los ángulos de +0x28, sumada a la posición.
void Advance(World::ModelEffect& effect)
{
    const glm::vec3 step = Math::OriginalVectorRotate(effect.velocity, Math::OriginalAngleMatrix(effect.copiedAngles));
    glm::vec3& position = effect.transform.position;
    position = {F32(D(step.x) + D(position.x)), F32(D(step.y) + D(position.y)), F32(D(step.z) + D(position.z))};
}

// 0x529bc9..0x529d06, por debajo de 25 de vida.
Core::Expected<void, std::string> Hunt(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t HuntLife = 20;
    constexpr double Near = 100.0;                                                   // 0x6b86b4
    constexpr double Swell = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 0x6b8aec, 0,04f
    constexpr std::int32_t TrailEvery = 3;
    constexpr std::int32_t Trail = 0x6ad, TrailSubtype = 1;
    constexpr double Acceleration = 1.5; // 0x6b8b94
    constexpr int SpinSpan = 30;
    constexpr double SpinBase = 30.0; // 0x6b8858
    constexpr double Floor = -50.0;   // 0x6ba1f8
    constexpr double Brake = 8.0;     // 0x6b86c4
    if (effect.life < HuntLife)
    {
        const double distance =
            Math::MoveToward(effect.transform.position, effect.copiedAngles, effect.start, effect.speed);
        // 0x529bf9..0x529c38: FCOMP con 100 (C0, también sin orden).
        bool trail = true;
        if (!(distance >= Near))
        {
            effect.transform.position = effect.start;
            effect.transform.scale = F32(D(effect.transform.scale) + Swell);
            trail = effect.life % TrailEvery == 0;
        }
        if (trail)
        {
            if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, Trail,
                                               effect.transform.position, effect.transform.angles, effect.light,
                                               TrailSubtype, 1.0f, 0x529c69);
                !made.HasValue())
                return Core::Unexpected(made.Error());
        }
    }
    effect.speed = F32(D(effect.speed) + Acceleration);
    Advance(effect);
    effect.turnDirection = F32(static_cast<double>(context.random.Next() % SpinSpan) + SpinBase);
    // 0x529ce7..0x529cf8: FCOMP con −50; con C0 o C3 (o sin orden), nada.
    if (D(effect.velocity.y) > Floor)
        effect.velocity.y = F32(D(effect.velocity.y) - Brake);
    return {};
}
} // namespace

Core::Expected<void, std::string> BeginTheJavelin(World::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr std::int32_t Life = 35;                          // 0x51dacb
    constexpr float Spin = 2.0f;                               // 0x51dad2, +0xe4
    constexpr float Speed = 10.0f;                             // 0x51dadc, +0xd8
    constexpr float Scale = std::bit_cast<float>(0x3f99999au); // 0x51dae6, 1,2f
    constexpr float Push = -5.0f;                              // 0x51daf3, +0xd0
    constexpr int SpreadSpan = 80, SpreadBase = 10;
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x51db1b: el 0x139 lee la posición de su dueño (+0x108) y no tiene"));
    effect.life = Life;
    effect.turnDirection = Spin;
    effect.speed = Speed;
    effect.transform.scale = Scale;
    effect.velocity = {0.0f, Push, 0.0f};
    effect.copiedAngles = effect.transform.angles;
    effect.start = owner->position;
    effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
    // 0x51db4e..0x51db6e: FILD r, FILD subtipo, FMUL, FSUB r y FADD la z de +0x28.
    const auto spread = static_cast<double>(context.random.Next() % SpreadSpan + SpreadBase);
    effect.copiedAngles.z = F32(static_cast<double>(effect.subtype) * spread - spread + D(effect.copiedAngles.z));
    return {};
}

Core::Expected<void, std::string> StepTheJavelin(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3c75c28fu)); // 0x6b8a24, 0,015f
    constexpr std::int32_t RiseLife = 25;
    constexpr double Climb = 10.0;                                                  // 0x6b87a8
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b8a34 y 0x6b87b8, 0,1f
    constexpr float Wave = 30.0f;                                                   // 0x6b8858
    constexpr std::int32_t RisingSubtype = 1, SinkingSubtype = 2;
    constexpr glm::vec3 Glow{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    constexpr std::int32_t GlowRadius = 2;
    if (const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext); owner != nullptr)
        effect.start = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Lift)};
    effect.transform.scale = F32(D(effect.transform.scale) + Growth);
    if (effect.life >= RiseLife)
    {
        effect.turnDirection = F32(D(effect.turnDirection) + Climb);
        Advance(effect);
    }
    else if (auto hunted = Hunt(effect, context); !hunted.HasValue())
        return hunted;
    // 0x529d65..0x529d7d.
    effect.transform.angles.z = F32(D(effect.turnDirection) + D(effect.transform.angles.z));
    const float fade = F32(static_cast<double>(effect.life) * Fade);
    effect.objectState.light = fade;
    effect.alpha = fade;
    // 0x529d83..0x529db7: el FSIN de vida · 0,1f por 30 se calcula con cualquier subtipo; solo el /1 y el /2 lo usan.
    if (effect.subtype == RisingSubtype || effect.subtype == SinkingSubtype)
    {
        const auto wave = Math::Detail::SineProductPC53(static_cast<double>(effect.life) * Fade, Wave);
        if (!wave)
            return Core::Unexpected(std::string("0x529d92: el seno del 0x139 no cabe en el perfil PC53"));
        effect.transform.position.z =
            effect.subtype == RisingSubtype ? F32(*wave + D(effect.start.z)) : F32(D(effect.start.z) - *wave);
    }
    return World::AddDynamicLight(context.services.lighting, effect.transform.position, Glow, GlowRadius, "0x529de1");
}
} // namespace Client::Scenes
