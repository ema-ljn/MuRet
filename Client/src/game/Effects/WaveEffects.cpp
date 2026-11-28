#include "Effects/WaveEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/HighModelEffects.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51c129..0x51c2b2 (el alta del 0x114 con el subtipo 0 o 1, y lo común).
namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;

constexpr std::int32_t WideSpan = 40, WideBase = 70;

// rand() % 40 + 70, el FILD de la escala de los joints (0x51c191 y 0x51c238).
float JointScale(Crt::CrtRandom& random)
{
    return static_cast<float>(random.Next() % WideSpan + WideBase);
}

// 0x51c186..0x51c1c1: sesenta 0x530860(0x5ac, posición, posición, ángulos, 0, 0, escala, −1, 0, 0, −1).
Core::Expected<void, std::string> ScatterTheFlares(World::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr int Flares = 60;
    constexpr std::int32_t FlareJoint = 0x5ac;
    for (int flare = 0; flare < Flares; ++flare)
    {
        const float scale = JointScale(context.random);
        if (auto made =
                CreateHighJoint(context.world, context.random, FlareJoint, effect.transform.position,
                                effect.transform.position, effect.transform.angles, 0, nullptr, scale, 0x51c1b8);
            !made.HasValue())
            return made;
    }
    return {};
}

// 0x51c1d3..0x51c2ab, la /1.
Core::Expected<void, std::string> RaiseTheSwirl(World::ModelEffect& effect,
                                                const Kernel::ModelEffectArguments& arguments,
                                                const HighEffectContext& context)
{
    constexpr std::int32_t SwirlLife = 15;
    constexpr std::uint32_t SwirlFlags = 0x2000;
    constexpr double Rise = 80.0;                                                        // 0x6b9ffc
    constexpr int ScaleSpan = 50;                                                        // 0x51c1f3
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b86c0, 0,01f
    constexpr double ScaleBase = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    constexpr float SwirlGrowth = std::bit_cast<float>(0x3c23d70au);                     // 0x51c21f, 0,01f
    constexpr float Upright = 90.0f;                                                     // 0x42b40000
    constexpr int Swirls = 2;                                                            // 0x51c209
    constexpr std::int32_t SwirlJoint = 0x6e8;
    constexpr int DriftSpan = 100;
    constexpr double DriftCenter = 50.0; // 0x6b86b0
    effect.life = SwirlLife;
    effect.renderFlags = SwirlFlags;
    glm::vec3& position = effect.transform.position;
    position.z = F32(D(position.z) + Rise);
    const int size = context.random.Next() % ScaleSpan;
    effect.transform.scale = F32(static_cast<double>(size) * ScaleStep + ScaleBase);
    effect.turnDirection = SwirlGrowth;
    effect.transform.angles.x = Upright;
    effect.transform.angles.z = arguments.angles.z;
    for (int swirl = 0; swirl < Swirls; ++swirl)
    {
        const float scale = JointScale(context.random);
        if (auto made = CreateHighJoint(context.world, context.random, SwirlJoint, position, position,
                                        effect.transform.angles, 0, nullptr, scale, 0x51c25f);
            !made.HasValue())
            return made;
    }
    position.x = F32(static_cast<double>(context.random.Next() % DriftSpan) - DriftCenter + D(position.x));
    position.y = F32(static_cast<double>(context.random.Next() % DriftSpan) - DriftCenter + D(position.y));
    return {};
}
} // namespace

Core::Expected<void, std::string> BeginTheWave(World::ModelEffect& effect,
                                               const Kernel::ModelEffectArguments& arguments,
                                               const HighEffectContext& context)
{
    constexpr std::int32_t WaveLife = 20;
    constexpr float WaveGrowth = std::bit_cast<float>(0x3dcccccdu); // 0x51c130, 0,1f
    constexpr double Lift = 50.0;                                   // 0x6b86b0
    effect.life = WaveLife;
    effect.turnDirection = WaveGrowth;
    effect.objectState.blendMesh = Models::EveryMeshBlends;
    effect.objectState.light = 1.0f;
    effect.transform.angles = glm::vec3(0.0f);
    effect.start = effect.transform.position;
    effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
    if (effect.subtype == 0)
        return ScatterTheFlares(effect, context);
    if (effect.subtype == 1)
        return RaiseTheSwirl(effect, arguments, context);
    return {};
}
} // namespace Client::Scenes
