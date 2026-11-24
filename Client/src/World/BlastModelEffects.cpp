#include "World/BlastModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Math/X87.h"
#include "Render/Models/Animation.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "World/Ftol.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/KalimaParticleKernels.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::World
{
namespace
{
using Crt::CrtRandom;

constexpr std::int16_t ProtectionEffect = 0xe4;     // Protect1
constexpr std::int16_t FirstBigStoneEffect = 0xe5;  // BigStone1
constexpr std::int16_t SecondBigStoneEffect = 0xe6; // BigStone2
constexpr std::int16_t FirstIceRockEffect = 0xe7;   // 0xe7..0xe9, sin modelo cargado
constexpr std::int16_t LastIceRockEffect = 0xe9;
constexpr std::int16_t MagicCircleEffect = 0xec; // MagicCircle1
constexpr std::int16_t FallingRockEffect = 0xf2;
constexpr std::int16_t SwordTrailEffect = 0xf3;     // suelta un 0xf4 por paso
constexpr std::int16_t SpinningItemEffect = 0xf4;   // el ítem del dueño, girando (0x52d620)
constexpr std::int16_t BlastEffect = 0xf5;          // Blast1
constexpr std::int16_t InfernoEffect = 0xf6;        // Inferno1
constexpr std::int16_t PiercingTrailEffect = 0x105; // Piercing
constexpr std::int16_t DeathStabEffect = 0x106;     // sin modelo cargado
constexpr std::int16_t WaveForceEffect = 0x107;     // WaveForce
constexpr std::int16_t BlizzardEffect = 0x108;      // Blizzard
constexpr std::int16_t ShieldEffect = 0x109;        // Protect2
constexpr std::int16_t FirstGateDebrisEffect = 0x10c;
constexpr std::int16_t SecondGateDebrisEffect = 0x10d;
constexpr std::int16_t RidingSpearEffect = 0x10f; // RidingSpear1
constexpr std::int16_t ComboEffect = 0x112;
constexpr std::int16_t AirForceEffect = 0x113; // AirForce
constexpr std::int32_t BigStoneOwnerSubtype = 5;
constexpr std::int32_t PiercingChildSubtype = 1;
constexpr std::uint8_t NoAction = 0xff;
constexpr std::int16_t HiddenBlend = -2; // +0x64 en −2 (0xfffffffe)

using Math::D;
using Math::F32;

std::string Cut(std::string_view address, std::string_view what)
{
    return std::format("{}: {} (CL11)", address, what);
}

// +0x108 leído en vivo: un efecto de dueño da sus campos; si no, los punteros que prestó quien creó el efecto.
const glm::vec3* OwnerPosition(const ModelEffect& effect)
{
    if (effect.owner.position != nullptr)
        return effect.owner.position;
    return effect.owner.effect != nullptr ? &effect.owner.effect->transform.position : nullptr;
}

const glm::vec3* OwnerAngles(const ModelEffect& effect)
{
    if (effect.owner.angles != nullptr)
        return effect.owner.angles;
    return effect.owner.effect != nullptr ? &effect.owner.effect->transform.angles : nullptr;
}

const bool* OwnerAlive(const ModelEffect& effect)
{
    if (effect.owner.alive != nullptr)
        return effect.owner.alive;
    return effect.owner.effect != nullptr ? &effect.owner.effect->alive : nullptr;
}

const bool* OwnerSeen(const ModelEffect& effect)
{
    if (effect.owner.seen != nullptr)
        return effect.owner.seen;
    return effect.owner.effect != nullptr ? &effect.owner.effect->seen : nullptr;
}

Core::Expected<void, std::string> FollowTheOwner(ModelEffect& effect, std::string_view address)
{
    const glm::vec3* position = OwnerPosition(effect);
    if (position == nullptr)
        return Core::Unexpected(Cut(address, std::format("el efecto {:#x}/{} lee la posición de su dueño (+0x108), "
                                                         "que MuRet no tiene",
                                                         effect.type, effect.subtype)));
    effect.transform.position = *position;
    return {};
}

glm::vec3 Rotated(const glm::vec3& vector, const glm::vec3& angles)
{
    return Render::Models::OriginalVectorRotate(vector, Render::Models::OriginalAngleMatrix(angles));
}

void AddTo(glm::vec3& target, const glm::vec3& offset)
{
    for (int axis = 0; axis < 3; ++axis)
        target[axis] = F32(D(offset[axis]) + D(target[axis]));
}

// El registro propio abierto (0x4e79a0) y cerrado (0x40f370): el cierre que deja la profundidad en 0 gasta sus
// 3.848 rand(). `inside` corre con el registro abierto.
template <typename Inside>
Core::Expected<void, std::string> WithTheOwnRecord(const BlastEffectServices& services, CrtRandom& random,
                                                   std::string_view address, Inside inside)
{
    if (services.ownRecordDepth == nullptr || services.ownRecordCount == nullptr)
        return Core::Unexpected(Cut(address, "el alta abre el registro propio (0x7681eb4), que no llegó"));
    const Render::Models::LogicalAccess access(*services.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    inside(*services.ownRecordCount);
    return {};
}

Core::Expected<void, std::string> AskTheJoint(const BlastEffectServices& services, const FishEffectRequest& request,
                                              bool effectOwner, std::string_view address)
{
    if (!services.joint)
        return Core::Unexpected(
            Cut(address, std::format("el joint {:#x}/{} (0x530860) no tiene la tabla", request.type, request.subtype)));
    if (auto made = services.joint(request, effectOwner); !made.HasValue())
        return Core::Unexpected(std::format("{}: {}", address, made.Error()));
    return {};
}

Core::Expected<void, std::string> HitTheTargets(const BlastEffectServices& services, const ModelEffect& effect,
                                                float radius, std::string_view address)
{
    if (!services.skillHits)
        return Core::Unexpected(Cut(address, "0x517a20 (los blancos de la skill) no llegó"));
    return services.skillHits(effect, radius);
}

// 0x519fc9..0x51a101 con el subtipo 1: el 0x105 que crea el 0x106 al volver a 0x518d0a.
Core::Expected<void, std::string> BeginThePiercingChild(ModelEffect& child, const BlastEffectServices& services)
{
    constexpr std::int32_t ChildLife = 100;
    constexpr float ChildScale = 24.0f; // 0x41c00000
    constexpr std::int32_t JointKey = 0x1e, NoTarget = -1;
    constexpr std::int32_t TrailJoint = 0x610, TrailSound = 0x7a;
    child.life = ChildLife;
    child.objectState.blendMesh = 0;
    child.objectState.light = 1.0f;
    child.turnDirection = 0.0f;
    child.objectState.hiddenMesh = 0;
    child.transform.scale = ChildScale;
    for (std::int32_t subtype = 0; subtype < 4; ++subtype)
    {
        const FishEffectRequest request{TrailJoint,
                                        child.transform.position,
                                        child.transform.position,
                                        child.transform.angles,
                                        subtype,
                                        &child,
                                        child.transform.scale,
                                        JointKey,
                                        child.subtype,
                                        0,
                                        NoTarget};
        if (auto made = AskTheJoint(services, request, true, "0x51a08a"); !made.HasValue())
            return made;
    }
    if (!services.sound)
        return Core::Unexpected(Cut("0x51a0f9", "el sonido 0x7a (0x41ed10) no llegó"));
    services.sound(TrailSound);
    child.transform.scale = 1.0f; // 0x51a101
    return {};
}

// 0x518e66..0x518f05 y la vuelta a 0x518d0a con el 0x105 del mismo subtipo.
Core::Expected<void, std::string> BeginTheDeathStab(ModelEffects& effects, ModelEffect& effect,
                                                    const BlastEffectServices& services)
{
    constexpr std::int32_t StabLife = 30;
    constexpr glm::vec3 Offset{-10.0f, -60.0f, 135.0f}; // 0xc1200000, 0xc2700000, 0x43070000
    constexpr float StabScale = 0.8f;                   // 0x3f4ccccd
    constexpr float Thrust = -70.0f;                    // 0xc28c0000
    effect.speed = 1.0f;
    effect.life = StabLife;
    AddTo(effect.transform.position, Rotated(Offset, effect.transform.angles));
    effect.transform.scale = StabScale;
    effect.velocity.y = Thrust;
    if (effect.subtype != PiercingChildSubtype)
        return Core::Unexpected(
            Cut("0x518f05", std::format("el 0x105/{} del 0x106 no es de este lote", effect.subtype)));
    // 0x518eb3..0x518eee: la posición y los ángulos de este efecto, su luz (+0xf4, en [EBP + 0x30]), su subtipo, él
    // de dueño, +0x8a en −1, los bytes en 0 y la escala 0.
    const ModelEffectArguments child{.type = PiercingTrailEffect,
                                     .position = effect.transform.position,
                                     .angles = effect.transform.angles,
                                     .light = effect.light,
                                     .subtype = effect.subtype,
                                     .owner = &effect,
                                     .key = 0xffff,
                                     .byte89 = 0,
                                     .byte88 = 0,
                                     .byte1bd = 0,
                                     .scale = 0.0f};
    const auto slot = effects.Begin(child);
    if (!slot)
        return {};
    ModelEffect& trail = effects.Effects()[*slot];
    trail.owner.effect = &effect;
    return BeginThePiercingChild(trail, services);
}

// 0x5192ed..0x5193d6 y 0x5193db..: el 0xf5 cae desde arriba con el joint 0x618/5 y copia la cuenta +0xf04.
Core::Expected<void, std::string> BeginTheBlast(ModelEffect& effect, CrtRandom& random,
                                                const BlastEffectServices& services)
{
    constexpr std::int32_t BlastLife = 30;
    constexpr double Tenth = static_cast<double>(0.1f); // 0x6b87b8
    constexpr double Fall = -50.0;                      // 0x6ba1f8
    constexpr float BlastPitch = 20.0f;                 // 0x41a00000
    constexpr float JointScale = 100.0f;                // 0x42c80000
    effect.life = BlastLife;
    effect.objectState.blendMesh = 0;
    effect.transform.scale = F32(static_cast<double>((random.Next() & 7) + 10) * Tenth);
    glm::vec3& position = effect.transform.position;
    position.x = F32(static_cast<double>(random.Next() % 100 + 200) + D(position.x));
    position.y = F32(static_cast<double>(random.Next() % 100 - 50) + D(position.y));
    position.z = F32(static_cast<double>(random.Next() % 500 + 300) + D(position.z));
    effect.velocity.x = 0.0f;
    effect.velocity.y = 0.0f;
    effect.velocity.z = F32(Fall - static_cast<double>(random.Next() % 50));
    effect.transform.angles = {0.0f, BlastPitch, 0.0f};
    effect.point40 = position;
    const FishEffectRequest request{0x618, position, position, effect.transform.angles, 5, &effect, JointScale, -1,
                                    0,     0,        -1};
    if (auto made = AskTheJoint(services, request, true, "0x5193d6"); !made.HasValue())
        return made;
    return WithTheOwnRecord(services, random, "0x5193db", [&](const std::uint8_t& count) { effect.word8c = count; });
}

// 0x5198cd..0x519a46 y 0x519a49..0x519e9e: el 0xf6 según su subtipo y, con el héroe de dueño y el subtipo 0 o 1,
// la cuenta +0xf04 en +0x8c y los blancos a 400.
Core::Expected<void, std::string> BeginTheInferno(ModelEffect& effect, std::uint16_t key, std::uint8_t byte89,
                                                  CrtRandom& random, const BlastEffectServices& services)
{
    constexpr float InfernoScale = 0.9f; // 0x3f666666
    constexpr float HalfSpeed = 0.5f;    // 0x3f000000
    constexpr glm::vec3 Gray{0.8f, 0.8f, 0.8f};
    constexpr double Hundredth = static_cast<double>(0.01f); // 0x6b86c0
    constexpr float Tenth = 0.1f;
    effect.objectState.blendMesh = HiddenBlend;
    effect.transform.scale = InfernoScale;
    effect.speed = HalfSpeed;
    const auto keyed = [&](std::int32_t life)
    {
        // 0x519943 y 0x5199ff: +0x58 con el byte +0x89 del argumento, +0x89 y +0x8a de vuelta, +0xec y la escala del
        // WORD +0x8a con signo.
        const double signedKey = static_cast<double>(static_cast<std::int16_t>(key));
        effect.life = life;
        effect.objectState.hiddenMesh = static_cast<std::int16_t>(byte89);
        effect.byte89 = 0;
        effect.disturbanceState = 0xffff;
        effect.flagEc = F32(signedKey);
        effect.transform.scale = F32(signedKey * Hundredth);
    };
    switch (effect.subtype)
    {
    case 0:
    case 5:
        effect.light = Gray;
        effect.life = 15;
        break;
    case 1:
        effect.light = {1.0f, 0.5f, 0.2f};
        effect.life = 35;
        break;
    case 2:
        keyed(12);
        effect.objectState.light = Tenth;
        break;
    case 3:
        effect.light = Gray;
        effect.life = 4;
        effect.objectState.hiddenMesh = 1;
        effect.transform.scale = 0.02f; // 0x3ca3d70a
        effect.speed = 1.0f;
        effect.transform.angles = {90.0f, 0.0f, 0.0f};
        break;
    case 4:
        effect.light = Gray;
        effect.life = 35;
        effect.objectState.hiddenMesh = 1;
        effect.transform.scale = Tenth;
        effect.turnDirection = 0.0f;
        break;
    case 6:
        keyed(5);
        effect.speed = Tenth;
        effect.objectState.light = Tenth;
        break;
    default:
        break;
    }
    // 0x519a49..0x519a60: el dueño contra el héroe publicado (0x743a170) y después el subtipo; con el subtipo desde 2
    // las dos salidas van a 0x51db73, así que el héroe solo decide por debajo de 2.
    if (effect.subtype >= 2)
        return {};
    if (services.hero == nullptr)
        return Core::Unexpected(Cut("0x519a4f", "el 0xf6 compara su dueño con el héroe publicado (0x743a170)"));
    if (effect.ownerContext != services.hero)
        return {};
    if (auto copied =
            WithTheOwnRecord(services, random, "0x519a66", [&](const std::uint8_t& count) { effect.word8c = count; });
        !copied.HasValue())
        return copied;
    constexpr float HitRadius = 400.0f; // 0x43c80000
    return HitTheTargets(services, effect, HitRadius, "0x519e9e");
}

// 0x51bccb..0x51be7a: el 0x108.
Core::Expected<void, std::string> BeginTheBlizzard(ModelEffect& effect, CrtRandom& random,
                                                   const BlastEffectServices& services)
{
    if (effect.subtype == 1)
    {
        effect.objectState.blendMesh = HiddenBlend;
        effect.life = 20;
        effect.speed = 0.0f;
        return {};
    }
    if (effect.subtype != 0 && effect.subtype != 2)
        return {};
    constexpr double Spread = 150.0, Height = 600.0, Ahead = 100.0; // 0x6b87ac, 0x6b8c08, 0x6b86b4
    constexpr float Fall = -20.0f;                                  // 0xc1a00000
    effect.objectState.blendMesh = HiddenBlend;
    effect.life = random.Next() % 15 + 15;
    effect.turnDirection = Fall;
    effect.speed = static_cast<float>(random.Next() % 360);
    effect.light = glm::vec3(0.0f);
    effect.transform.scale = 0.5f;
    effect.turnDirection = F32(D(effect.turnDirection) - static_cast<double>(random.Next() % 30 + 10));
    glm::vec3& position = effect.transform.position;
    position.x = F32(static_cast<double>(random.Next() % 300) + D(position.x) - Spread);
    position.y = F32(static_cast<double>(random.Next() % 300) + D(position.y) - Spread);
    position.z = F32(D(position.z) + Height);
    position.x = F32(D(position.x) + Ahead);
    effect.start = position;
    if (effect.subtype != 2)
        return {};
    constexpr double JointBase = 60.0; // 0x6b86cc
    const auto ice = [&]() -> Core::Expected<void, std::string>
    {
        const float scale = F32(static_cast<double>(random.Next() % 10) + JointBase);
        const FishEffectRequest request{0x615, position, position, effect.transform.angles, 4, &effect, scale, -1,
                                        0,     0,        -1};
        return AskTheJoint(services, request, true, "0x51be22");
    };
    if (auto made = ice(); !made.HasValue())
        return made;
    if (random.Next() % 2 != 0)
        return {};
    return ice();
}

// 0x51b99f..0x51bafa: las piedras 0x10c y 0x10d de las puertas de Blood Castle.
void BeginTheGateDebris(ModelEffect& effect, CrtRandom& random)
{
    constexpr double Tenth = static_cast<double>(0.1f);  // 0x6b87b8
    constexpr double Lift = 50.0;                        // 0x6b86b0
    constexpr double Shrink = static_cast<double>(0.6f); // 0x6b8a20
    const glm::vec3 push{0.0f, F32(static_cast<double>((random.Next() & 127) + 64) * Tenth), 0.0f};
    glm::vec3& position = effect.transform.position;
    position.z = F32(D(position.z) + Lift);
    effect.life = (random.Next() & 15) + 32;
    effect.transform.scale = F32(static_cast<double>((random.Next() & 3) + 8) * Tenth);
    glm::vec3& angles = effect.transform.angles;
    angles.z = static_cast<float>(random.Next() % 360);
    effect.velocity = Rotated(push, angles);
    effect.turnDirection = static_cast<float>(random.Next() % 5 + 2);
    if (effect.type == FirstGateDebrisEffect && effect.subtype == 0)
    {
        effect.subtype = 1;
        effect.turnDirection = F32(static_cast<double>(random.Next() % 5) + D(effect.turnDirection));
    }
    else if (effect.subtype == 2)
    {
        effect.transform.scale = F32(D(effect.transform.scale) * Shrink);
    }
    angles.x = static_cast<float>(random.Next() % 360);
    angles.y = static_cast<float>(random.Next() % 360);
    angles.z = static_cast<float>(random.Next() % 360);
    effect.previousPosition = position;
}

Core::Expected<void, std::string> BeginTheCase(ModelEffects& effects, ModelEffect& effect,
                                               const ModelEffectArguments& arguments, CrtRandom& random,
                                               const BlastEffectServices& services)
{
    constexpr double Tenth = static_cast<double>(0.1f); // 0x6b87b8
    const std::int16_t type = effect.type;
    if (type == ProtectionEffect)
    {
        // 0x519fae.
        effect.life = 10000;
        effect.speed = 0.3f;
        effect.objectState.blendMesh = 0;
        return {};
    }
    if (type == FirstBigStoneEffect || type == SecondBigStoneEffect)
    {
        // 0x51b3b0..0x51b41e: la partida donde nació y la posición, la del dueño.
        effect.life = 60;
        effect.transform.scale = F32(static_cast<double>((random.Next() & 3) + 8) * Tenth);
        effect.start = effect.transform.position;
        return FollowTheOwner(effect, "0x51b3fd");
    }
    if (type >= FirstIceRockEffect && type <= LastIceRockEffect)
    {
        // 0x519f56..0x519fa2.
        constexpr double Forward = -15.0, Down = -30.0; // 0x6b8934, 0x6ba1f4
        effect.life = 100;
        effect.transform.scale = F32(static_cast<double>(random.Next() % 10 + 4) * Tenth);
        effect.velocity.x = 0.0f;
        effect.velocity.y = F32(Forward / D(effect.transform.scale));
        effect.velocity.z = F32(Down / D(effect.transform.scale));
        effect.subtype = 1;
        return {};
    }
    if (type == MagicCircleEffect)
    {
        // 0x519eec..0x519f46.
        constexpr float CircleScale = 0.7f; // 0x3f333333
        effect.life = 30;
        effect.transform.scale = CircleScale;
        effect.objectState.blendMesh = HiddenBlend;
        if (effect.subtype == 2)
        {
            effect.life = 15;
            effect.speed = 0.3f;
            effect.transform.scale = CircleScale;
        }
        else if (effect.subtype == 1)
        {
            effect.life = 20;
            effect.speed = 0.1f;
            effect.objectState.hiddenMesh = 0;
        }
        else
        {
            effect.speed = 0.1f;
        }
        return {};
    }
    if (type == FallingRockEffect)
    {
        // 0x519eb3..0x519edc.
        effect.life = 50;
        effect.transform.scale = 0.8f;
        effect.speed = 1.0f;
        effect.transform.angles.x = 0.0f;
        effect.objectState.blendMesh = 0;
        effect.velocity.y = -50.0f;
        effect.turnDirection = -10.0f;
        return {};
    }
    if (type == SwordTrailEffect)
    {
        // 0x51a625..0x51a64e: el registro abierto, su cuenta +0xf04 un paso más (envuelve) y cerrado.
        effect.life = 5;
        return WithTheOwnRecord(services, random, "0x51a638", [](std::uint8_t& count) { ++count; });
    }
    if (type == SpinningItemEffect)
    {
        // 0x51a65a y 0x51a1b6: la cuenta +0xf04 en +0x8c.
        effect.life = 25;
        return WithTheOwnRecord(services, random, "0x51a1b6",
                                [&](const std::uint8_t& count) { effect.word8c = count; });
    }
    if (type == BlastEffect)
        return BeginTheBlast(effect, random, services);
    if (type == InfernoEffect)
        return BeginTheInferno(effect, arguments.key, arguments.byte89, random, services);
    if (type == DeathStabEffect)
        return BeginTheDeathStab(effects, effect, services);
    if (type == PiercingTrailEffect)
        return BeginThePiercingChild(effect, services);
    if (type == WaveForceEffect)
    {
        // 0x519884..0x5198c1.
        constexpr double Hundredth = static_cast<double>(0.01f); // 0x6b86c0
        effect.objectState.blendMesh = HiddenBlend;
        effect.speed = 0.5f;
        effect.life = 12;
        effect.byte89 = 0;
        effect.disturbanceState = 0xffff;
        effect.transform.scale = F32(static_cast<double>(static_cast<std::int16_t>(arguments.key)) * Hundredth);
        effect.objectState.light = 0.1f;
        return {};
    }
    if (type == BlizzardEffect)
        return BeginTheBlizzard(effect, random, services);
    if (type == ShieldEffect)
    {
        // 0x51921a.
        effect.life = 20;
        effect.objectState.blendMesh = 0;
        effect.objectState.light = 1.0f;
        return {};
    }
    if (type == FirstGateDebrisEffect || type == SecondGateDebrisEffect)
    {
        BeginTheGateDebris(effect, random);
        return {};
    }
    if (type == RidingSpearEffect)
    {
        // 0x519232..0x51926a: la velocidad del yaw con FSIN y FCOS de la x87.
        constexpr double Radians = static_cast<double>(std::bit_cast<float>(0x3c8efa35u)); // 0x6b8cf8
        effect.life = 20;
        effect.transform.scale = 1.5f;
        const double argument = D(effect.transform.angles.z) * Radians;
        const auto sine = Render::Models::Detail::SineProductPC53(argument, 5.0f);
        const auto cosine = Render::Models::Detail::CosineDoubleProductPC53(argument, -5.0);
        if (!sine || !cosine)
            return Core::Unexpected(Cut("0x51924b", "el yaw del 0x10f no cabe en el perfil PC53"));
        effect.velocity.x = F32(*sine);
        effect.velocity.y = F32(*cosine);
        return {};
    }
    if (type == ComboEffect)
    {
        // 0x51bf73..0x51c00d: con el subtipo 0, sesenta joints 0x5ac/0 sin dueño (el largo, antes de cada uno).
        effect.life = 20;
        effect.turnDirection = 0.1f;
        effect.objectState.blendMesh = HiddenBlend;
        effect.objectState.light = 1.0f;
        effect.transform.angles = glm::vec3(0.0f);
        effect.start = effect.transform.position;
        effect.transform.position.z = F32(D(effect.transform.position.z) + 50.0);
        if (effect.subtype != 0)
            return {};
        constexpr int Rays = 60;
        for (int ray = 0; ray < Rays; ++ray)
        {
            const auto length = static_cast<float>(random.Next() % 40 + 70);
            const FishEffectRequest request{0x5ac,
                                            effect.transform.position,
                                            effect.transform.position,
                                            effect.transform.angles,
                                            0,
                                            nullptr,
                                            length,
                                            -1,
                                            0,
                                            0,
                                            -1};
            if (auto made = AskTheJoint(services, request, false, "0x51c004"); !made.HasValue())
                return made;
        }
        return {};
    }
    if (type == AirForceEffect)
    {
        // 0x51c016..0x51c051.
        if (effect.subtype == 0)
        {
            effect.life = 15;
            effect.objectState.light = 1.0f;
            effect.transform.scale = 0.6f;
        }
        else if (effect.subtype == 1)
        {
            effect.life = 20;
            effect.objectState.light = 1.0f;
            effect.transform.scale = 1.2f;
        }
        return {};
    }
    if (type == FlameJetEffect)
    {
        // 0x51eca0..0x51ecda: el /1 y el /2, vida 10 y los ángulos en 0.
        effect.life = 10;
        effect.transform.angles = glm::vec3(0.0f);
        return {};
    }
    return Core::Unexpected(Cut("0x518e3c", std::format("el alta del efecto {:#x} no es de este lote", type)));
}

// 0x52cb98..0x52cd9d: la cola. `straight` es 0x51fc80(efecto, 0) del 0x10f; `lifeOnly`, el salto a 0x52cd94.
enum class Tail : std::uint8_t
{
    Rotated,
    Straight,
    LifeOnly,
    None
};

void FinishTheStep(ModelEffect& effect, const Render::Models::Skeleton* skeleton,
                   const ModelEffectStepServices& services, Tail tail, const glm::vec3& before)
{
    if (tail == Tail::None)
    {
        effect.previousPosition = before;
        return;
    }
    if (tail != Tail::LifeOnly)
    {
        constexpr std::int16_t FirstAnimatedModel = 0xad, LastAnimatedModel = 0x151;
        if (effect.type >= FirstAnimatedModel && effect.type < LastAnimatedModel)
        {
            // 0x52cc21: la acción en la tabla del modelo; 0x4ec200 con +0xd8 y, al terminar, las keys y 0xff.
            if (services.publishAction)
                services.publishAction(static_cast<std::uint16_t>(effect.type), effect.animation.action);
            if (effect.animation.action != NoAction && skeleton != nullptr &&
                effect.animation.action < skeleton->actions.size())
            {
                const Render::Models::ActionInfo& action = skeleton->actions[effect.animation.action];
                if (Render::Models::AdvanceAnimation(effect.animation, action, effect.speed,
                                                     Render::Models::AnimationScene::Other))
                {
                    effect.animation.frame = static_cast<float>(action.keyCount);
                    effect.animation.action = NoAction;
                }
            }
        }
        // 0x51fc80: con 1, la velocidad girada por los ángulos (0x51fc91..0x51fcdf); con 0, sin girar (0x51fce7).
        AddTo(effect.transform.position,
              tail == Tail::Rotated ? Rotated(effect.velocity, effect.transform.angles) : effect.velocity);
    }
    --effect.life;
    if (effect.life <= 0)
        effect.alive = false;
    effect.previousPosition = before;
}

Core::Expected<void, std::string> Light(const ModelEffectStepServices& services, const glm::vec3& position,
                                        const glm::vec3& color, std::int32_t radius, std::string_view address)
{
    return AddDynamicLight(services.lighting, position, color, radius, address);
}

glm::vec3 Shade(float luminosity)
{
    constexpr double Shadow = -0.5; // 0x6ba298
    return glm::vec3(F32(D(luminosity) * Shadow));
}

Core::Expected<void, std::string> Emit(Particles& particles, std::int32_t type, const glm::vec3& position,
                                       const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                                       float scale, float worldClock, CrtRandom& random, std::string_view address)
{
    const auto created = particles.CreateChecked(type, position, angles, light, subtype, scale, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(Cut(address, std::format("la partícula {:#x}/{} (0x53fe70) no está portada ({})", type,
                                                         subtype, static_cast<int>(created.Error()))));
    return {};
}

Core::Expected<void, std::string> Sprite(const ModelEffectStepServices& services, std::uint16_t texture,
                                         const glm::vec3& position, float scale, const glm::vec3& light, float rotation,
                                         std::string_view address)
{
    if (services.sprites == nullptr)
        return Core::Unexpected(Cut(address, std::format("el sprite {:#x} (0x5491e0) no tiene la tabla", texture)));
    static_cast<void>(services.sprites->Create(texture, position, scale, light, rotation, 0));
    return {};
}

double Ground(const ModelEffectStepServices& services, const glm::vec3& position)
{
    return services.terrainHeight(position.x, position.y);
}

Core::Expected<void, std::string> NeedTheGround(const ModelEffectStepServices& services, std::string_view address)
{
    if (!services.terrainHeight)
        return Core::Unexpected(Cut(address, "el paso pide la altura del terreno (0x5f93c0)"));
    return {};
}

Core::Expected<void, std::string> NeedTheEffects(const ModelEffectStepServices& services, std::string_view address)
{
    if (services.effects == nullptr)
        return Core::Unexpected(Cut(address, "el paso pide la tabla de efectos (0x518cf0)"));
    return {};
}

// 0x5210e5..0x52124e: el 0xf5 cae; al tocar el terreno suelta seis piedritas, el estallido y los blancos, y muere.
Core::Expected<void, std::string> StepTheBlast(ModelEffect& effect, float luminosity, Particles& particles,
                                               float worldClock, CrtRandom& random,
                                               const ModelEffectStepServices& services)
{
    if (auto ground = NeedTheGround(services, "0x5210f0"); !ground.HasValue())
        return ground;
    glm::vec3& position = effect.transform.position;
    const double height = Ground(services, position);
    if (height > D(position.z))
    {
        position.z = F32(height);
        effect.velocity = glm::vec3(0.0f);
        const glm::vec3 point{position.x, position.y, F32(D(position.z) + 80.0)}; // 0x6b9ffc
        constexpr int Chips = 6;
        for (int chip = 0; chip < Chips; ++chip)
        {
            const auto type = static_cast<std::int16_t>(FirstStoneChipEffect + random.Next() % 2);
            if (auto table = NeedTheEffects(services, "0x521175"); !table.HasValue())
                return table;
            static_cast<void>(
                services.effects->CreateStoneChip(type, position, effect.transform.angles, effect.light, random));
        }
        const glm::vec3 white(1.0f);
        if (auto made =
                Emit(particles, 0x600, point, effect.transform.angles, white, 0, 1.0f, worldClock, random, "0x5211a0");
            !made.HasValue())
            return made;
        if (auto made = Emit(particles, ExplosionParticle, point, effect.transform.angles, white, 0, 1.0f, worldClock,
                             random, "0x5211bf");
            !made.HasValue())
            return made;
        if (effect.ownerContext != nullptr && effect.ownerContext == services.hero)
        {
            if (!services.skillHits)
                return Core::Unexpected(Cut("0x5211f6", "0x517a20 (los blancos del 0xf5) no llegó"));
            constexpr float HitRadius = 150.0f; // 0x43160000
            if (auto hit = services.skillHits(effect, HitRadius); !hit.HasValue())
                return hit;
        }
        effect.alive = false;
    }
    effect.point40 = position;
    constexpr double Red = static_cast<double>(0.2f), Green = static_cast<double>(0.4f); // 0x6b8854, 0x6b87b4
    const glm::vec3 color{F32(D(luminosity) * Red), F32(D(luminosity) * Green), luminosity};
    return Light(services, position, color, 2, "0x521246");
}

// 0x521324..0x5214d1: el 0xf6 según su subtipo.
Core::Expected<void, std::string> StepTheInferno(ModelEffect& effect, float luminosity,
                                                 const ModelEffectStepServices& services)
{
    const std::int32_t subtype = effect.subtype;
    glm::vec3& position = effect.transform.position;
    constexpr double Twentieth = static_cast<double>(0.05f); // 0x6b8ad0
    const double lum = D(luminosity);
    if (subtype == 2)
    {
        effect.transform.scale = F32(D(effect.transform.scale) + static_cast<double>(0.04f)); // 0x6b8aec
    }
    else if (subtype == 3)
    {
        if (auto followed = FollowTheOwner(effect, "0x521342"); !followed.HasValue())
            return followed;
        const ModelEffect* owner = effect.owner.effect;
        const glm::vec3* grand = owner != nullptr ? OwnerPosition(*owner) : nullptr;
        if (grand == nullptr)
            return Core::Unexpected(Cut("0x52136c", "el 0xf6/3 lee la posición del dueño de su dueño, que MuRet no "
                                                    "tiene"));
        position.x = F32(D(grand->x) + 150.0); // 0x6b87ac
    }
    else if (subtype == 4)
    {
        if (auto followed = FollowTheOwner(effect, "0x521388"); !followed.HasValue())
            return followed;
        // 0x5213ac..0x5213cd: FST deja la suma sin redondear en la pila para el producto y la z.
        const double scale = D(effect.transform.scale) + static_cast<double>(0.003f); // 0x6b89cc
        effect.transform.scale = F32(scale);
        const double rise = scale * 24.0 + D(effect.turnDirection); // 0x6ba294
        effect.turnDirection = F32(rise);
        position.z = F32(rise + D(position.z));
    }
    else if (subtype == 5 || subtype == 6)
    {
        glm::vec3 color{};
        std::int32_t radius = 5;
        if (subtype == 5)
        {
            position.z = F32(D(position.z) + 2.0);
            effect.transform.angles.z = F32(D(effect.transform.angles.z) + 20.0);
            effect.objectState.light = F32(static_cast<double>(effect.life) * Twentieth);
            color = {F32(lum * static_cast<double>(0.1f)), F32(lum * static_cast<double>(0.3f)),
                     F32(lum * static_cast<double>(0.8f))};
        }
        else
        {
            effect.transform.scale = F32(D(effect.transform.scale) + static_cast<double>(0.01f));          // 0x6b88d4
            effect.objectState.light = F32(static_cast<double>(effect.life) * static_cast<double>(0.02f)); // 0x6ba290
            color = {F32(lum * static_cast<double>(0.8f)), F32(lum * static_cast<double>(0.3f)),
                     F32(lum * static_cast<double>(0.1f))};
            radius = 2;
        }
        if (auto lit = Light(services, position, color, radius, "0x52147d"); !lit.HasValue())
            return lit;
    }
    // 0x521485.
    if (subtype >= 4)
        return {};
    effect.objectState.light = F32(static_cast<double>(effect.life) * Twentieth);
    if (subtype == 2)
        return {};
    return Light(services, position, Shade(luminosity), 5, "0x5214c9");
}

// 0x5214d6..0x5215a0: el 0xec.
Core::Expected<void, std::string> StepTheMagicCircle(ModelEffect& effect, float luminosity,
                                                     const ModelEffectStepServices& services)
{
    constexpr double Growth = static_cast<double>(0.01f); // 0x6b88d4
    if (effect.subtype == 2)
    {
        effect.transform.scale = F32(D(effect.transform.scale) + static_cast<double>(0.015f)); // 0x6b8a24
        if (auto followed = FollowTheOwner(effect, "0x5214ea"); !followed.HasValue())
            return followed;
        effect.light = {0.1f, 0.0f, 0.0f};
        return {};
    }
    if (effect.subtype == 1)
    {
        effect.transform.scale = F32(D(effect.transform.scale) + Growth);
        return Light(services, effect.transform.position, {luminosity, 0.0f, 0.0f}, 3, "0x521563");
    }
    // 0x521570: la posición del dueño y después el crecimiento.
    if (auto followed = FollowTheOwner(effect, "0x521570"); !followed.HasValue())
        return followed;
    effect.transform.scale = F32(D(effect.transform.scale) + Growth);
    return {};
}

// 0x5215a5..0x5216d6: las rocas 0xe7..0xe9 caen fuera de las celdas con el bit 0x08; al tocar el terreno tiemblan la
// cámara, sueltan cinco pedazos 0xea/0xeb y mueren. Cada paso, el polvo 5f2/3, sin la cola.
Core::Expected<void, std::string> StepTheIceRock(ModelEffect& effect, Particles& particles, float worldClock,
                                                 CrtRandom& random, const ModelEffectStepServices& services)
{
    if (services.terrainAttributes == nullptr)
        return Core::Unexpected(Cut("0x5215e4", "la roca lee el atributo de su celda (0x7d0d348)"));
    glm::vec3& position = effect.transform.position;
    const std::int32_t cellY = FtolLow(D(position.y)) / 100;
    const std::int32_t cellX = FtolLow(D(position.x)) / 100;
    const auto index = Data::Terrain::CellIndex(static_cast<std::uint8_t>(cellX), static_cast<std::uint8_t>(cellY));
    constexpr std::uint16_t OpenBit = 0x08;
    if ((services.terrainAttributes->cells[index] & OpenBit) != OpenBit)
    {
        if (auto ground = NeedTheGround(services, "0x521601"); !ground.HasValue())
            return ground;
        const double height = Ground(services, position);
        if (height > D(position.z))
        {
            position.z = F32(height);
            effect.alive = false;
            // 0x521619: el rand() del temblor sale antes de escribirlo.
            const int shake = random.Next() % 8 - 8;
            if (services.cameraShake == nullptr)
                return Core::Unexpected(Cut("0x52163e", "la roca escribe el temblor de la cámara (0x7d325c8)"));
            *services.cameraShake = F32(static_cast<double>(shake) * static_cast<double>(0.1f));
            constexpr int Shards = 5;
            for (int shard = 0; shard < Shards; ++shard)
            {
                // 0x52165e..0x521675: 0x518cf0(0xea + rand() % 2, posición, ángulos, luz, 0, nadie, −1, 0, 0, 0, 0).
                // En 0x51b659 los 0xea y 0xeb no son el 0xca (0x51b6d9): van por el camino de las piedritas
                // (0x51b72b..0x51b805), sin +0x64, +0x68 ni los 50 más en z, con cinco rand().
                const auto type = static_cast<std::int16_t>(0xea + random.Next() % 2);
                if (auto table = NeedTheEffects(services, "0x521675"); !table.HasValue())
                    return table;
                static_cast<void>(
                    services.effects->CreateStoneChip(type, position, effect.transform.angles, effect.light, random));
            }
        }
    }
    AddTo(position, effect.velocity);
    effect.transform.angles.x = F32(10.0 / D(effect.transform.scale) + D(effect.transform.angles.x)); // 0x6b87a8
    return Emit(particles, BreathParticle, position, effect.transform.angles, effect.light, 3, 1.0f, worldClock, random,
                "0x5216d6");
}

// 0x52186e..0x521c57: el ítem del dueño, girando detrás de él, con el polvo 5f2/3, la luz, los joints 0x617/0 y la
// chispa 5c5/0 (en Kalima, la 6f4/3), el sprite 0x5ac y, en Kalima, la onda.
Core::Expected<void, std::string> StepTheSpinningItem(ModelEffect& effect, float luminosity, Particles& particles,
                                                      float worldClock, CrtRandom& random,
                                                      const ModelEffectStepServices& services)
{
    switch (effect.subtype)
    {
    case 1:
        effect.alpha = 0.6f;
        break;
    case 2:
        effect.alpha = 0.5f;
        break;
    case 3:
        effect.alpha = 0.4f;
        break;
    case 4:
        effect.alpha = 0.3f;
        break;
    default:
        break;
    }
    const glm::vec3* owner = OwnerPosition(effect);
    if (effect.itemType == nullptr || owner == nullptr)
        return Core::Unexpected(Cut("0x5218ac", "el 0xf4 lee +0x8c y la posición de su dueño, que MuRet no tiene"));
    constexpr std::int16_t FirstLongItem = 0x600, LastLongItem = 0x800;
    const bool longItem = *effect.itemType >= FirstLongItem && *effect.itemType < LastLongItem;
    const glm::vec3 behind{0.0f, longItem ? -180.0f : -150.0f, 0.0f}; // 0xc3340000, 0xc3160000
    const glm::vec3 offset = Rotated(behind, effect.transform.angles);
    glm::vec3& position = effect.transform.position;
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(offset[axis]) + D((*owner)[axis]));
    effect.transform.angles.z = F32(D(effect.transform.angles.z) - 18.0); // 0x6b886c
    if (auto made = Emit(particles, BreathParticle, position, effect.transform.angles, effect.light, 3, 1.0f,
                         worldClock, random, "0x521960");
        !made.HasValue())
        return made;
    const float glow = F32(D(luminosity) * static_cast<double>(0.3f)); // 0x6b89dc
    if (auto lit = Light(services, position, glm::vec3(glow), 3, "0x521989"); !lit.HasValue())
        return lit;
    if (services.map == nullptr)
        return Core::Unexpected(Cut("0x52198e", "el 0xf4 lee el mapa (0x6c9dd0)"));
    // 0x521996..0x521aab: en Kalima (0x18..0x1d o 0x24), una sola chispa con sus cuatro rand() (0x5219cc, 0x5219ec,
    // 0x521a30 y 0x521a4c), 120 más arriba (0x6b86c8), y 0x53fe70(0x6f4, punto, ángulos, (0,5; 0,5; 0,5), 3, 1, 0)
    // (0x521aa3), que salta al sprite (0x521bb6); el sprite sube 60 (0x42700000) en vez de 20 (0x41a00000, 0x5219ae).
    const bool kalima = IsKalimaMap(*services.map);
    const double lift = kalima ? 60.0 : 20.0;
    const int sparks = kalima ? 1 : 4;
    for (int spark = 0; spark < sparks; ++spark)
    {
        glm::vec3 angles{static_cast<float>(random.Next() % 60 - 30), 0.0f, 0.0f};
        angles.z = static_cast<float>(random.Next() % 30 + 90);
        angles.x = F32(D(angles.x) + D(effect.transform.angles.x));
        angles.y = F32(D(angles.y) + D(effect.transform.angles.y));
        angles.z = F32(D(angles.z) + D(effect.transform.angles.z));
        glm::vec3 point = position;
        point.x = F32(static_cast<double>(random.Next() % 20 - 10) + D(point.x));
        point.y = F32(static_cast<double>(random.Next() % 20 - 10) + D(point.y));
        if (kalima)
        {
            constexpr std::int32_t FallSubtype = 3;
            constexpr glm::vec3 FallLight{0.5f}; // 0x521a68..0x521a76, 0x3f000000
            point.z = F32(D(point.z) + 120.0);
            if (auto made = Emit(particles, KalimaWaterfallLast, point, angles, FallLight, FallSubtype, 1.0f,
                                 worldClock, random, "0x521aa3");
                !made.HasValue())
                return made;
            break;
        }
        if (!services.joint)
            return Core::Unexpected(Cut("0x521b75", "el joint 0x617/0 (0x530860) no tiene la tabla"));
        const FishEffectRequest request{0x617, point, point, angles, 0, nullptr, 10.0f, -1, 0, 0, -1};
        if (auto made = services.joint(request); !made.HasValue())
            return Core::Unexpected(std::format("0x521b75: {}", made.Error()));
        if (spark == 0)
        {
            if (auto made =
                    Emit(particles, 0x5c5, point, angles, glm::vec3(1.0f), 0, 1.0f, worldClock, random, "0x521b9e");
                !made.HasValue())
                return made;
        }
    }
    const glm::vec3 above{position.x, position.y, F32(D(position.z) + lift)};
    if (auto made = Sprite(services, 0x5ac, above, 2.0f, {1.0f, 0.8f, 0.6f}, 0.0f, "0x521bf6"); !made.HasValue())
        return made;
    return {};
}

// 0x523974..0x523b3a con el 0xf2: el paso de las piedritas, sin partícula (el tipo no es ni 0xc8/0xc9 ni 0xca/0xea/
// 0xeb), con el rand() % 10 del subtipo 0 y del 12.
Core::Expected<void, std::string> StepTheFallingRock(ModelEffect& effect, CrtRandom& random,
                                                     const ModelEffectStepServices& services)
{
    const std::int32_t subtype = effect.subtype;
    const bool resting = subtype == 0 || subtype == 10 || subtype == 12;
    glm::vec3& position = effect.transform.position;
    AddTo(position, resting ? effect.velocity : Rotated(effect.velocity, effect.transform.angles));
    constexpr double Drag = static_cast<double>(0.9f); // 0x6b8a1c
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(D(effect.velocity[axis]) * Drag);
    position.z = F32(D(position.z) + D(effect.turnDirection));
    if (!resting)
    {
        if (subtype != 1)
            effect.transform.angles.z = F32(D(effect.transform.angles.z) + 20.0); // 0x6b86d0
        effect.turnDirection = F32(D(effect.turnDirection) + 0.5);                // 0x6b882c
        return {};
    }
    effect.turnDirection = F32(D(effect.turnDirection) - 3.0); // 0x6b86b8
    if (auto ground = NeedTheGround(services, "0x523a80"); !ground.HasValue())
        return ground;
    const double height = Ground(services, position);
    double tilt = D(effect.transform.scale) * 32.0; // 0x6b883c
    if (height > D(position.z))
    {
        position.z = F32(height);
        effect.turnDirection = F32(D(effect.turnDirection) * -0.5); // 0x6ba298
        effect.life -= 4;
        tilt = D(effect.transform.scale) * 128.0; // 0x6b8844
    }
    effect.transform.angles.x = F32(D(effect.transform.angles.x) - tilt);
    if (subtype == 0 || subtype == 12)
        static_cast<void>(random.Next() % 10); // 0x523adb: sin partícula para el 0xf2.
    return {};
}

// 0x523d3a..0x523dd4: el 0x106 suelta a veces el aliento 5f2/0, dos fuegos 5d9/5 y la luz.
Core::Expected<void, std::string> StepTheDeathStab(ModelEffect& effect, float luminosity, Particles& particles,
                                                   float worldClock, CrtRandom& random,
                                                   const ModelEffectStepServices& services)
{
    const glm::vec3 white(1.0f);
    const glm::vec3& position = effect.transform.position;
    if (random.Next() % 2 == 0)
    {
        if (auto made = Emit(particles, BreathParticle, position, effect.transform.angles, white, 0, 1.0f, worldClock,
                             random, "0x523d67");
            !made.HasValue())
            return made;
    }
    for (const std::string_view address : {"0x523d89", "0x523da2"})
    {
        if (auto made = Emit(particles, FireParticle, position, effect.transform.angles, white, 5, 1.0f, worldClock,
                             random, address);
            !made.HasValue())
            return made;
    }
    const float red = F32(D(luminosity) * static_cast<double>(0.6f));  // 0x6b8a20
    const float rest = F32(D(luminosity) * static_cast<double>(0.8f)); // 0x6b8a38
    return Light(services, position, {red, rest, rest}, 2, "0x523dd4");
}

// 0x523b8b..0x523d35 con el subtipo 1: la estela del 0x106 sigue a su dueño con el joint 0x614/3, el sprite 0x5fd y, en
// Kalima con el héroe detrás del dueño, la onda.
Core::Expected<void, std::string> StepThePiercingChild(ModelEffect& effect, CrtRandom& random, KalimaWorld* kalima,
                                                       const ModelEffectStepServices& services)
{
    const ModelEffect* owner = effect.owner.effect;
    if (owner == nullptr || !owner->alive)
    {
        effect.life = -1;
        return {};
    }
    effect.turnDirection = F32(D(effect.turnDirection) + 90.0); // 0x6b8948
    effect.transform.angles = owner->transform.angles;
    effect.copiedAngles = owner->transform.angles;
    effect.start = effect.transform.position;
    effect.transform.position = owner->transform.position;
    glm::vec3& position = effect.transform.position;
    if (!services.joint)
        return Core::Unexpected(Cut("0x523c78", "el joint 0x614/3 (0x530860) no tiene la tabla"));
    const FishEffectRequest request{0x614, position, position, effect.transform.angles, 3, nullptr, 20.0f, 7, 0, 0, -1};
    if (auto made = services.joint(request); !made.HasValue())
        return Core::Unexpected(std::format("0x523c78: {}", made.Error()));
    const auto rotation = static_cast<float>(random.Next() % 360);
    const float scale = F32(static_cast<double>((random.Next() & 7) + 8) * static_cast<double>(0.2f)); // 0x6b8854
    if (auto made = Sprite(services, 0x5fd, position, scale, glm::vec3(1.0f), rotation, "0x523cc8"); !made.HasValue())
        return made;
    if (services.map == nullptr)
        return Core::Unexpected(Cut("0x523cd0", "la estela 105/1 lee el mapa (0x6c9dd0)"));
    if (IsKalimaMap(*services.map) && owner->ownerContext != nullptr && owner->ownerContext == services.hero)
    {
        if (kalima == nullptr)
            return Core::Unexpected(Cut("0x523d24", "la estela 105/1 pide las ondas de Kalima (0x437860)"));
        constexpr double TileFactor = static_cast<double>(0.01f); // 0x6b86c0
        constexpr std::int32_t Radius = 2, Amplitude = -200;
        const auto tileY = FtolLow(D(position.y) * TileFactor);
        const auto tileX = FtolLow(D(position.x) * TileFactor);
        if (auto disturbed = kalima->Disturb(tileX, tileY, Radius, Amplitude, random); !disturbed.HasValue())
            return disturbed;
    }
    effect.copiedAngles.y = effect.turnDirection; // 0x523d2c
    return {};
}

// 0x5251df..0x525318: las piedras 0x10c y 0x10d de las puertas. Uno de cada diez pasos corta en 0x5252e6, como las
// 0x10a/0x10b: el polvo 5f3/0 va en [EBP − 0x28], un local que ningún camino escribe (Q-C1176, P-C232).
Core::Expected<void, std::string> StepTheGateDebris(ModelEffect& effect, Particles& particles, CrtRandom& random,
                                                    const ModelEffectStepServices& services)
{
    glm::vec3& position = effect.transform.position;
    AddTo(position, effect.velocity);
    constexpr double Brake = static_cast<double>(0.9f), Boost = static_cast<double>(1.1f); // 0x6b8a1c, 0x6b8830
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(D(effect.velocity[axis]) * Brake);
    position.z = F32(D(position.z) + D(effect.turnDirection));
    if (effect.subtype == 1)
    {
        for (int axis = 0; axis < 3; ++axis)
            effect.velocity[axis] = F32(D(effect.velocity[axis]) * Boost);
    }
    effect.turnDirection = F32(D(effect.turnDirection) - 4.0); // 0x6b8a44
    if (auto ground = NeedTheGround(services, "0x52529b"); !ground.HasValue())
        return ground;
    const double height = Ground(services, position);
    double tilt = D(effect.transform.scale) * 16.0; // 0x6b8f00
    if (height > D(position.z))
    {
        position.z = F32(height);
        effect.turnDirection = F32(D(effect.turnDirection) * -0.5); // 0x6ba298
        effect.life -= 5;
        tilt = D(effect.transform.scale) * 128.0; // 0x6b8844
    }
    effect.transform.angles.x = F32(D(effect.transform.angles.x) - tilt);
    if (random.Next() % 10 != 0)
        return {};
    if (std::ranges::none_of(particles.All(), [](const Particle& particle) { return !particle.alive; }))
        return {};
    return Core::Unexpected(std::format("0x5252e6: el polvo 0x5f3/0 de la piedra {:#x} va en [EBP − 0x28] de "
                                        "0x5208a0, un local que ningún camino escribe (Q-C1176; la VM, P-C232)",
                                        effect.type));
}

// 0x52552d..0x525564 y 0x5257e8..0x525825: la x y la y, la partida más FSIN(r % 1000 · 0,01f) · `amplitude`.
Core::Expected<void, std::string> SwayTheBlizzard(ModelEffect& effect, CrtRandom& random, float amplitude,
                                                  std::string_view address)
{
    constexpr double Hundredth = static_cast<double>(0.01f); // 0x6b88d4
    for (int axis = 0; axis < 2; ++axis)
    {
        const double argument = static_cast<double>(random.Next() % 1000) * Hundredth;
        const auto sine = Render::Models::Detail::SineProductPC53(argument, amplitude);
        if (!sine)
            return Core::Unexpected(Cut(address, "el seno del 0x108 no cabe en el perfil PC53"));
        effect.transform.position[axis] = F32(*sine + D(effect.start[axis]));
    }
    effect.transform.position.z = F32(D(effect.transform.position.z) + D(effect.turnDirection));
    effect.turnDirection = F32(D(effect.turnDirection) - static_cast<double>(random.Next() % 5));
    effect.start.x = F32(D(effect.start.x) - 10.0); // 0x6b87a8
    return {};
}

// 0x525602..0x52569a y 0x5258b3..0x52bfc6: la luz más 0,1f en los tres canales y los sprites 0x5fd y 0x5ac.
Core::Expected<void, std::string> BrightenTheBlizzard(ModelEffect& effect, CrtRandom& random,
                                                      const ModelEffectStepServices& services,
                                                      std::string_view firstAddress, std::string_view secondAddress)
{
    effect.light = glm::vec3(F32(D(effect.light.x) + static_cast<double>(0.1f)));
    const auto rotation = static_cast<float>(random.Next() % 360);
    const float scale = F32(static_cast<double>((random.Next() & 3) + 4) * static_cast<double>(0.2f));
    if (auto made = Sprite(services, 0x5fd, effect.transform.position, scale, effect.light, rotation, firstAddress);
        !made.HasValue())
        return made;
    const auto spin = static_cast<float>(random.Next() % 360);
    return Sprite(services, 0x5ac, effect.transform.position, 1.0f, effect.light, spin, secondAddress);
}

// 0x525506..0x5259xx: el 0x108.
Core::Expected<void, std::string> StepTheBlizzard(ModelEffect& effect, Particles& particles, float worldClock,
                                                  CrtRandom& random, const ModelEffectStepServices& services)
{
    const glm::vec3 white(1.0f);
    glm::vec3& position = effect.transform.position;
    glm::vec3& angles = effect.transform.angles;
    if (effect.subtype == 1)
    {
        effect.objectState.light =
            F32(D(effect.objectState.light) * static_cast<double>(std::bit_cast<float>(0x3f68ba2eu))); // 0x6ba26c
        if (effect.life == 18 && effect.disturbanceState == 1)
        {
            if (!services.sound)
                return Core::Unexpected(Cut("0x5257bc", "el sonido 0x91 (0x41ed10) no llegó"));
            services.sound(0x91);
        }
        return {};
    }
    if (effect.subtype == 2)
    {
        if (auto swayed = SwayTheBlizzard(effect, random, 50.0f, "0x5257ee"); !swayed.HasValue())
            return swayed;
        if (auto ground = NeedTheGround(services, "0x52586c"); !ground.HasValue())
            return ground;
        // 0x525871: FCOMP y TEST AH, 1: sigue con la altura por debajo de la z (o NaN).
        const double height = Ground(services, position);
        if (!(height >= D(position.z)))
        {
            if (random.Next() % 2 == 0)
            {
                if (auto made = Emit(particles, DeathSpiritFireParticle, position, angles, white, 7,
                                     effect.transform.scale, worldClock, random, "0x5258ab");
                    !made.HasValue())
                    return made;
            }
            return BrightenTheBlizzard(effect, random, services, "0x52591a", "0x52bfc6");
        }
        return {};
    }
    if (effect.subtype != 0)
        return {};
    if (auto swayed = SwayTheBlizzard(effect, random, 10.0f, "0x52552d"); !swayed.HasValue())
        return swayed;
    if (auto made = Emit(particles, BreathParticle, position, angles, white, 0, 1.5f, worldClock, random, "0x5255bb");
        !made.HasValue())
        return made;
    const bool icy = random.Next() % 2 == 0;
    if (auto made = icy ? Emit(particles, 0x5ca, position, angles, white, 1, 0.5f, worldClock, random, "0x5255fd")
                        : Emit(particles, DeathSpiritFireParticle, position, angles, white, 7, effect.transform.scale,
                               worldClock, random, "0x5255fd");
        !made.HasValue())
        return made;
    if (auto bright = BrightenTheBlizzard(effect, random, services, "0x52566c", "0x52569a"); !bright.HasValue())
        return bright;
    if (auto ground = NeedTheGround(services, "0x5256a6"); !ground.HasValue())
        return ground;
    if (!(Ground(services, position) > D(position.z)))
        return {};
    const glm::vec3 frost{std::bit_cast<float>(0x3e75c28fu), std::bit_cast<float>(0x3e8f5c29u), 0.8f};
    const glm::vec3 point{position.x, position.y, F32(D(position.z) + 50.0)};
    const float scale = F32(static_cast<double>((random.Next() & 31) + 80) * static_cast<double>(0.025f)); // 0x6b8bd0
    if (auto made = Emit(particles, BreathParticle, point, angles, frost, StoneDustSubtype, scale, worldClock, random,
                         "0x525721");
        !made.HasValue())
        return made;
    // 0x525729: el rand() % 5 sale antes de 0x518cf0(0xca) (0x525750) y del 0x108/1 (0x52577a).
    const bool shard = random.Next() % 5 == 0;
    if (auto table = NeedTheEffects(services, shard ? "0x525750" : "0x52577a"); !table.HasValue())
        return table;
    if (shard)
        static_cast<void>(services.effects->CreateIceShard(point, angles, effect.light, random));
    const ModelEffectArguments shatter{.type = BlizzardEffect,
                                       .position = point,
                                       .angles = angles,
                                       .light = effect.light,
                                       .subtype = 1,
                                       .owner = nullptr,
                                       .key = effect.disturbanceState,
                                       .byte89 = 0,
                                       .byte88 = 0,
                                       .byte1bd = 0,
                                       .scale = 0.0f};
    if (auto made = CreateBlastModelEffect(*services.effects, shatter, {}, random, {}); !made.HasValue())
        return Core::Unexpected(made.Error());
    effect.alive = false;
    return {};
}

// 0x52b15a..0x52b268: los /1 y /2 del chorro 0x5de, dieciocho llamas 5de/0 (el /1) o chispas 5dc/13 (el /2) en un
// anillo de 250 alrededor de la posición, corridas al azar.
Core::Expected<void, std::string> StepTheFlameRing(ModelEffect& effect, Particles& particles, float worldClock,
                                                   CrtRandom& random)
{
    constexpr int Flames = 18;
    constexpr glm::vec3 Radius{0.0f, 250.0f, 0.0f}; // 0x437a0000
    const glm::vec3 white(1.0f);
    for (int flame = 0; flame < Flames; ++flame)
    {
        const glm::vec3 turn{0.0f, 0.0f, F32(static_cast<double>(flame) * 20.0)}; // 0x6b86d0
        glm::vec3 point = Rotated(Radius, turn);
        AddTo(point, effect.transform.position);
        point.x = F32(static_cast<double>((random.Next() & 63) - 32) + D(point.x));
        point.y = F32(static_cast<double>((random.Next() & 63) - 32) + D(point.y));
        const auto made = effect.subtype == 1 ? Emit(particles, FlameParticle, point, effect.transform.angles, white, 0,
                                                     std::bit_cast<float>(0x3f99999au), worldClock, random, "0x52b253")
                                              : Emit(particles, BlueTrailFireParticle, point, effect.transform.angles,
                                                     white, 13, 2.5f, worldClock, random, "0x52b253");
        if (!made.HasValue())
            return made;
    }
    return {};
}

// 0x525d6c..0x525e66: el 0x113.
Core::Expected<void, std::string> StepTheAirForce(ModelEffect& effect)
{
    if (effect.subtype == 0)
    {
        effect.objectState.light =
            F32(D(effect.objectState.light) * static_cast<double>(std::bit_cast<float>(0x3f2aaaabu))); // 0x6b8c64
        effect.transform.scale = F32(D(effect.transform.scale) + static_cast<double>(0.2f));           // 0x6b8854
    }
    if (effect.subtype != 1)
        return {};
    effect.objectState.light = F32(static_cast<double>(effect.life) * static_cast<double>(0.1f)); // 0x6b8a34
    const glm::vec3* angles = OwnerAngles(effect);
    if (auto followed = FollowTheOwner(effect, "0x525dbf"); !followed.HasValue())
        return followed;
    if (angles == nullptr)
        return Core::Unexpected(Cut("0x525de6", "el 0x113/1 lee los ángulos de su dueño, que MuRet no tiene"));
    effect.transform.angles = *angles;
    glm::vec3 offset = Rotated({0.0f, -70.0f, 0.0f}, effect.transform.angles); // 0xc28c0000
    offset.z = F32(D(offset.z) + 100.0);                                       // 0x6b86b4
    AddTo(effect.transform.position, offset);
    return {};
}
} // namespace

bool IsBlastModelEffect(std::int16_t type, std::int32_t subtype)
{
    if (type == FirstBigStoneEffect || type == SecondBigStoneEffect)
        return subtype == BigStoneOwnerSubtype;
    if (type == PiercingTrailEffect)
        return subtype == PiercingChildSubtype;
    if (type == FlameJetEffect)
        return subtype == 1 || subtype == 2;
    return type == ProtectionEffect || (type >= FirstIceRockEffect && type <= LastIceRockEffect) ||
           type == MagicCircleEffect || (type >= FallingRockEffect && type <= InfernoEffect) ||
           (type >= DeathStabEffect && type <= ShieldEffect) || type == FirstGateDebrisEffect ||
           type == SecondGateDebrisEffect || type == RidingSpearEffect || type == ComboEffect || type == AirForceEffect;
}

bool StepsAsBlastModelEffect(const ModelEffect& effect)
{
    return IsBlastModelEffect(effect.type, effect.subtype);
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastModelEffect(ModelEffects& effects, const ModelEffectArguments& arguments, const ModelEffectOwner& owner,
                       CrtRandom& random, const BlastEffectServices& services)
{
    if (!IsBlastModelEffect(arguments.type, arguments.subtype))
        return Core::Unexpected(Cut("0x518e3c", std::format("el alta del efecto {:#x}/{} no es de este lote",
                                                            arguments.type, arguments.subtype)));
    const auto slot = effects.Begin(arguments);
    if (!slot)
        return std::optional<std::size_t>{};
    ModelEffect& effect = effects.Effects()[*slot];
    effect.owner = owner;
    if (auto begun = BeginTheCase(effects, effect, arguments, random, services); !begun.HasValue())
        return Core::Unexpected(begun.Error());
    return slot;
}

Core::Expected<void, std::string> StepBlastModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton,
                                                       Particles& particles, float worldClock, CrtRandom& random,
                                                       int lightDraw, KalimaWorld* kalima,
                                                       const ModelEffectStepServices& services)
{
    const glm::vec3 before = effect.transform.position;
    const float luminosity = StepLuminosity(lightDraw, effect.life);
    const std::int16_t type = effect.type;
    Tail tail = Tail::Rotated;
    Core::Expected<void, std::string> stepped{};
    if (type == ShieldEffect)
    {
        // 0x5210a1..0x5210e0.
        stepped = FollowTheOwner(effect, "0x5210a1");
        if (stepped.HasValue() && effect.life < 10)
            effect.objectState.light = F32(static_cast<double>(effect.life) * static_cast<double>(0.1f));
    }
    else if (type == BlastEffect)
    {
        stepped = StepTheBlast(effect, luminosity, particles, worldClock, random, services);
    }
    else if (type == WaveForceEffect)
    {
        // 0x521316 y 0x5212b6..0x5212da.
        effect.objectState.light = F32(static_cast<double>(effect.life) * static_cast<double>(0.05f));
        stepped = Light(services, effect.transform.position, Shade(luminosity), 5, "0x5212da");
    }
    else if (type == InfernoEffect)
    {
        stepped = StepTheInferno(effect, luminosity, services);
    }
    else if (type == MagicCircleEffect)
    {
        stepped = StepTheMagicCircle(effect, luminosity, services);
    }
    else if (type >= FirstIceRockEffect && type <= LastIceRockEffect)
    {
        stepped = StepTheIceRock(effect, particles, worldClock, random, services);
        tail = Tail::None;
    }
    else if (type == ProtectionEffect)
    {
        // 0x5216ef..0x52173f.
        effect.objectState.light = std::bit_cast<float>(0x3fa66666u); // 1,3f
        stepped = FollowTheOwner(effect, "0x5216f6");
        if (stepped.HasValue())
        {
            effect.transform.angles.z = F32(D(effect.transform.angles.z) + 10.0); // 0x6b87a8
            const bool* alive = OwnerAlive(effect);
            const bool* seen = OwnerSeen(effect);
            if (alive == nullptr || seen == nullptr)
                stepped = Core::Unexpected(Cut("0x52172c", "el 0xe4 lee el byte vivo y +0x16c de su dueño"));
            else if (!*alive || !*seen)
                effect.alive = false;
        }
    }
    else if (type == SwordTrailEffect)
    {
        // 0x521821..0x521861: un 0xf4 con el subtipo 4 − vida, el mismo dueño, +0x8a, +0x89 y +0x88.
        tail = Tail::LifeOnly;
        stepped = NeedTheEffects(services, "0x521861");
        if (stepped.HasValue())
        {
            const ModelEffectArguments spin{.type = SpinningItemEffect,
                                            .position = effect.transform.position,
                                            .angles = effect.transform.angles,
                                            .light = effect.light,
                                            .subtype = 4 - effect.life,
                                            .owner = effect.ownerContext,
                                            .key = effect.disturbanceState,
                                            .byte89 = effect.byte89,
                                            .byte88 = effect.state,
                                            .byte1bd = 0,
                                            .scale = 0.0f};
            const BlastEffectServices record{.ownRecordDepth = services.ownRecordDepth,
                                             .ownRecordCount = services.ownRecordCount};
            const auto made = CreateBlastModelEffect(*services.effects, spin, effect.owner, random, record);
            if (!made.HasValue())
                stepped = Core::Unexpected(made.Error());
            else if (made.Value())
            {
                ModelEffect& item = services.effects->Effects()[*made.Value()];
                item.itemType = effect.itemType;
                item.itemLevel = effect.itemLevel;
            }
        }
    }
    else if (type == SpinningItemEffect)
    {
        tail = Tail::LifeOnly;
        stepped = StepTheSpinningItem(effect, luminosity, particles, worldClock, random, services);
    }
    else if (type == FirstBigStoneEffect || type == SecondBigStoneEffect)
    {
        // 0x523926..0x52396f: muerto el dueño, apaga el byte vivo y sigue; la posición, la del dueño más la partida.
        const bool* alive = OwnerAlive(effect);
        const glm::vec3* owner = OwnerPosition(effect);
        if (alive == nullptr || owner == nullptr)
            stepped = Core::Unexpected(Cut("0x52392b", "el 0xe5/5 lee el byte vivo y la posición de su dueño"));
        else
        {
            if (!*alive)
                effect.alive = false;
            for (int axis = 0; axis < 3; ++axis)
                effect.transform.position[axis] = F32(D((*owner)[axis]) + D(effect.start[axis]));
        }
    }
    else if (type == FallingRockEffect)
    {
        stepped = StepTheFallingRock(effect, random, services);
    }
    else if (type == DeathStabEffect)
    {
        stepped = StepTheDeathStab(effect, luminosity, particles, worldClock, random, services);
    }
    else if (type == PiercingTrailEffect)
    {
        stepped = StepThePiercingChild(effect, random, kalima, services);
    }
    else if (type == FirstGateDebrisEffect || type == SecondGateDebrisEffect)
    {
        stepped = StepTheGateDebris(effect, particles, random, services);
    }
    else if (type == BlizzardEffect)
    {
        stepped = StepTheBlizzard(effect, particles, worldClock, random, services);
    }
    else if (type == ComboEffect)
    {
        // 0x525a66..0x525a9e.
        if (effect.subtype == 0)
        {
            if (effect.life > 4)
            {
                effect.transform.scale = F32(D(effect.transform.scale) + D(effect.turnDirection));
                effect.turnDirection = F32(D(effect.turnDirection) + static_cast<double>(0.1f));
            }
            effect.objectState.light =
                F32(D(effect.objectState.light) * static_cast<double>(std::bit_cast<float>(0x3f36db6eu))); // 0x6ba268
        }
    }
    else if (type == AirForceEffect)
    {
        // 0x525d6c: sin dueño, apaga el byte vivo y sale sin la cola.
        if (!effect.hasOwner)
        {
            effect.alive = false;
            tail = Tail::None;
        }
        else
        {
            stepped = StepTheAirForce(effect);
        }
    }
    else if (type == RidingSpearEffect)
    {
        // 0x52d1a4: el 0x10f no tiene caso propio; la cola lo mueve sin girar la velocidad.
        tail = Tail::Straight;
    }
    else if (type == FlameJetEffect)
    {
        stepped = StepTheFlameRing(effect, particles, worldClock, random);
    }
    else
    {
        return Core::Unexpected(
            Cut("0x5208a0", std::format("el paso del efecto {:#x}/{} no es de este lote", type, effect.subtype)));
    }
    if (!stepped.HasValue())
        return stepped;
    FinishTheStep(effect, skeleton, services, tail, before);
    return {};
}
} // namespace Client::World
