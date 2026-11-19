#include "Scenes/KundunEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51cdc9 (el alta del 0x125) y 0x5276b7 (su paso).
namespace Client::Scenes
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

constexpr std::int32_t SpiritJoint = 0x6ee;
constexpr float SpiritWidth = 100.0f; // 0x42c80000
constexpr std::int32_t SpiritKey = 0; // el 8.º argumento de 0x530860 ([ebp+0x24]): esi en 0x51ce6a; 0 en 0x52774b
constexpr int FullTurn = 360;         // 0x168
} // namespace

Core::Expected<void, std::string> BeginTheKundunSpirit(World::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr std::int32_t SpiritLife = 30;
    constexpr float Glow = 0.5f;                    // 0x3f000000
    constexpr double Lift = 100.0;                  // 0x6b86b4, 100,0f
    constexpr int ReachSpan = 600, ReachBase = 200; // 0x258 y 0xc8
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
    const auto sine = Render::Models::Detail::SineProductPC53(D(turn), reach);
    const auto cosine = Render::Models::Detail::CosineProductPC53(D(turn), reach);
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

Core::Expected<void, std::string> StepTheKundunSpirit(World::ModelEffect& effect, const HighEffectStepContext& context)
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
} // namespace Client::Scenes
