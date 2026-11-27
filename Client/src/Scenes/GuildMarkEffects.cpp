#include "Scenes/GuildMarkEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "Kernel/WorldState.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>

// Cotejado con el PE c32bbd5f: 0x510ef0 (el llamador), 0x51e0cb (el alta del 0x140) y 0x529f0c (su paso).
namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;

// ((punto + z del dueño) − z del dueño) + 60 en la x87 (0x51e148..0x51e153 y 0x52a07a..0x52a086).
double MarkHeight(const glm::vec3& bonePoint, const ActorState& owner)
{
    constexpr double Above = 60.0; // 0x6b86cc, 60,0f
    return ((D(bonePoint.z) + D(owner.position.z)) - D(owner.position.z)) + Above;
}

// El lugar del 0x140 en la tabla de los efectos con modelo, que sus chispas guardan de dueño (+0x40): el ebx que
// empujan 0x529f5b y 0x52a134.
Core::Expected<World::ParticleOwner, std::string>
MarkAsOwner(const World::ModelEffect& effect, const HighEffectStepContext& context, std::string_view address)
{
    const auto pool = context.world.Map().effects.Effects();
    const auto found =
        std::ranges::find_if(pool, [&](const World::ModelEffect& candidate) { return &candidate == &effect; });
    if (found == pool.end())
        return Core::Unexpected(std::format("{}: el 0x140 fuera de su pool durable", address));
    return World::ParticleOwner{World::ParticleOwnerKind::ModelEffect,
                                static_cast<std::uint64_t>(found - pool.begin())};
}

// 0x52a0c2..0x52a16e: la chispa 0x5c5/5 de la vida 40..59, con el 5 que 0x5208a0 dejó en edi (0x5208df) de divisor y
// de subtipo, y la marca de dueño (0x52a134: push ebx), cuya posición lee al nacer (0x5403ad) para su partida.
Core::Expected<void, std::string> SparkTheMark(const World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr int FullTurn = Math::FullTurn;         // 0x168
    constexpr int RadiusSpan = 26, RadiusShift = 13; // 0x1a y 0xd
    constexpr int LiftSpan = 5;                      // edi
    constexpr double Base = 48.0;                    // 0x6ba220, 48,0f
    constexpr std::int32_t Spark = 0x5c5, SparkSubtype = 5;
    constexpr float SparkScale = 2.0f; // 0x40000000
    const float turn = static_cast<float>(context.random.Next() % FullTurn);
    const glm::vec3& position = effect.transform.position;
    glm::vec3 point{};
    const int side = context.random.Next() % RadiusSpan - RadiusShift;
    const auto sine = Math::Detail::SineDoubleProductPC53(D(turn), static_cast<double>(side));
    if (!sine)
        return Core::Unexpected(std::string("0x52a0f1: el seno del giro del 0x140 fuera del perfil PC53"));
    point.x = F32(*sine + D(position.x));
    const int depth = context.random.Next() % RadiusSpan - RadiusShift;
    const auto cosine = Math::Detail::CosineDoubleProductPC53(D(turn), static_cast<double>(depth));
    if (!cosine)
        return Core::Unexpected(std::string("0x52a11b: el coseno del giro del 0x140 fuera del perfil PC53"));
    point.y = F32(*cosine + D(position.y));
    const int lift = context.random.Next() % LiftSpan;
    point.z = F32(((D(position.z) + Base) - static_cast<double>(effect.life)) + static_cast<double>(lift));
    const auto owner = MarkAsOwner(effect, context, "0x52a134");
    if (!owner.HasValue())
        return Core::Unexpected(owner.Error());
    World::PortableParticleTrigonometry trigonometry;
    const auto made = context.world.Particles().CreateTowardChecked(
        Spark, point, effect.transform.angles, effect.light, SparkSubtype, SparkScale, std::cref(context.worldClock),
        context.random, trigonometry, owner.Value(), position);
    if (!made.HasValue())
        return Core::Unexpected(std::format("0x52a16e: la chispa 0x5c5/5 del 0x140 falla con el error {} (CL11)",
                                            static_cast<unsigned>(made.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> BeginTheGuildMark(World::ModelEffect& effect, std::size_t slot,
                                                    const ActorState& owner, const glm::vec3& bonePoint,
                                                    World::Particles& particles, Crt::CrtRandom& random,
                                                    float worldClock)
{
    constexpr float Tilt = 45.0f; // 0x42340000
    constexpr std::int32_t MarkLife = 130;
    constexpr float MarkScale = std::bit_cast<float>(0x404ccccdu); // 3,2f
    effect.alpha = 0.0f;
    effect.transform.angles.z = Tilt;
    glm::vec3& position = effect.transform.position;
    position.x = bonePoint.x;
    position.y = bonePoint.y;
    position.z = F32(MarkHeight(bonePoint, owner));
    effect.life = MarkLife;
    effect.transform.scale = MarkScale;
    effect.light = glm::vec3(1.0f);
    effect.start = position;
    constexpr int Sparks = 10;
    constexpr int FullTurn = Math::FullTurn, RadiusSpan = 20, RadiusBase = 15; // 0x168, 0x14 y 0xf
    constexpr std::int32_t Spark = 0x5c6, SparkSubtype = 4;
    constexpr float SparkScale = std::bit_cast<float>(0x3f19999au); // 0,6f
    World::PortableParticleTrigonometry trigonometry;
    const World::ParticleOwner sparkOwner{World::ParticleOwnerKind::ModelEffect, slot};
    for (int spark = 0; spark < Sparks; ++spark)
    {
        const float turn = static_cast<float>(random.Next() % FullTurn);
        glm::vec3 point{};
        const float side = static_cast<float>(random.Next() % RadiusSpan + RadiusBase);
        // 0x52e3a0 y 0x52e390: FSIN y FCOS del giro, retenidos en la x87 hasta el FMUL por el radio.
        const auto sine = Math::Detail::SineProductPC53(D(turn), side);
        if (!sine)
            return Core::Unexpected(std::string("0x51e1c6: el seno del giro del 0x140 fuera del perfil PC53"));
        point.x = F32(*sine + D(position.x));
        const float depth = static_cast<float>(random.Next() % RadiusSpan + RadiusBase);
        const auto cosine = Math::Detail::CosineProductPC53(D(turn), depth);
        if (!cosine)
            return Core::Unexpected(std::string("0x51e1ee: el coseno del giro del 0x140 fuera del perfil PC53"));
        point.y = F32(*cosine + D(position.y));
        point.z = position.z;
        // 0x540686: la 5c6/4 lee la posición de su dueño (+0x40 → +0x10) para su velocidad.
        const auto made =
            particles.CreateTowardChecked(Spark, point, effect.transform.angles, effect.light, SparkSubtype, SparkScale,
                                          std::cref(worldClock), random, trigonometry, sparkOwner, position);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x51e218: la chispa 0x5c6/4 del 0x140 falla con el error {}",
                                                static_cast<unsigned>(made.Error())));
    }
    return {};
}

bool GuildMarkIsDue(const ActorState& actor, float worldClock)
{
    if (actor.byte369 != 1)
        return false;
    // 0x510f00..0x510f2a: FCOMP con 0 (C3 también con un NaN) y, si no, el reloj menos +0x36c contra 5000 (JNE con C0 o
    // C3: igual, menos o sin orden, nada).
    constexpr double Period = 5000.0; // 0x6ba1c4, 5000,0f
    const float last = std::bit_cast<float>(actor.dword36c);
    return last == 0.0f || std::isnan(last) || D(worldClock) - D(last) > Period;
}

Core::Expected<void, std::string> RaiseTheGuildMark(ActorState& actor, const glm::vec3& bonePoint,
                                                    World::ModelEffects& effects, World::Particles& particles,
                                                    Crt::CrtRandom& random, float worldClock)
{
    if (!GuildMarkIsDue(actor, worldClock))
        return {};
    const World::ModelEffectArguments arguments{.type = GuildMarkEffect,
                                                .position = actor.position,
                                                .angles = actor.angles,
                                                .light = actor.light,
                                                .owner = &actor};
    if (const auto slot = effects.Begin(arguments); slot.has_value())
        if (auto begun =
                BeginTheGuildMark(effects.Effects()[*slot], *slot, actor, bonePoint, particles, random, worldClock);
            !begun.HasValue())
            return begun;
    actor.dword36c = std::bit_cast<std::uint32_t>(worldClock); // 0x510f5a
    return {};
}

Core::Expected<void, std::string> StepTheGuildMark(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    const auto* owner = static_cast<const ActorState*>(effect.ownerContext);
    if (owner == nullptr)
    {
        effect.life = 0; // 0x529f18
        return {};
    }
    constexpr float Opaque = 1.0f; // 0x6b87d4
    constexpr double Spin = 5.0;   // 0x6b87f8, 5,0f
    // 0x529f26: FCOMP con 1; JNE con C0 (menos o sin orden) salta el giro.
    if (effect.alpha >= Opaque)
        effect.transform.angles.z = F32(D(effect.transform.angles.z) + Spin);
    constexpr std::int32_t BurstFrom = 120, BurstTo = 125, FadeInFrom = 95, FadeOutFrom = 50;
    if (effect.life > BurstFrom)
    {
        if (effect.life < BurstTo)
        {
            constexpr int Bursts = 5;
            constexpr std::int32_t Burst = 0x5c5, BurstSubtype = 6;
            constexpr float BurstScale = 1.5f; // 0x3fc00000
            // 0x529f5b: push ebx, la marca de dueño; el paso de la 0x5c5/6 resta lo que se corrió desde su partida.
            const auto burstOwner = MarkAsOwner(effect, context, "0x529f5b");
            if (!burstOwner.HasValue())
                return Core::Unexpected(burstOwner.Error());
            for (int burst = 0; burst < Bursts; ++burst)
                if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, Burst,
                                                   effect.transform.position, effect.transform.angles, effect.light,
                                                   BurstSubtype, BurstScale, 0x529f71, burstOwner.Value());
                    !made.HasValue())
                    return Core::Unexpected(made.Error());
        }
    }
    else if (effect.life > FadeInFrom)
    {
        constexpr double FadeIn = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4, 0,4f
        effect.alpha = effect.alpha > Opaque ? Opaque : F32(D(effect.alpha) + FadeIn);
    }
    else if (effect.life < FadeOutFrom)
    {
        constexpr double FadeOut = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
        effect.alpha = effect.alpha >= 0.0f ? F32(D(effect.alpha) - FadeOut) : 0.0f;
    }
    // 0x529fe7..0x52a0aa: 0x4ec110 con el hueso 20 del dueño y el modelo como lo dejó el paso de su personaje.
    const glm::vec3 point = ActorBonePoint(*owner, GuildMarkBone, glm::vec3(0.0f), context.world);
    const ActorState& hero = context.world.Hero();
    glm::vec3& position = effect.transform.position;
    position.x = F32((D(owner->position.x) - D(hero.position.x)) + D(point.x));
    position.y = F32((D(owner->position.y) - D(hero.position.y)) + D(point.y));
    constexpr double Settle = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    constexpr double Rise = 0.5;                                                      // 0x6b882c, 0,5f
    const double height = MarkHeight(point, *owner);
    // FCOM: C0 con la meta por debajo o sin orden, un décimo; si no, la mitad.
    const bool above = !(height >= D(position.z));
    position.z = F32((height - D(position.z)) * (above ? Settle : Rise) + D(position.z));
    constexpr std::int32_t SparkFrom = 39, SparkTo = 60; // 0x27 y 0x3c
    if (effect.life > SparkFrom && effect.life < SparkTo)
        return SparkTheMark(effect, context);
    return {};
}
} // namespace Client::Scenes
