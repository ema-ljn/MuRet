#include "Scenes/FireBreathEffect.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51f5fe (el alta del 0x6ac), 0x52c7f0 (su paso) y 0x51fef0.
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

// 0x5fc8f0 y 0x5fcbd0: el vector girado por los ángulos.
glm::vec3 Rotated(const glm::vec3& vector, const glm::vec3& angles)
{
    return Render::Models::OriginalVectorRotate(vector, Render::Models::OriginalAngleMatrix(angles));
}

// El fld/fadd/fstp de cada eje.
glm::vec3 Sum(const glm::vec3& a, const glm::vec3& b)
{
    return {F32(D(a.x) + D(b.x)), F32(D(a.y) + D(b.y)), F32(D(a.z) + D(b.z))};
}

// 0x51fef0(punto, 1): veinte 0x5c5/2 en el punto con la z 30 más, cada una con los ángulos (rand() % 60 + 150; 0;
// rand() % 30), la luz (1, 1, 1) y escala 1, sacados aunque no haya lugar; después, con el 1, la 0x5ee/0 ahí con los
// ángulos en 0 y escala 4.
Core::Expected<void, std::string> SparkAndBlast(const glm::vec3& position, const HighEffectStepContext& context)
{
    constexpr double Lift = 30.0; // 0x6b8858
    constexpr int Sparks = 20;
    constexpr int PitchSpan = 60, PitchBase = 150, YawSpan = 30;
    constexpr std::int32_t Spark = 0x5c5, SparkSubtype = 2;
    constexpr std::int32_t Blast = 0x5ee;
    constexpr float BlastScale = 4.0f; // 0x51ffbd
    const glm::vec3 point{position.x, position.y, F32(D(position.z) + Lift)};
    for (int spark = 0; spark < Sparks; ++spark)
    {
        const auto pitch = static_cast<float>(context.random.Next() % PitchSpan + PitchBase);
        const auto yaw = static_cast<float>(context.random.Next() % YawSpan);
        if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, Spark, point,
                                           {pitch, 0.0f, yaw}, glm::vec3(1.0f), SparkSubtype, 1.0f, 0x51ff7e);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, Blast, point, glm::vec3(0.0f),
                                       glm::vec3(1.0f), 0, BlastScale, 0x51ffd5);
        !made.HasValue())
        return Core::Unexpected(made.Error());
    return {};
}
} // namespace

Core::Expected<void, std::string> BeginTheFireBreath(World::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr std::int32_t Life = 10;              // 0x51f5fe
    constexpr float Push = -30.0f;                 // 0x51f61c, +0xd0
    constexpr glm::vec3 Lift{0.0f, -20.0f, 50.0f}; // 0x51f641..0x51f64f
    // 0x51f676..0x51f684 y 0x51f740..0x51f751.
    constexpr std::array<glm::vec3, 2> Mouths{{{-20.0f, -20.0f, 60.0f}, {30.0f, -20.0f, 60.0f}}};
    constexpr std::array<std::uint32_t, 2> MouthAddresses{0x51f719, 0x51f7dc};
    constexpr int TiltSpan = 20;
    constexpr double Tilt = 5.0; // 0x6b87f8
    constexpr int TurnStep = 18, Turns = 360;
    constexpr std::int32_t Spark = 0x617, SparkSubtype = 1;
    constexpr float SparkScale = 10.0f; // 0x51f6e3
    // [ebp+0xc] (edi): la posición que recibe el alta, la del llamador, no la del efecto que se corre.
    const glm::vec3 origin = effect.transform.position;
    const glm::vec3 angles = effect.transform.angles;
    effect.life = Life;
    effect.speed = 1.0f;
    effect.velocity = {0.0f, Push, 0.0f};
    effect.transform.position = Sum(Rotated(Lift, angles), origin);
    for (std::size_t mouth = 0; mouth < Mouths.size(); ++mouth)
    {
        const glm::vec3 point = Sum(Rotated(Mouths[mouth], angles), origin);
        // [ebp−0x30]: la x se rehace en cada vuelta y la y suma las vueltas sin volver a la del efecto.
        glm::vec3 turned = angles;
        for (int turn = 0; turn < Turns; turn += TurnStep)
        {
            const int tilt = context.random.Next() % TiltSpan;
            turned.x = F32(static_cast<double>(tilt) + D(angles.x) + Tilt);
            turned.y = F32(static_cast<double>(turn) + D(turned.y));
            if (auto joint = CreateHighJoint(context.world, context.random, Spark, point, point, turned, SparkSubtype,
                                             nullptr, SparkScale, MouthAddresses[mouth]);
                !joint.HasValue())
                return joint;
        }
    }
    return {};
}

Core::Expected<void, std::string> StepTheFireBreath(World::ModelEffect& effect, float luminosity,
                                                    const HighEffectStepContext& context)
{
    constexpr double Half = 0.5;                                                    // 0x6b882c
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0,8f
    constexpr std::int32_t LightRadius = 2;
    constexpr double Turn = 90.0; // 0x6b8948
    constexpr int SizeSpan = 3, SizeBase = 2;
    constexpr std::int32_t Fade = 15;
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8ad0, 0,05f
    // 0x52c8a2..0x52c8b0 y 0x52c90e..0x52c91c.
    constexpr std::array<glm::vec3, 2> Sides{{{0.0f, 20.0f, 0.0f}, {0.0f, -20.0f, 0.0f}}};
    constexpr std::array<std::uint32_t, 2> SideAddresses{0x52c8f7, 0x52c95d};
    constexpr std::int32_t Fire = 0x5db, FireSubtype = 0xa;
    // 0x52c7f0..0x52c816: FST de la mitad en la x y la y, FSTP del 0,8f en la z.
    const double lum = D(luminosity);
    const float half = F32(lum * Half);
    const glm::vec3 color{half, half, F32(lum * Blue)};
    if (auto lit = World::AddDynamicLight(context.services.lighting, effect.transform.position, color, LightRadius,
                                          "0x52c820");
        !lit.HasValue())
        return lit;
    const glm::vec3& angles = effect.transform.angles;
    const glm::vec3 turned{angles.x, angles.y, F32(D(angles.z) + Turn)};
    // 0x52c852..0x52c88b: el rand() de la escala va al FILD antes que la vida.
    const int size = context.random.Next() % SizeSpan + SizeBase;
    effect.transform.scale = F32(static_cast<double>(size) * (static_cast<double>(Fade - effect.life) * Growth));
    for (std::size_t side = 0; side < Sides.size(); ++side)
    {
        effect.start = Sum(Rotated(Sides[side], turned), effect.transform.position);
        if (auto made =
                CreateHighParticle(context.world, context.random, context.worldClock, Fire, effect.start, angles,
                                   glm::vec3(1.0f), FireSubtype, effect.transform.scale, SideAddresses[side]);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    // 0x52c962..0x52c974.
    if (effect.life == 1)
        return SparkAndBlast(effect.transform.position, context);
    return {};
}
} // namespace Client::Scenes
