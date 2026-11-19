#include "Scenes/KalimaMonsterEffects.h"

#include "Core/Expected.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51c884 (el alta del 0x11a) y 0x5266ea (su paso).
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
} // namespace

void BeginTheSeaman(World::ModelEffect& effect)
{
    constexpr std::int16_t HiddenBlend = -2;                       // 0x51c884
    constexpr float MeshLight = std::bit_cast<float>(0x3e4ccccdu); // 0x51c88b, 0,2f
    constexpr float Scale = std::bit_cast<float>(0x3ecccccdu);     // 0x51c892, 0,4f
    constexpr std::int32_t Life = 20;                              // 0x51c899
    constexpr float Lift = 120.0f;                                 // 0x51c8a0, +0xe4
    constexpr float Push = -40.0f;                                 // 0x51c8aa, +0xd8
    effect.objectState.blendMesh = HiddenBlend;
    effect.objectState.light = MeshLight;
    effect.transform.scale = Scale;
    effect.life = Life;
    effect.turnDirection = Lift;
    effect.speed = Push;
    effect.velocity = {0.0f, Push, 0.0f};
    effect.light = glm::vec3(1.0f);
    effect.start = effect.transform.position;
}

Core::Expected<void, std::string> StepTheSeaman(World::ModelEffect& effect, float luminosity,
                                                const HighEffectStepContext& context)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0,02f
    constexpr double Brake = 10.0;                                                    // 0x6b87a8
    constexpr double Drop = 20.0;                                                     // 0x6b86d0
    constexpr double Floor = 60.0;                                                    // 0x6b86cc
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));   // 0x6b87b8, 0,1f
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3e99999au));  // 0x6b89dc, 0,3f
    constexpr double Red = static_cast<double>(std::bit_cast<float>(0x3f19999au));    // 0x6b8a20, 0,6f
    constexpr std::int32_t Spray = 0x6f4, SpraySubtype = 1;
    constexpr int Sprays = 4;
    constexpr std::int32_t LightRadius = 3;
    effect.transform.scale = F32(D(effect.transform.scale) + Growth);
    effect.transform.position.z = F32(D(effect.start.z) + D(effect.turnDirection));
    effect.speed = F32(D(effect.speed) - Brake);
    // 0x526717..0x526736: FST de la resta y FCOMP del valor de la pila con 60.
    const double lift = D(effect.turnDirection) - Drop;
    effect.turnDirection = lift < Floor ? static_cast<float>(Floor) : F32(lift);
    const double lum = D(luminosity);
    const glm::vec3 color{F32(lum * Blue), F32(lum * Green), F32(lum * Red)};
    effect.velocity = {0.0f, effect.speed, 0.0f};
    for (int spray = 0; spray < Sprays; ++spray)
    {
        if (auto made =
                CreateHighParticle(context.world, context.random, context.worldClock, Spray, effect.transform.position,
                                   effect.transform.angles, color, SpraySubtype, 1.0f, 0x52679d);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return World::AddDynamicLight(context.services.lighting, effect.transform.position, color, LightRadius, "0x5267ba");
}
} // namespace Client::Scenes
