#include "Effects/SkullEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

// Checked against the PE c32bbd5f: 0x51ee23..0x51ee95 (the creation) and 0x52b59b..0x52b8a1 (the step, with 0x52d18c's
// table).
namespace Client::Effects
{
namespace
{
using Math::D;
using Math::F32;

constexpr std::int32_t PlainSubtype = 0;        // that of flag 0x40 (0x4e688c, 0x4cd664)
constexpr std::int32_t OrbitSubtype = 1;        // turns like the /0
constexpr std::int32_t GlowSubtype = 2;         // a sprite that fades with the life
constexpr std::int32_t ReverseOrbitSubtype = 3; // turns in reverse, higher and with another light
constexpr std::int32_t FallingSubtype = 4;      // falls without looking at the owner
constexpr std::int32_t StackSubtype = 5;        // that of flag 0x1000 (0x60c6d0)

constexpr std::uint16_t SkullSprite = 0x628; // Skill\Skull.jpg (0x61db86)
constexpr std::int32_t OrbitParticle = 0x5de;
constexpr std::int32_t StackParticle = 0x5ac;
constexpr std::int32_t OrbitParticleSubtype = 5;
constexpr std::int32_t StackParticleSubtype = 5;
constexpr float OrbitParticleScale = std::bit_cast<float>(0x3ecccccdu); // 0x52b6dd, 0.4f
constexpr float StackParticleScale = std::bit_cast<float>(0x3f333333u); // 0x52b7af, 0.7f
constexpr int Turns = 3;

constexpr double TurnStep = static_cast<double>(std::bit_cast<float>(0x40060a92u));   // 0x6ba214, 2π/3
constexpr double LifeTurn = static_cast<double>(std::bit_cast<float>(0x3e2e147bu));   // 0x6ba218, 0.17f
constexpr double WobbleStep = static_cast<double>(std::bit_cast<float>(0x4175eb85u)); // 0x6ba210, 15.37f
constexpr double OrbitClock = static_cast<double>(std::bit_cast<float>(0x3b4b295fu)); // 0x6ba20c, 0.0031f
constexpr double StackClock = static_cast<double>(std::bit_cast<float>(0x3b449ba6u)); // 0x6b89cc, 0.003f
constexpr float WobbleDepth = 20.0f;                                                  // 0x6b86d0
constexpr double RadiusBase = 50.0;                                                   // 0x6b86b0
constexpr float StackRadius = 50.0f;                                                  // 0x6b86b0
constexpr double StackSpacing = 50.0;                                                 // 0x6b86b0
constexpr double OrbitHeight = 200.0;                                                 // 0x6b8928
constexpr double ReverseOrbitHeight = 250.0;                                          // 0x6b8920
constexpr double Tenth = Math::TenthAsDouble;                                         // 0x6b87b8, 0.1f

// 0x52b6ea..0x52b71c: the light of the particle 0x5de, (0.65f, 0.65f, 0.65f) or, the /3, (0.1f, 1, 1).
constexpr float Grey = std::bit_cast<float>(0x3f266666u);
constexpr float Cyan = std::bit_cast<float>(0x3dcccccdu);
// 0x52b817..0x52b839: that of the 0x5ac, (0.6f, 0.6f, 1).
constexpr float Pale = std::bit_cast<float>(0x3f19999au);

// 0x52b612..0x52b63b: the bits the /0 checks in its own +0x78 (not the owner's). MuRet does not write that field on any
// effect, like 0x518cf0's prefix, which does not touch it (Effects::ModelEffect::flags78).
constexpr std::uint32_t HeldFlag = 0x40;
constexpr std::uint32_t HiddenFlag = 0x4000;
constexpr std::int32_t HeldLife = 10;

// 0x52b640..0x52b782, the /0, the /1 and the /3: three rounds; in each, the angle life · 0.17f + i · 2π/3 (negated in
// the /3) and the radius FSIN(i · 15.37f + T · 0.0031f) · 20 + 50, stored in float; the point is the owner's shifted in
// x by FSIN(angle) · radius and in y by FCOS(angle) · radius, at 200 (250 the /3) times its scale above its z. There,
// the particle 0x5de/5 with the effect's angles and scale 0.4f, and the sprite 0x628 of scale 1 with the local light
// (1, 1, 1).
Core::Expected<void, std::string> Orbit(Effects::ModelEffect& effect, const Kernel::ActorState& owner,
                                        const HighEffectStepContext& context)
{
    const bool reversed = effect.subtype == ReverseOrbitSubtype;
    const glm::vec3 particleLight = reversed ? glm::vec3{Cyan, 1.0f, 1.0f} : glm::vec3(Grey);
    const glm::vec3 localLight(1.0f); // [ebp−0x1c], 0x520921
    const double height = reversed ? ReverseOrbitHeight : OrbitHeight;
    for (int turn = 0; turn < Turns; ++turn)
    {
        const double index = D(static_cast<float>(turn)); // FILD and FSTP to float (0x52b648..0x52b655)
        double angle = static_cast<double>(effect.life) * LifeTurn + index * TurnStep;
        if (reversed)
            angle = -angle;
        const auto wobble =
            Math::Detail::SineProductPC53(index * WobbleStep + D(context.worldClock) * OrbitClock, WobbleDepth);
        if (!wobble)
            return Core::Unexpected(std::string("0x52b68d: el radio del 0x628 no cabe en el perfil PC53"));
        const float radius = F32(*wobble + RadiusBase);
        const auto x = Math::Detail::SineProductPC53(angle, radius);
        const auto y = Math::Detail::CosineProductPC53(angle, radius);
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

// 0x52b78d..0x52b853, the /5: three particles 0x5ac/5 with the effect's angles, the light (0.6f, 0.6f, 1) and scale
// 0.7f, at SkullStackPoint.
Core::Expected<void, std::string> Stack(Effects::ModelEffect& effect, const Kernel::ActorState& owner,
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
                                                       const Kernel::ActorState& owner)
{
    const double angle =
        static_cast<double>(turn) * TurnStep + static_cast<double>(life) * LifeTurn + D(worldClock) * StackClock;
    const auto x = Math::Detail::SineProductPC53(angle, StackRadius);
    const auto y = Math::Detail::CosineProductPC53(angle, StackRadius);
    if (!x || !y)
        return Core::Unexpected(std::string("0x52b7ce: la vuelta del 0x628/5 no cabe en el perfil PC53"));
    return glm::vec3{F32(*x + D(owner.position.x)), F32(*y + D(owner.position.y)),
                     F32(static_cast<double>(turn + 1) * D(owner.scale) * StackSpacing + D(owner.position.z))};
}

void BeginTheSkull(Effects::ModelEffect& effect)
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
        // 0x51ee79..0x51ee89: the speed's x and z, from the prologue's ESI at 0.
        effect.life = FallingLife;
        effect.speed = FallingSpeed;
        effect.velocity = {0.0f, FallingPush, 0.0f};
        break;
    default:
        break;
    }
}

Core::Expected<void, std::string> StepTheSkull(Effects::ModelEffect& effect, const HighEffectStepContext& context)
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
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
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
        // 0x52b85e..0x52b899: the light at life · 0.1f, one FST per channel.
        constexpr float GlowScale = std::bit_cast<float>(0x3fc00000u); // 0x52b88e, 1.5f
        const float glow = F32(static_cast<double>(effect.life) * Tenth);
        return CreateHighSprite(context, SkullSprite, effect.transform.position, GlowScale, glm::vec3(glow), 0.0f,
                                0x52b899);
    }
    case StackSubtype:
        return Stack(effect, *owner, context);
    default:
        // 0x52b603: those above 5 (unsigned) go to the tail.
        return {};
    }
}

Core::Expected<void, std::string> PutTheSkullOn(Kernel::ActorState& actor, Kernel::WorldState& world,
                                                Crt::CrtRandom& random, std::uint32_t address)
{
    static_cast<void>(Effects::KillModelEffectsOf(world.Map().effects, SkullEffect, &actor, PlainSubtype));
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
} // namespace Client::Effects
