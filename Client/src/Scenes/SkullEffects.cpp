#include "Scenes/SkullEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorFlags.h"
#include "Scenes/ActorState.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

// Cotejado con el PE c32bbd5f: 0x51ee23..0x51ee95 (el alta) y 0x52b59b..0x52b8a1 (el paso, con la tabla de 0x52d18c).
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

constexpr std::int32_t PlainSubtype = 0;        // el de la bandera 0x40 (0x4e688c, 0x4cd664)
constexpr std::int32_t OrbitSubtype = 1;        // gira como el /0
constexpr std::int32_t GlowSubtype = 2;         // un sprite que se apaga con la vida
constexpr std::int32_t ReverseOrbitSubtype = 3; // gira al revés, más alto y con otra luz
constexpr std::int32_t FallingSubtype = 4;      // cae sin mirar al dueño
constexpr std::int32_t StackSubtype = 5;        // el de la bandera 0x1000 (0x60c6d0)

constexpr std::uint16_t SkullSprite = 0x628; // Skill\Skull.jpg (0x61db86)
constexpr std::int32_t OrbitParticle = 0x5de;
constexpr std::int32_t StackParticle = 0x5ac;
constexpr std::int32_t OrbitParticleSubtype = 5;
constexpr std::int32_t StackParticleSubtype = 5;
constexpr float OrbitParticleScale = std::bit_cast<float>(0x3ecccccdu); // 0x52b6dd, 0,4f
constexpr float StackParticleScale = std::bit_cast<float>(0x3f333333u); // 0x52b7af, 0,7f
constexpr int Turns = 3;

constexpr double TurnStep = static_cast<double>(std::bit_cast<float>(0x40060a92u));   // 0x6ba214, 2π/3
constexpr double LifeTurn = static_cast<double>(std::bit_cast<float>(0x3e2e147bu));   // 0x6ba218, 0,17f
constexpr double WobbleStep = static_cast<double>(std::bit_cast<float>(0x4175eb85u)); // 0x6ba210, 15,37f
constexpr double OrbitClock = static_cast<double>(std::bit_cast<float>(0x3b4b295fu)); // 0x6ba20c, 0,0031f
constexpr double StackClock = static_cast<double>(std::bit_cast<float>(0x3b449ba6u)); // 0x6b89cc, 0,003f
constexpr float WobbleDepth = 20.0f;                                                  // 0x6b86d0
constexpr double RadiusBase = 50.0;                                                   // 0x6b86b0
constexpr float StackRadius = 50.0f;                                                  // 0x6b86b0
constexpr double StackSpacing = 50.0;                                                 // 0x6b86b0
constexpr double OrbitHeight = 200.0;                                                 // 0x6b8928
constexpr double ReverseOrbitHeight = 250.0;                                          // 0x6b8920
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));      // 0x6b87b8, 0,1f

// 0x52b6ea..0x52b71c: la luz de la partícula 0x5de, (0,65f; 0,65f; 0,65f) o, el /3, (0,1f; 1; 1).
constexpr float Grey = std::bit_cast<float>(0x3f266666u);
constexpr float Cyan = std::bit_cast<float>(0x3dcccccdu);
// 0x52b817..0x52b839: la de la 0x5ac, (0,6f; 0,6f; 1).
constexpr float Pale = std::bit_cast<float>(0x3f19999au);

// 0x52b612..0x52b63b: los bits que el /0 mira en su propio +0x78 (no en el del dueño). MuRet no escribe ese campo en
// ningún efecto, como el prefijo de 0x518cf0, que no lo toca (World::ModelEffect::flags78).
constexpr std::uint32_t HeldFlag = 0x40;
constexpr std::uint32_t HiddenFlag = 0x4000;
constexpr std::int32_t HeldLife = 10;

// 0x52b640..0x52b782, el /0, el /1 y el /3: tres vueltas; en cada una, el ángulo vida · 0,17f + i · 2π/3 (negado en el
// /3) y el radio FSIN(i · 15,37f + T · 0,0031f) · 20 + 50, guardado en float; el punto es el del dueño corrido en x por
// FSIN(ángulo) · radio y en y por FCOS(ángulo) · radio, a 200 (250 el /3) por su escala sobre su z. Ahí, la partícula
// 0x5de/5 con los ángulos del efecto y escala 0,4f, y el sprite 0x628 de escala 1 con la luz local (1, 1, 1).
Core::Expected<void, std::string> Orbit(World::ModelEffect& effect, const ActorState& owner,
                                        const HighEffectStepContext& context)
{
    const bool reversed = effect.subtype == ReverseOrbitSubtype;
    const glm::vec3 particleLight = reversed ? glm::vec3{Cyan, 1.0f, 1.0f} : glm::vec3(Grey);
    const glm::vec3 localLight(1.0f); // [ebp−0x1c], 0x520921
    const double height = reversed ? ReverseOrbitHeight : OrbitHeight;
    for (int turn = 0; turn < Turns; ++turn)
    {
        const double index = D(static_cast<float>(turn)); // FILD y FSTP a float (0x52b648..0x52b655)
        double angle = static_cast<double>(effect.life) * LifeTurn + index * TurnStep;
        if (reversed)
            angle = -angle;
        const auto wobble = Render::Models::Detail::SineProductPC53(
            index * WobbleStep + D(context.worldClock) * OrbitClock, WobbleDepth);
        if (!wobble)
            return Core::Unexpected(std::string("0x52b68d: el radio del 0x628 no cabe en el perfil PC53"));
        const float radius = F32(*wobble + RadiusBase);
        const auto x = Render::Models::Detail::SineProductPC53(angle, radius);
        const auto y = Render::Models::Detail::CosineProductPC53(angle, radius);
        if (!x || !y)
            return Core::Unexpected(std::string("0x52b6a0: la vuelta del 0x628 no cabe en el perfil PC53"));
        const glm::vec3 point{F32(*x + D(owner.position.x)), F32(*y + D(owner.position.y)),
                              F32(height * D(owner.scale) + D(owner.position.z))};
        if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, OrbitParticle, point,
                                           effect.transform.angles, particleLight, OrbitParticleSubtype,
                                           OrbitParticleScale, 0x52b74e);
            !made.HasValue())
            return Core::Unexpected(made.Error());
        if (auto sprite = CreateHighSprite(context, SkullSprite, point, 1.0f, localLight, 0.0f, 0x52b773);
            !sprite.HasValue())
            return sprite;
    }
    return {};
}

// 0x52b78d..0x52b853, el /5: tres partículas 0x5ac/5 con los ángulos del efecto, la luz (0,6f; 0,6f; 1) y escala 0,7f,
// en SkullStackPoint.
Core::Expected<void, std::string> Stack(World::ModelEffect& effect, const ActorState& owner,
                                        const HighEffectStepContext& context)
{
    const glm::vec3 light{Pale, Pale, 1.0f};
    for (int turn = 0; turn < Turns; ++turn)
    {
        const auto point = SkullStackPoint(turn, effect.life, context.worldClock, owner);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto made =
                CreateHighParticle(context.world, context.random, context.worldClock, StackParticle, point.Value(),
                                   effect.transform.angles, light, StackParticleSubtype, StackParticleScale, 0x52b843);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return {};
}
} // namespace

Core::Expected<glm::vec3, std::string> SkullStackPoint(int turn, std::int32_t life, float worldClock,
                                                       const ActorState& owner)
{
    const double angle =
        static_cast<double>(turn) * TurnStep + static_cast<double>(life) * LifeTurn + D(worldClock) * StackClock;
    const auto x = Render::Models::Detail::SineProductPC53(angle, StackRadius);
    const auto y = Render::Models::Detail::CosineProductPC53(angle, StackRadius);
    if (!x || !y)
        return Core::Unexpected(std::string("0x52b7ce: la vuelta del 0x628/5 no cabe en el perfil PC53"));
    return glm::vec3{F32(*x + D(owner.position.x)), F32(*y + D(owner.position.y)),
                     F32(static_cast<double>(turn + 1) * D(owner.scale) * StackSpacing + D(owner.position.z))};
}

void BeginTheSkull(World::ModelEffect& effect)
{
    constexpr std::int32_t Life = 1000, OrbitLife = 940, GlowLife = 8, FallingLife = 20;
    constexpr double GlowRise = 150.0;                                // 0x6b87ac
    constexpr float FallingSpeed = std::bit_cast<float>(0x40000000u); // 0x51ee6f, 2
    constexpr float FallingPush = std::bit_cast<float>(0xc1200000u);  // 0x51ee7f, −10
    effect.life = Life;
    switch (effect.subtype)
    {
    case OrbitSubtype:
        effect.life = OrbitLife;
        break;
    case GlowSubtype:
        effect.life = GlowLife;
        effect.transform.position.z = F32(D(effect.transform.position.z) + GlowRise);
        break;
    case FallingSubtype:
        // 0x51ee79..0x51ee89: la x y la z de la velocidad, del ESI en 0 del prólogo.
        effect.life = FallingLife;
        effect.speed = FallingSpeed;
        effect.velocity = {0.0f, FallingPush, 0.0f};
        break;
    default:
        break;
    }
}

Core::Expected<void, std::string> StepTheSkull(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.subtype == FallingSubtype)
    {
        // 0x52b5a3..0x52b5dc.
        constexpr float FallingSpriteScale = 10.0f; // 0x52b5ce
        effect.velocity.y = F32(D(effect.velocity.y) - D(effect.speed));
        effect.transform.angles.x = F32(D(effect.transform.angles.x) - 1.0);
        return CreateHighSprite(context, SkullSprite, effect.transform.position, FallingSpriteScale, effect.light, 0.0f,
                                0x52b5dc);
    }
    const auto* owner = static_cast<const ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::format("0x52b5ef: el 0x628/{} lee el byte vivo de un dueño nulo", effect.subtype));
    if (!owner->alive)
    {
        effect.life = 0;
        effect.alive = false;
        return {};
    }
    switch (effect.subtype)
    {
    case PlainSubtype:
        if ((effect.flags78 & HeldFlag) == HeldFlag)
        {
            effect.life = HeldLife;
            if ((effect.flags78 & HiddenFlag) == HiddenFlag)
            {
                effect.seen = false;
                return {};
            }
        }
        return Orbit(effect, *owner, context);
    case OrbitSubtype:
    case ReverseOrbitSubtype:
        return Orbit(effect, *owner, context);
    case GlowSubtype:
    {
        // 0x52b85e..0x52b899: la luz en vida · 0,1f, un FST por canal.
        constexpr float GlowScale = std::bit_cast<float>(0x3fc00000u); // 0x52b88e, 1,5f
        const float glow = F32(static_cast<double>(effect.life) * Tenth);
        return CreateHighSprite(context, SkullSprite, effect.transform.position, GlowScale, glm::vec3(glow), 0.0f,
                                0x52b899);
    }
    case StackSubtype:
        return Stack(effect, *owner, context);
    default:
        // 0x52b603: los de más de 5 (sin signo) van a la cola.
        return {};
    }
}

Core::Expected<void, std::string> PutTheSkullOn(ActorState& actor, WorldState& world, Render::Models::CrtRandom& random,
                                                std::uint32_t address)
{
    static_cast<void>(KillModelEffectsOf(world.Map().effects, SkullEffect, &actor, PlainSubtype));
    const auto made = CreateHighModelEffect({.type = SkullEffect,
                                             .position = actor.position,
                                             .angles = actor.angles,
                                             .light = glm::vec3(1.0f),
                                             .subtype = PlainSubtype,
                                             .owner = &actor},
                                            {.world = world, .random = random});
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    return {};
}
} // namespace Client::Scenes
