#include "Scenes/KundunFallEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/WizardEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// Cotejado con el PE c32bbd5f: las altas 0x51e234 (los pedazos), 0x51cf6d (el 0x127) y 0x518f51 (la vuelta del 0x128/2)
// y los pasos 0x52a17b y 0x5277ae.
namespace Client::Scenes
{
namespace
{
using Crt::CrtRandom;

using Math::D;
using Math::F32;

// FCOMP y FNSTSW con TEST AH, 0x40: C3, igual o sin orden.
bool EqualOrUnordered(float a, float b)
{
    return !(a < b) && !(a > b);
}

// FCOMP y FNSTSW con TEST AH, 1: C0, menor o sin orden.
bool BelowOrUnordered(double a, double b)
{
    return !(a >= b);
}

constexpr std::int32_t RockSubtype = 2, ShakingSubtype = 3, SinkingSubtype = 4, FadingSubtype = 5;
constexpr std::uint16_t NoKey = 0xffff;
constexpr double Hundredth = Math::HundredthAsDouble; // 0x6b88d4 y 0x6b86c0, 0,01f
constexpr float PieceScale = 2.0f;                    // 0x40000000

// 0x52a5fe..0x52a683: el alpha (+0x174) 0,01f menos.
void Fade(World::ModelEffect& effect)
{
    effect.alpha = F32(D(effect.alpha) - Hundredth);
}

// 0x52a4ab..0x52a5f9: el /3 con más de 385 de vida; el /2 no tiembla.
Core::Expected<void, std::string> Shake(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t StillLife = 0x181; // 385
    if (effect.subtype == RockSubtype || effect.life <= StillLife)
        return {};
    CrtRandom& random = context.random;
    if (random.Next() % 2 == 0)
    {
        constexpr int FullTurn = Math::FullTurn; // 0x168
        const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(random.Next() % FullTurn)};
        const glm::vec3 point = effect.transform.position;
        if (random.Next() % 2 == 0)
        {
            constexpr std::int32_t DustJoint = 0x6ee, DustSubtype = 8, DustKey = 0;
            constexpr float DustWidth = 50.0f; // 0x42480000
            if (auto made = CreateHighJoint(context.world, random, DustJoint, point, point, angles, DustSubtype,
                                            nullptr, DustWidth, 0x52a57b, DustKey);
                !made.HasValue())
                return made;
        }
    }
    constexpr int SmokeChance = 3, SmokeMask = 31, SmokeBase = 48;                       // 0x1f y 0x30
    constexpr double SmokeStep = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0,02f
    constexpr std::int32_t SmokeParticle = 0x5f5, SmokeSubtype = 2;
    if (random.Next() % SmokeChance != 0)
        return {};
    const float scale = F32(static_cast<double>((random.Next() & SmokeMask) + SmokeBase) * SmokeStep);
    const auto made =
        CreateHighParticle(context.world, random, context.worldClock, SmokeParticle, effect.transform.position,
                           effect.transform.angles, glm::vec3(0.5f), SmokeSubtype, scale, 0x52a5f1);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    return {};
}

// 0x52a1ae..0x52a259: por encima de 350, la caída.
void Drop(World::ModelEffect& effect, CrtRandom& random)
{
    glm::vec3& velocity = effect.velocity;
    if (EqualOrUnordered(velocity.x, 0.0f) && EqualOrUnordered(velocity.y, 0.0f) && EqualOrUnordered(velocity.z, 0.0f))
    {
        velocity.x = static_cast<float>((random.Next() % 2) * 2 - 2);
        velocity.y = static_cast<float>((random.Next() % 2) * 2 - 2);
        velocity.z = 0.0f;
    }
    constexpr double Gravity = 6.0; // 0x6b87f4
    velocity.z = F32(D(velocity.z) - Gravity);
}

// 0x52a26e..0x52a386: la onda de Kalima y las cien partículas 0x6f4/2 del golpe contra el suelo.
Core::Expected<void, std::string> Strike(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    CrtRandom& random = context.random;
    // 0x52a26e..0x52a293: ftol de y · 0,01f y después de x · 0,01f; 0x437860 no hace nada sin Kalima (0x723c48).
    const auto tileY = static_cast<std::int32_t>(D(effect.transform.position.y) * Hundredth);
    const auto tileX = static_cast<std::int32_t>(D(effect.transform.position.x) * Hundredth);
    constexpr std::int32_t WaveRadius = 2, WaveAmplitude = -1000; // 0xfffffc18
    if (auto* kalima = context.world.Map().kalima.get(); kalima != nullptr)
    {
        if (auto disturbed = kalima->Disturb(tileX, tileY, WaveRadius, WaveAmplitude, random); !disturbed.HasValue())
            return Core::Unexpected(std::format("0x52a293: {}", disturbed.Error()));
    }
    constexpr int Debris = 100, FullTurn = Math::FullTurn, ReachSpan = 30, ReachBase = 30, TiltSpan = 180;
    constexpr double Sink = 30.0; // 0x6b8858
    constexpr std::int32_t DebrisParticle = 0x6f4, DebrisSubtype = 2;
    const glm::vec3 dim{std::bit_cast<float>(0x3e99999au)}; // 0,3f
    for (int piece = 0; piece < Debris; ++piece)
    {
        glm::vec3 point = effect.transform.position;
        const auto turn = static_cast<float>(random.Next() % FullTurn);
        const auto reach = static_cast<float>(random.Next() % ReachSpan + ReachBase);
        // 0x52a2fe..0x52a312: FSIN y FCOS del giro (en grados, como radianes), retenidos hasta el FMUL por el radio.
        const auto sine = Math::Detail::SineProductPC53(D(turn), reach);
        const auto cosine = Math::Detail::CosineProductPC53(D(turn), reach);
        if (!sine || !cosine)
            return Core::Unexpected(std::string("0x52a2fe: el giro del pedazo de Kundun fuera del perfil PC53"));
        point.x = F32(*sine + D(point.x));
        point.y = F32(*cosine + D(point.y));
        point.z = F32(D(point.z) - Sink);
        glm::vec3 angles = effect.transform.angles;
        angles.z = F32(static_cast<double>(random.Next() % TiltSpan) + D(angles.z));
        const auto made = CreateHighParticle(context.world, random, context.worldClock, DebrisParticle, point, angles,
                                             dim, DebrisSubtype, 1.0f, 0x52a37d);
        if (!made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return {};
}

// 0x52a41e..0x52a487: la x de los ángulos baja hasta −30 y sube hasta 30, con la x copiada (+0x28) de sentido.
void Rock(World::ModelEffect& effect, CrtRandom& random)
{
    constexpr float Low = -30.0f, High = 30.0f; // 0x6ba1f4 y 0x6b8858
    constexpr int Swing = 3;
    float& pitch = effect.transform.angles.x;
    if (EqualOrUnordered(effect.copiedAngles.x, 0.0f))
    {
        if (pitch > Low)
            pitch = F32(D(pitch) - static_cast<double>(random.Next() % Swing + 1));
        else
            effect.copiedAngles.x = 1.0f;
        return;
    }
    if (BelowOrUnordered(D(pitch), D(High)))
        pitch = F32(static_cast<double>(random.Next() % Swing + 1) + D(pitch));
    else
        effect.copiedAngles.x = 0.0f;
}

// 0x52a191..0x52a4a6: el /2 y el /3.
Core::Expected<void, std::string> StepTheFallingPiece(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t Base = 0x5e, Step = 5;
    if (effect.life >= (Base - effect.byte89) * Step)
        return Shake(effect, context);
    constexpr float Floor = 350.0f; // 0x6b89ac
    if (effect.transform.position.z > Floor)
    {
        Drop(effect, context.random);
        return {};
    }
    constexpr std::uint32_t Landed = 0xc0400000u; // −3, la z de la velocidad después del primer golpe
    if (std::bit_cast<std::uint32_t>(effect.velocity.z) != Landed)
        if (auto struck = Strike(effect, context); !struck.HasValue())
            return struck;
    CrtRandom& random = context.random;
    constexpr int Spread = 40, Center = 20; // 0x28 y 0x14
    constexpr double Below = 50.0;          // 0x6b86b0
    glm::vec3 point = effect.transform.position;
    point.x = F32(static_cast<double>(random.Next() % Spread - Center) + D(point.x));
    point.y = F32(static_cast<double>(random.Next() % Spread - Center) + D(point.y));
    point.z = F32(D(point.z) - Below);
    constexpr std::int32_t GroundParticle = 0x607, GroundSubtype = 4;
    const auto made = CreateHighParticle(context.world, random, context.worldClock, GroundParticle, point,
                                         effect.transform.angles, effect.light, GroundSubtype, 1.0f, 0x52a400);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    effect.velocity = {0.0f, 0.0f, std::bit_cast<float>(Landed)};
    Rock(effect, random);
    constexpr std::int32_t FadeLife = 100;                                              // 0x64
    constexpr double FadeStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    if (effect.life < FadeLife)
        effect.alpha = F32(D(effect.alpha) - FadeStep);
    return {};
}
} // namespace

Core::Expected<void, std::string> BeginTheKundunPiece(World::ModelEffect& effect, CrtRandom& random,
                                                      const KundunTerrainHeight& terrainHeight)
{
    const std::int32_t subtype = effect.subtype;
    // La clave del argumento (+0x8a), leída con MOVSX antes de pisarla.
    const auto key = static_cast<double>(static_cast<std::int16_t>(effect.disturbanceState));
    if (subtype == 1)
    {
        constexpr std::int32_t ShardLife = 100; // 0x64
        effect.life = ShardLife;
        effect.transform.scale = PieceScale;
    }
    else if (subtype == RockSubtype || subtype == ShakingSubtype)
    {
        if (!terrainHeight)
            return Core::Unexpected(std::format("0x51e26e: el pedazo {:#x}/{} pide la altura del terreno (0x5f93c0)",
                                                effect.type, subtype));
        constexpr double Depth = 80.0;         // 0x6b9ffc
        constexpr std::int32_t RockLife = 500; // 0x1f4
        effect.start.x = effect.transform.position.x;
        effect.start.y = effect.transform.position.y;
        effect.start.z = F32(terrainHeight(effect.transform.position.x, effect.transform.position.y) - key - Depth);
        effect.velocity = glm::vec3(0.0f);
        effect.life = RockLife;
        effect.transform.scale = PieceScale;
        effect.copiedAngles.x = static_cast<float>(random.Next() % 2);
        if (subtype == RockSubtype)
            effect.copiedAngles.x = 0.0f;
    }
    else if (subtype == SinkingSubtype)
    {
        if (!terrainHeight)
            return Core::Unexpected(std::format("0x51e2ee: el pedazo {:#x}/{} pide la altura del terreno (0x5f93c0)",
                                                effect.type, subtype));
        constexpr std::int32_t SinkingLife = 170; // 0xaa
        effect.start.x = effect.transform.position.x;
        effect.start.y = effect.transform.position.y;
        effect.start.z = F32(terrainHeight(effect.transform.position.x, effect.transform.position.y) - key);
        effect.velocity = glm::vec3(0.0f);
        effect.animation.action = 0;
        effect.life = SinkingLife;
        effect.transform.scale = PieceScale;
    }
    else if (subtype == FadingSubtype)
    {
        constexpr std::int32_t FadingLife = 170; // 0xaa
        effect.animation.action = 0;
        effect.life = FadingLife;
        effect.transform.scale = PieceScale;
    }
    // 0x51e330..0x51e33d: la clave en 0xffff y el dueño (+0x108) en el del argumento, que el prefijo ya escribió.
    effect.disturbanceState = NoKey;
    return {};
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateTheKundunPiece(World::ModelEffects& effects, const World::ModelEffectArguments& arguments, CrtRandom& random,
                     const KundunTerrainHeight& terrainHeight)
{
    if (arguments.type < FirstKundunPieceEffect || arguments.type > LastKundunPieceEffect)
        return Core::Unexpected(
            std::format("0x518cf0: el alta de los pedazos de Kundun recibe el tipo {:#x}", arguments.type));
    const auto slot = effects.Begin(arguments);
    if (!slot.has_value())
        return slot;
    if (auto begun = BeginTheKundunPiece(effects.Effects()[*slot], random, terrainHeight); !begun.HasValue())
        return Core::Unexpected(begun.Error());
    return slot;
}

void BeginTheKundunEcho(World::ModelEffect& effect)
{
    constexpr float Speed = std::bit_cast<float>(0x3da3d70au); // 0,08f
    constexpr std::int32_t EchoLife = 200;                     // 0xc8
    constexpr float Scale = std::bit_cast<float>(0x3fe66666u); // 1,8f
    constexpr float Glow = 0.5f;                               // 0x3f000000
    effect.speed = Speed;
    effect.life = EchoLife;
    effect.objectState.blendMesh = -2;
    effect.transform.scale = Scale;
    effect.light = glm::vec3(Glow);
}

std::optional<std::size_t> CreateTheKundunEcho(World::ModelEffects& effects, std::size_t call)
{
    World::ModelEffect& owner = effects.Effects()[call];
    const auto slot = effects.Begin({.type = KundunEchoEffect,
                                     .position = owner.transform.position,
                                     .angles = owner.transform.angles,
                                     .light = owner.light,
                                     .subtype = 0,
                                     .owner = &owner,
                                     .key = NoKey});
    if (!slot.has_value())
        return slot;
    World::ModelEffect& echo = effects.Effects()[*slot];
    // El paso lee la clave (+0x8a) de su dueño, el 0x128/2.
    echo.owner.effect = &owner;
    BeginTheKundunEcho(echo);
    return slot;
}

Core::Expected<void, std::string> StepTheKundunPiece(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    switch (effect.subtype)
    {
    case 1:
        Fade(effect);
        return {};
    case RockSubtype:
    case ShakingSubtype:
        return StepTheFallingPiece(effect, context);
    case SinkingSubtype:
    {
        constexpr std::int32_t StillLife = 140; // 0x8c
        if (effect.life >= StillLife)
        {
            effect.animation.frame = 0.0f;
            return {};
        }
        Fade(effect);
        constexpr double Sink = 3.5; // 0x6b8ae4
        if (effect.transform.position.z > effect.start.z)
            effect.velocity.z = F32(D(effect.velocity.z) - Sink);
        else
            effect.velocity = glm::vec3(0.0f);
        return {};
    }
    case FadingSubtype:
    {
        constexpr std::int32_t StillLife = 150; // 0x96
        if (effect.life >= StillLife)
            effect.animation.frame = 0.0f;
        else
            Fade(effect);
        return {};
    }
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheKundunEcho(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.ownerContext != nullptr)
    {
        const World::ModelEffect* call = effect.owner.effect;
        if (call == nullptr)
            return Core::Unexpected(
                std::string("0x5277ba: el dueño (+0x108) del 0x127 no es un efecto con modelo (CL11)"));
        if (call->disturbanceState == 0)
        {
            effect.animation.frame = 0.0f;
            return {};
        }
        effect.ownerContext = nullptr;
        effect.hasOwner = false;
        effect.owner = {};
        return {};
    }
    CrtRandom& random = context.random;
    glm::vec3& position = effect.transform.position;
    constexpr float RiseFrame = 6.0f; // 0x6b87f4
    if (effect.animation.frame > RiseFrame)
    {
        constexpr double Climb = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0,8f
        constexpr int Spread = 120, Center = 60, Lift = 60;                              // 0x78, 0x3c y 0x3c
        constexpr std::int32_t SmokeParticle = 0x5f2, SmokeSubtype = 0x14, Puffs = 3;
        constexpr float SmokeScale = 10.0f; // 0x41200000
        effect.disturbanceState = static_cast<std::uint16_t>(effect.disturbanceState + 1U);
        position.z =
            F32(static_cast<double>(static_cast<std::int16_t>(effect.disturbanceState)) * Climb + D(position.z));
        glm::vec3 point = position;
        point.x = F32(static_cast<double>(random.Next() % Spread - Center) + D(point.x));
        point.y = F32(static_cast<double>(random.Next() % Spread - Center) + D(point.y));
        point.z = F32(static_cast<double>(random.Next() % Lift) + D(point.z));
        for (int puff = 0; puff < Puffs; ++puff)
        {
            const auto made =
                CreateHighParticle(context.world, random, context.worldClock, SmokeParticle, point,
                                   effect.transform.angles, glm::vec3(1.0f), SmokeSubtype, SmokeScale, 0x5278a7);
            if (!made.HasValue())
                return Core::Unexpected(made.Error());
        }
        return {};
    }
    effect.byte89 = static_cast<std::uint8_t>(effect.byte89 + 1U);
    const double count = static_cast<double>(effect.byte89);
    constexpr double Turn = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
    constexpr double Pull = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0,2f
    constexpr float Facing = 45.0f;                                                 // 0x6b8848
    float& yaw = effect.transform.angles.z;
    if (BelowOrUnordered(D(yaw), D(Facing)))
        yaw = F32(count * Turn + D(yaw));
    if (yaw > Facing)
        yaw = F32(D(yaw) - count * Turn);
    const WorldRuntime& runtime = context.world.Runtime();
    if (runtime.hero >= context.world.Actors().size())
        return Core::Unexpected(std::string("0x527919: el héroe publicado ([0x743a170]) no está en el pool"));
    const glm::vec3& hero = context.world.Hero().position;
    constexpr double Margin = 300.0; // 0x6b8900
    // 0x527919..0x5279e1: cuatro comparaciones con lo que dejó la anterior; las dos de C0 siguen también sin orden.
    if (D(hero.x) + Margin > D(position.x))
        position.x = F32(count * Pull + D(position.x));
    if (BelowOrUnordered(D(hero.x) + Margin, D(position.x)))
        position.x = F32(D(position.x) - count * Pull);
    if (D(hero.y) - Margin > D(position.y))
        position.y = F32(count * Pull + D(position.y));
    if (BelowOrUnordered(D(hero.y) - Margin, D(position.y)))
        position.y = F32(D(position.y) - count * Pull);
    constexpr int Spread = 1200, Center = 600, Spokes = 10; // 0x4b0, 0x258 y 0xa
    constexpr double SpokeTurn = 20.0;                      // 0x6b86d0
    glm::vec3 point = position;
    point.x = F32(static_cast<double>(random.Next() % Spread - Center) + D(point.x));
    point.y = F32(static_cast<double>(random.Next() % Spread - Center) + D(point.y));
    const glm::vec3 angles{0.0f, 0.0f, F32(static_cast<double>(random.Next() % Spokes) * SpokeTurn)};
    constexpr std::int16_t FireEffect = 0xc2;
    constexpr std::uint16_t FireKey = 0; // ESI en 0x527a6f
    const World::WizardEffectServices services =
        WizardEffectServicesOf(context.world, random,
                               WizardSceneParts{.particles = &context.world.Particles(),
                                                .worldClock = context.worldClock,
                                                .terrain = &context.world.Map().terrain});
    const auto fire = World::CreateWizardModelEffect(
        context.world.Map().effects, WizardArguments(FireEffect, point, angles, glm::vec3(1.0f), 0, nullptr, FireKey),
        random, services);
    if (!fire.HasValue())
        return Core::Unexpected(std::format("0x527a97: {}", fire.Error()));
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0,02f
    effect.transform.scale = F32(D(effect.transform.scale) + Growth);
    const std::int32_t shake = random.Next() % 8 - 8;
    if (context.services.cameraShake == nullptr)
        return Core::Unexpected(std::string("0x527acb: falta el temblor de la cámara (0x7d325c8)"));
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8, 0,1f
    *context.services.cameraShake = F32(static_cast<double>(shake) * Tenth);
    return {};
}
} // namespace Client::Scenes
