#include "Scenes/SkillBurstEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "Kernel/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

// Cotejado con el PE c32bbd5f: las altas 0x51e397, 0x51e529 y 0x51e773, los pasos 0x52a6d6, 0x52abbe, 0x52acc3 y
// 0x529dee, y la rama del 0x5ca de 0x518b60.
namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;

constexpr double Fifth = Math::FifthAsDouble;                                          // 0x6b8854, 0,2f
constexpr double TwoFifths = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));   // 0x6b87b4, 0,4f
constexpr double ThreeFifths = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0,6f
constexpr double ThreeTenths = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
constexpr std::int32_t BurstLightRadius = 2; // el cuarto argumento de 0x5f9780 en 0x52ac4b y 0x52acff

// El personaje de `world` que es el dueño opaco (+0x108) de un efecto, o nullptr si no es uno de ellos.
const ActorState* ActorOwnerOf(const WorldState& world, const void* owner)
{
    for (const ActorState& actor : world.Actors())
    {
        if (&actor == owner)
            return &actor;
    }
    return nullptr;
}

// 0x518b60 con el 0x5ca (0x518bfd..0x518c13): con el dueño vivo (+0), visto (+0x16c) y a 100 o menos en el plano
// (FSQRT de dy² + dx², sin orden también sigue), la vida en 1 y la partícula 0x5c6/1 en su posición y con sus
// ángulos, en blanco (0x518bd2) y de escala 6.
Core::Expected<void, std::string> HitTheEnergyBallOwner(World::ModelEffect& effect,
                                                        const HighEffectStepContext& context)
{
    if (effect.ownerContext == nullptr)
        return {};
    const ActorState* owner = ActorOwnerOf(context.world, effect.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(
            std::string("0x518b7a: el dueño (+0x108) del efecto 0x5ca no es un personaje del mundo (CL11)"));
    if (!owner->alive || !owner->seen)
        return {};
    constexpr double Reach = 100.0; // 0x6b86b4
    const double dx = D(effect.transform.position.x) - D(owner->position.x);
    const double dy = D(effect.transform.position.y) - D(owner->position.y);
    if (std::sqrt((dy * dy) + (dx * dx)) > Reach)
        return {};
    effect.life = 1;
    constexpr std::int32_t SparkParticle = 0x5c6, HitSubtype = 1;
    constexpr float HitScale = 6.0f; // 0x40c00000
    const auto made =
        CreateHighParticle(context.world, context.random, context.worldClock, SparkParticle, effect.transform.position,
                           effect.transform.angles, glm::vec3(1.0f), HitSubtype, HitScale, 0x518c13);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    return {};
}
} // namespace

void BeginTheSweepEmitter(World::ModelEffect& effect)
{
    constexpr std::int32_t SweepLife = 30;                    // 0x1e
    constexpr float Glow = std::bit_cast<float>(0x3f4ccccdu); // 0,8f
    constexpr double Tilt = 45.0;                             // 0x6b8848
    effect.life = SweepLife;
    effect.light = glm::vec3(Glow);
    // 0x51e3b8..0x51e3c7: la x y la y en ESI (0); la z del argumento, que el prefijo copió en los ángulos.
    effect.copiedAngles.x = 0.0f;
    effect.copiedAngles.y = 0.0f;
    effect.copiedAngles.z = F32(D(effect.transform.angles.z) + Tilt);
    effect.start = effect.transform.position;
}

void BeginTheEnergyBall(World::ModelEffect& effect)
{
    constexpr std::int32_t BallLife = 20;                          // 0x14
    constexpr float Push = std::bit_cast<float>(0xc2700000u);      // −60
    constexpr double Lift = 100.0;                                 // 0x6b86b4
    constexpr float WideScale = std::bit_cast<float>(0x3fc00000u); // 1,5f
    effect.life = BallLife;
    effect.velocity = {0.0f, Push, 0.0f};
    effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
    if (effect.subtype == 1)
        effect.transform.scale = WideScale;
}

void BeginTheFlameBurst(World::ModelEffect& effect)
{
    constexpr std::int32_t BurstLife = 10;                     // 0xa
    constexpr float Ahead = std::bit_cast<float>(0xc2700000u); // −60
    constexpr double Lift = 130.0;                             // 0x6ba194
    effect.life = BurstLife;
    const glm::vec3 offset =
        Math::OriginalVectorRotate({0.0f, Ahead, 0.0f}, Math::OriginalAngleMatrix(effect.transform.angles));
    glm::vec3& position = effect.transform.position;
    position.x = F32(D(offset.x) + D(position.x));
    position.y = F32(D(offset.y) + D(position.y));
    position.z = F32(D(offset.z) + D(position.z) + Lift);
}

Core::Expected<void, std::string> StepTheSweepEmitter(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t SweepLife = 30;
    if (effect.life != SweepLife)
        return {};
    constexpr double Lift = 100.0; // 0x6b86b4
    constexpr std::int32_t SweepJoint = 0x61c, SweepSubtype = 0, NoTarget = -1;
    constexpr float SweepWidth = std::bit_cast<float>(0x43160000u); // 150
    glm::vec3 point{effect.transform.position.x, effect.transform.position.y,
                    F32(D(effect.transform.position.z) + Lift)};
    glm::vec3 angles = effect.copiedAngles;
    // El dueño del joint es el del efecto (el personaje de la habilidad): el 0x61c/0 no lo lee, sólo lo compara con el
    // héroe en su paso (SweepJoints.cpp); por el tipo, ByType.
    const World::FishEffectRequest request{.type = SweepJoint,
                                           .firstPosition = point,
                                           .secondPosition = point,
                                           .angles = angles,
                                           .subtype = SweepSubtype,
                                           .ownerContext = effect.ownerContext,
                                           .scale = SweepWidth,
                                           .key = effect.disturbanceState,
                                           .skill = effect.byte89,
                                           .skillSerial = 0,
                                           .target = NoTarget};
    const auto made = CreateFaunaJoint(context.world.DrawTail().joints, request, context.random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("0x52a72a: {} (CL11)", made.Error()));
    return {};
}

Core::Expected<void, std::string> StepTheEnergyBall(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    const float level = F32(static_cast<double>(effect.life) * Fifth);
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    if (effect.subtype == 0)
        color = glm::vec3(level);
    else if (effect.subtype == 1)
        color = {1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3e4ccccdu)};
    constexpr std::int32_t BallParticle = 0x5ca, SparkParticle = 0x5c6;
    constexpr float SparkScale = 4.0f; // 0x40800000
    const auto ball = CreateHighParticle(context.world, context.random, context.worldClock, BallParticle,
                                         effect.transform.position, effect.transform.angles, color, 0, 1.0f, 0x52ac16);
    if (!ball.HasValue())
        return Core::Unexpected(ball.Error());
    const auto spark =
        CreateHighParticle(context.world, context.random, context.worldClock, SparkParticle, effect.transform.position,
                           effect.transform.angles, color, 0, SparkScale, 0x52ac2f);
    if (!spark.HasValue())
        return Core::Unexpected(spark.Error());
    const glm::vec3 light{F32(D(level) * Fifth), F32(D(level) * TwoFifths), level};
    if (auto lit = World::AddDynamicLight(context.services.lighting, effect.transform.position, light, BurstLightRadius,
                                          "0x52ac64");
        !lit.HasValue())
        return lit;
    return HitTheEnergyBallOwner(effect, context);
}

Core::Expected<void, std::string> StepTheFlameBurst(World::ModelEffect& effect, float luminosity,
                                                    const HighEffectStepContext& context)
{
    constexpr std::int32_t BurstParticle = 0x5da, BurstSubtype = 1;
    const auto made =
        CreateHighParticle(context.world, context.random, context.worldClock, BurstParticle, effect.transform.position,
                           effect.transform.angles, glm::vec3(1.0f), BurstSubtype, 1.0f, 0x52acf2);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    const glm::vec3 light{luminosity, F32(D(luminosity) * ThreeFifths), F32(D(luminosity) * ThreeTenths)};
    return World::AddDynamicLight(context.services.lighting, effect.transform.position, light, BurstLightRadius,
                                  "0x52b3a9");
}

Core::Expected<void, std::string> StepTheArrowBlast(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr double Pitch = 5.0, Drop = 8.0; // 0x6b87f8 y 0x6b86c4
    effect.transform.angles.x = F32(D(effect.transform.angles.x) - Pitch);
    effect.velocity.y = F32(D(effect.velocity.y) - Drop);
    constexpr std::int32_t LastLives = 2, FiredSubtype = 1;
    if (effect.life < LastLives && effect.subtype == 0 && effect.ownerContext != nullptr)
    {
        // 0x529e31 y 0x529e37: el pitch en 90 y el subtipo en 1 antes de leer al dueño (+0x108, 0x529e3e).
        constexpr float Upright = std::bit_cast<float>(0x42b40000u); // 90
        effect.transform.angles.x = Upright;
        effect.subtype = FiredSubtype;
        const ActorState* owner = ActorOwnerOf(context.world, effect.ownerContext);
        if (owner == nullptr)
            return Core::Unexpected(
                std::string("0x529e3e: el dueño (+0x108) del efecto 0x13a no es un personaje del mundo (CL11)"));
        constexpr int Spread = 100;                      // 0x64
        constexpr double Center = 50.0, Height = 1200.0; // 0x6b86b0 y 0x6ba198
        glm::vec3 point = owner->position;
        point.x = F32((static_cast<double>(context.random.Next() % Spread) - Center) + D(point.x));
        point.y = F32((static_cast<double>(context.random.Next() % Spread) - Center) + D(point.y));
        point.z = F32(D(point.z) + Height);
        constexpr std::int32_t RainJoint = 0x632, FirstRain = 2, SecondRain = 3;
        constexpr float RainWidth = 50.0f; // 0x42480000
        if (auto made = CreateHighJoint(context.world, context.random, RainJoint, point, point, effect.transform.angles,
                                        FirstRain, &effect, RainWidth, 0x529eca);
            !made.HasValue())
            return made;
        return CreateHighJoint(context.world, context.random, RainJoint, point, point, effect.transform.angles,
                               SecondRain, &effect, RainWidth, 0x529eed);
    }
    if (effect.subtype == FiredSubtype)
        effect.alive = false;
    return {};
}
} // namespace Client::Scenes
