#include "Effects/KalimaMonsterEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/HighModelEffects.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51c884 (el alta del 0x11a), 0x5266ea (su paso), 0x51ed79 (el alta del 0x615) y
// 0x52b4a7 (su paso).
namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;
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

void BeginTheThunder(World::ModelEffect& effect)
{
    constexpr std::int32_t Life = 10;                 // 0x51ed79
    constexpr glm::vec3 Offset{-25.0f, -80.0f, 0.0f}; // 0x51eda2..0x51edb0
    effect.life = Life;
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr || effect.subtype != 0)
        return;
    const glm::vec3 step = Math::OriginalVectorRotate(Offset, Math::OriginalAngleMatrix(owner->angles));
    glm::vec3& position = effect.transform.position;
    position = {F32(D(step.x) + D(position.x)), F32(D(step.y) + D(position.y)), F32(D(step.z) + D(position.z))};
}

Core::Expected<void, std::string> StepTheThunder(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t BoltLife = 5; // edi, 0x5208df
    constexpr int Spread = 64;
    constexpr double Centre = 32.0; // 0x6b883c
    constexpr int ScaleSpan = 10;
    constexpr double ScaleBase = 50.0; // 0x6b86b0
    constexpr std::int32_t Bolt = 0x615, BoltSubtype = 5;
    if (effect.life >= BoltLife || effect.life % 2 != 0 || effect.subtype != 0)
        return {};
    glm::vec3 point = effect.transform.position;
    point.x = F32(static_cast<double>(context.random.Next() % Spread) - Centre + D(point.x));
    point.y = F32(static_cast<double>(context.random.Next() % Spread) - Centre + D(point.y));
    const float scale = F32(static_cast<double>(context.random.Next() % ScaleSpan) + ScaleBase);
    return CreateHighJoint(context.world, context.random, Bolt, point, point, effect.transform.angles, BoltSubtype,
                           nullptr, scale, 0x52b560);
}
} // namespace Client::Scenes
