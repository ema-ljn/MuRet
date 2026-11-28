#include "Effects/KundunEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/HighModelEffects.h"
#include "Effects/KundunFallEffects.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// Cotejado con el PE c32bbd5f: 0x518f38, 0x51cebf y 0x51cdc9 (las altas del 0x128, el 0x126 y el 0x125) y 0x527ad6,
// 0x52778c y 0x5276b7 (sus pasos).
namespace Client::Effects
{
namespace
{
using Math::D;
using Math::F32;

constexpr std::int32_t SpiritJoint = 0x6ee;
constexpr float SpiritWidth = 100.0f;    // 0x42c80000
constexpr std::int32_t SpiritKey = 0;    // el 8.º argumento de 0x530860 ([ebp+0x24]): esi en 0x51ce6a; 0 en 0x52774b
constexpr int FullTurn = Math::FullTurn; // 0x168
constexpr std::int32_t CallLife = 30;    // 0x1e, 0x51cf97 y 0x527add

// 0x518cf0(tipo, posición del 0x128, ángulos, luz del 0x128, 0, 0, −1, 0, 0, 0, 0) desde su paso, en `address`.
Core::Expected<void, std::string> CallOut(const Effects::ModelEffect& call, std::int16_t type, const glm::vec3& angles,
                                          const HighEffectContext& creation, std::uint32_t address)
{
    const auto made = CreateHighModelEffect(
        {.type = type, .position = call.transform.position, .angles = angles, .light = call.light}, creation);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    return {};
}
} // namespace

void BeginTheKundunCall(Effects::ModelEffects& effects, std::size_t slot)
{
    constexpr std::int32_t EchoSubtype = 2;
    Effects::ModelEffect& effect = effects.Effects()[slot];
    if (effect.subtype == 0 || effect.subtype == 1)
    {
        effect.life = CallLife;
        return;
    }
    if (effect.subtype != EchoSubtype)
        return;
    // 0x518f62 y 0x518f69: antes de volver a entrar.
    constexpr std::int32_t EchoLife = 40; // 0x28
    effect.life = EchoLife;
    effect.disturbanceState = 0;
    static_cast<void>(CreateTheKundunEcho(effects, slot));
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateTheKundunCall(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments)
{
    if (arguments.type != KundunCallEffect)
        return Core::Unexpected(std::format("0x518cf0: el alta de Kundun recibe el tipo {:#x}", arguments.type));
    const auto slot = effects.Begin(arguments);
    if (!slot.has_value())
        return slot;
    BeginTheKundunCall(effects, *slot);
    return slot;
}

Core::Expected<void, std::string> StepTheKundunCall(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    const HighEffectContext creation{.world = context.world,
                                     .random = context.random,
                                     .worldClock = context.worldClock,
                                     .models = context.models,
                                     .terrain = context.terrain,
                                     .faunaModels = context.faunaModels};
    if (effect.subtype == 0)
    {
        if (effect.life != CallLife)
            return {};
        constexpr int Fan = 0x50, FanStep = 8;
        constexpr double Offset = static_cast<double>(320.0f); // 0x6ba22c, el float de un fadd DWORD (0x527b18)
        for (int turn = 0; turn < Fan; turn += FanStep)
        {
            const glm::vec3 angles{0.0f, 0.0f, F32(static_cast<double>(turn) + D(effect.transform.angles.z) + Offset)};
            if (auto made = CallOut(effect, KundunPhoenixEffect, angles, creation, 0x527b37); !made.HasValue())
                return made;
        }
        return {};
    }
    // La clave (+0x8a) leída con MOVSX: el −1 del alta de Kundun es −1.
    const auto key = [&effect] { return static_cast<std::int16_t>(effect.disturbanceState); };
    constexpr int KeyTurn = 30; // (clave · 15) · 2
    if (effect.subtype == 1)
    {
        if (effect.life == CallLife)
        {
            constexpr int Spirits = 0x14;
            for (int spirit = 0; spirit < Spirits; ++spirit)
            {
                const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(key() * KeyTurn)};
                if (auto made = CallOut(effect, KundunSpiritEffect, angles, creation, 0x527bb8); !made.HasValue())
                    return made;
            }
        }
        constexpr std::int16_t After = 5, Before = 10;
        if (key() > After && key() < Before)
        {
            const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(key() * KeyTurn)};
            if (auto made = CallOut(effect, KundunSpiritEffect, angles, creation, 0x527c13); !made.HasValue())
                return made;
        }
        effect.disturbanceState = static_cast<std::uint16_t>(effect.disturbanceState + 1U);
        return {};
    }
    constexpr std::int32_t EchoSubtype = 2, EchoLife = 10;
    if (effect.subtype == EchoSubtype && key() == 0 && effect.life < EchoLife)
        effect.disturbanceState = 1;
    return {};
}

Core::Expected<void, std::string> BeginTheKundunPhoenix(Effects::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr float Speed = std::bit_cast<float>(0x3eae147bu); // 0,34f
    constexpr std::int32_t PhoenixLife = 20;                   // 0x14
    constexpr float Scale = std::bit_cast<float>(0x3f333333u); // 0,7f
    constexpr float Glow = 0.5f;                               // 0x3f000000
    constexpr std::int32_t TrailJoint = 0x613, TrailSubtype = 0xf;
    constexpr double Radians = Math::DegreesToRadiansAsDouble; // 0x6b8cf8, 0,01745329f
    constexpr int Frames = 5;
    effect.speed = Speed;
    effect.life = PhoenixLife;
    effect.objectState.blendMesh = -2;
    effect.transform.scale = Scale;
    effect.light = glm::vec3(Glow);
    effect.alpha = 0.0f;
    const glm::vec3 point = effect.transform.position;
    const glm::vec3 angles{0.0f, 0.0f, F32(D(effect.transform.angles.z) * Radians)};
    if (auto made = CreateHighJoint(context.world, context.random, TrailJoint, point, point, angles, TrailSubtype,
                                    &effect, SpiritWidth, 0x51cf45, SpiritKey);
        !made.HasValue())
        return made;
    effect.animation.frame = static_cast<float>(context.random.Next() % Frames);
    return {};
}

void StepTheKundunPhoenix(Effects::ModelEffect& effect)
{
    constexpr std::int32_t FadeLife = 5;                                                // edi, 0x5208df
    constexpr double FadeStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0,2f
    if (effect.life >= FadeLife)
        return;
    effect.alpha = F32(static_cast<double>(effect.life) * FadeStep);
}

Core::Expected<void, std::string> BeginTheKundunSpirit(Effects::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr std::int32_t SpiritLife = 30;
    constexpr float Glow = 0.5f;                         // 0x3f000000
    constexpr double Lift = static_cast<double>(100.0f); // 0x6b86b4, el float de un fadd DWORD (0x51cdfe)
    constexpr int ReachSpan = 600, ReachBase = 200;      // 0x258 y 0xc8
    constexpr std::int32_t OrbitSubtype = 0x12;
    constexpr float Pitch = -30.0f; // 0xc1f00000
    effect.life = SpiritLife;
    effect.objectState.blendMesh = -2;
    effect.transform.scale = 1.0f;
    effect.light = glm::vec3(Glow);
    effect.alpha = 0.0f;
    // 0x51ce04..0x51ce13: el punto toma la posición antes de la subida.
    const glm::vec3 before = effect.transform.position;
    effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
    const float turn = static_cast<float>(context.random.Next() % FullTurn);
    const float reach = static_cast<float>(context.random.Next() % ReachSpan + ReachBase);
    // 0x52e3a0 y 0x52e390: FSIN y FCOS del giro, retenidos en la x87 hasta el FMUL por el radio.
    const auto sine = Math::Detail::SineProductPC53(D(turn), reach);
    const auto cosine = Math::Detail::CosineProductPC53(D(turn), reach);
    if (!sine || !cosine)
        return Core::Unexpected(std::string("0x51ce4f: el giro del 0x125 fuera del perfil PC53"));
    const glm::vec3 point{F32(*sine + D(before.x)), F32(*cosine + D(before.y)), 0.0f};
    if (auto made = CreateHighJoint(context.world, context.random, SpiritJoint, point, point, effect.transform.angles,
                                    OrbitSubtype, &effect, SpiritWidth, 0x51ce91, SpiritKey);
        !made.HasValue())
        return made;
    effect.transform.angles = {Pitch, 0.0f, static_cast<float>(context.random.Next() % FullTurn)};
    return {};
}

Core::Expected<void, std::string> StepTheKundunSpirit(Effects::ModelEffect& effect,
                                                      const HighEffectStepContext& context)
{
    constexpr int SpinSpan = 10, SpinBase = 10; // 0xa
    effect.transform.angles.z =
        F32(static_cast<double>(context.random.Next() % SpinSpan + SpinBase) + D(effect.transform.angles.z));
    constexpr float Low = 300.0f, High = 600.0f; // 0x6b8900 y 0x6b8c08
    // 0x5276d2..0x5276f4: JNE con C0 o C3 (hasta 300 o sin orden) y JE sin C0 (600 o más): a la cola.
    const float height = effect.transform.position.z;
    if (!(height > Low) || !(height < High))
        return {};
    constexpr float Hover = 350.0f; // 0x43af0000
    glm::vec3 point{effect.transform.position.x, effect.transform.position.y, Hover};
    if ((context.random.Next() & 1) != 0)
        return {};
    constexpr int Spokes = 24;    // 0x18
    constexpr int SpokeTurn = 30; // (k · 15) · 2
    constexpr std::int32_t FanSubtype = 0xe;
    const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(context.random.Next() % Spokes * SpokeTurn)};
    return CreateHighJoint(context.world, context.random, SpiritJoint, point, point, angles, FanSubtype, nullptr,
                           SpiritWidth, 0x52777f, SpiritKey);
}
} // namespace Client::Effects
