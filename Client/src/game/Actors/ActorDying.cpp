#include "Actors/ActorDying.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Actors/ActorCreation.h"
#include "Kernel/ActorState.h"
#include "Scenes/SceneId.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleBurst.h"
#include "World/Particles.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors
{
namespace
{
constexpr std::uint32_t FrozenFlag = 0x1000;
constexpr std::uint32_t Flag4000 = 0x4000;
constexpr std::uint32_t LockedFlag = 0x20;
// 0x4fba46..0x4fba89: how much +0x3c8 rises per step (0x6b8a48 and 0x6b8b90).
constexpr std::uint16_t FastDyingType = 0x172;
constexpr std::int32_t FastDyingMap = 7;
constexpr float FastDyingStep = 0.05f;
constexpr float DyingStep = 0.02f;
constexpr std::uint16_t SlowDyingType = 0x192;
// 0x4fba6d..0x4fba7b: with the object's +0x60 at 100 or more (signed), 0.01 (0x6b88d4); otherwise, 0.02.
constexpr std::int32_t SlowDyingLevel = 100;
constexpr float SlowDyingStep = 0.01f;
// 0x4fba95..0x4fbad6: the one (0x6b87d4), the lowest alpha that goes on (0x6b88d4) and how much it sinks (0x6b87b4).
constexpr float FadeStart = 1.0f;
constexpr float LowestAlpha = 0.01f;
constexpr float SinkStep = 0.4f;
using Maps::FirstBloodCastleMap;
using Maps::FirstChaosCastleMap;
using Maps::LastBloodCastleMap;
using Maps::LastChaosCastleMap;
constexpr int ChaosOffsetSpan = 160;
constexpr int ChaosOffsetCenter = 80;
// 0x4fbc1e..0x4fbc9b: the skills that push and up to which step.
constexpr std::uint8_t PushSkill = 0x28;
constexpr std::uint8_t SecondPushSkill = 0x3b;
constexpr std::uint8_t LastFlightStep = 14;
constexpr std::uint8_t LastSparkStep = 30;
// 0x4fbd45..0x4fbdf5: four 0x607 bubbles on map 7.
constexpr std::int32_t BubbleMap = 7;
constexpr int Bubbles = 4;
constexpr std::int32_t BubbleParticle = 0x607;
// 0x4fb343: the X angle the castle flight loses (0x6b87f8).
constexpr float CastleSpin = 5.0f;
constexpr int DeathSkillBoneCount = 32;
constexpr int DeathSkillScaleSpan = 100;
constexpr std::int32_t DeathSkillFlareSubtype = 5;
constexpr Audio::SoundId DeathSkillSound = Sound::ExplosionSound;

float Sum(float a, float b)
{
    return static_cast<float>(static_cast<double>(a) + static_cast<double>(b));
}

// 0x4fbd52..0x4fbdf5: each bubble, x and y with rand() & 0x7f − 64 and z with rand() & 0xff, over the position, with
// the object's angles (+0x1c) and own light (+0xf4): 0x53fe70(0x607, point, +0x1c, +0xf4, 0, 1, 0).
Core::Expected<void, std::string> BlowTheBubbles(const Kernel::ActorState& actor, std::size_t slot,
                                                 const ActorDyingContext& context)
{
    for (int bubble = 0; bubble < Bubbles; ++bubble)
    {
        const auto x = static_cast<float>((context.random.Next() & 0x7f) - 0x40);
        const auto y = static_cast<float>((context.random.Next() & 0x7f) - 0x40);
        const auto z = static_cast<float>(context.random.Next() & 0xff);
        const glm::vec3 point{Sum(x, actor.position.x), Sum(y, actor.position.y), Sum(z, actor.position.z)};
        if (context.particles == nullptr)
            return Core::Unexpected(std::format("0x4fbdec: falta el pool de partículas de las burbujas del que se "
                                                "muere, lugar {}",
                                                slot));
        const auto created = context.particles->CreateChecked(BubbleParticle, point, actor.angles, actor.light, 0, 1.0f,
                                                              context.worldClock, context.random, 0);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x4fbdec: la burbuja 0x607/0 del que se muere, lugar {}, falla con "
                                                "error {}",
                                                slot, static_cast<unsigned>(created.Error())));
    }
    return {};
}

glm::vec3 ChaosCastleBurstPoint(const Kernel::ActorState& actor, Crt::CrtRandom& random)
{
    const auto offset = [&random]()
    {
        const auto draw = random.Next() % ChaosOffsetSpan;
        return static_cast<float>(draw - ChaosOffsetCenter);
    };
    return {Sum(actor.position.x, offset()), Sum(actor.position.y, offset()), Sum(actor.position.z, offset())};
}

Core::Expected<void, std::string> CreateTheChaosCastleBurst(Kernel::ActorState& actor, std::size_t slot,
                                                            const ActorDyingContext& context)
{
    const glm::vec3 point = ChaosCastleBurstPoint(actor, context.random);
    if (context.particles == nullptr)
    {
        // 0x51fe4b and 0x51fe6e draw before 0x53fe70 looks for its first free slot.
        static_cast<void>(context.random.Next());
        static_cast<void>(context.random.Next());
        return Core::Unexpected(
            std::format("0x4fbba8: falta el pool para las chispas de Chaos Castle del lugar {}", slot));
    }
    const auto burst = World::CreateParticleBurst(*context.particles, point, context.random, true, context.worldClock);
    if (!burst.HasValue())
        return Core::Unexpected(std::format("0x4fbba8: falla el burst de Chaos Castle del lugar {} con error {}", slot,
                                            static_cast<unsigned>(burst.Error())));
    if (burst.Value() && context.soundRequest)
        context.soundRequest(DeathSkillSound);
    return {};
}

// 0x4fbca1..0x4fbd43: ten times, bone rand() % 32 through 0x4ec110 (Translate 1) with the offset (0, 0, 0) and
// particle 0x5ac/5 at that point, with the object's angles (+0x1c), the light (0.3f, 0.3f, 1) and the scale
// (rand() % 100) · 0.02f + 0.5 (0x6b89e4 and 0x6b882c). Two rand() per round, also with the pool full.
Core::Expected<void, std::string> CreateTheDeathSkillFlares(const Kernel::ActorState& actor, std::size_t slot,
                                                            const ActorDyingContext& context)
{
    constexpr int Flares = 10;
    constexpr glm::vec3 FlareLight{std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e99999au), 1.0f};
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    constexpr double ScaleBase = 0.5;
    if (context.particles == nullptr)
        return Core::Unexpected(std::format("0x4fbd3a: falta el pool para la partícula 0x5ac/5 del lugar {}", slot));
    const Math::BoneTransform emptyPose(0.0f);
    const glm::vec3 offset{};
    const Kernel::SharedModelPlacement placement =
        context.world != nullptr ? context.world->ModelPlacement(actor.type) : Kernel::SharedModelPlacement{};
    for (int flare = 0; flare < Flares; ++flare)
    {
        const auto bone = static_cast<std::size_t>(context.random.Next() % DeathSkillBoneCount);
        const Math::BoneTransform& bodyBone = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : emptyPose;
        const glm::vec3 point = Math::OriginalTransformPosition(Math::OriginalMatrixOf(bodyBone), offset,
                                                                placement.scale, placement.origin);
        const int scaleDraw = context.random.Next() % DeathSkillScaleSpan;
        const auto scale = static_cast<float>(static_cast<double>(scaleDraw) * ScaleStep + ScaleBase);
        const auto created =
            context.particles->CreateChecked(World::KalimaFlare, point, actor.angles, FlareLight,
                                             DeathSkillFlareSubtype, scale, context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x4fbd3a: la partícula 0x5ac/5 del lugar {} falla con error {}", slot,
                                                static_cast<unsigned>(created.Error())));
    }
    return {};
}
} // namespace

void FlyFromTheCastle(Kernel::ActorState& actor)
{
    const auto matrix = Math::OriginalAngleMatrix(actor.push);
    const glm::vec3 turned = Math::OriginalVectorRotate({0.0f, actor.floatD0, 0.0f}, matrix);
    actor.floatD0 = Sum(actor.floatCC, actor.floatD0);
    actor.floatE4 = Sum(actor.floatD8, actor.floatE4);
    actor.floatD8 = static_cast<float>(static_cast<double>(actor.floatD8) - static_cast<double>(actor.floatD4));
    actor.angles.x = static_cast<float>(static_cast<double>(actor.angles.x) - static_cast<double>(CastleSpin));
    actor.position.x = Sum(turned.x, actor.pushOrigin.x);
    actor.position.y = Sum(turned.y, actor.pushOrigin.y);
    actor.position.z = Sum(actor.pushOrigin.z, actor.floatE4);
}

void FlyFromThePush(Kernel::ActorState& actor)
{
    const glm::vec3 angles{actor.headTurn.angles.x, actor.headTurn.angles.y, actor.angle30};
    const auto matrix = Math::OriginalAngleMatrix(angles);
    const glm::vec3 moved = Math::OriginalVectorRotate({actor.floatCC, actor.floatD0, actor.floatD4}, matrix);
    actor.position.x = Sum(moved.x, actor.position.x);
    actor.position.y = Sum(moved.y, actor.position.y);
    actor.position.z = Sum(moved.z, actor.position.z);
}

Core::Expected<void, std::string> StepActorDying(Kernel::ActorState& actor, std::size_t slot,
                                                 const ActorDyingContext& context)
{
    if (actor.dying == 0)
        return {};
    // 0x4fb9fa..0x4fba43: each flag is subtracted if it is set.
    actor.flags78 &= ~(FrozenFlag | Flag4000 | LockedFlag);
    // 0x4fba46..0x4fba8f.
    float step = DyingStep;
    if (actor.type == FastDyingType && context.map == FastDyingMap)
        step = FastDyingStep;
    else if (actor.type == SlowDyingType && actor.dword60 >= SlowDyingLevel)
        step = SlowDyingStep;
    actor.dyingTime = Sum(actor.dyingTime, step);
    // 0x4fba95..0x4fbb0d: with +0x3c8 at 1 or more (unordered, no), the alpha is 1 − (+0x3c8 − 1) on the x87, stored
    // as float but compared unrounded with 0.01: while it stays above that the Z drops 0.4; below it (or unordered),
    // anyone but the hero is erased. Then 0x4fb1a0(c, o, 0) frees the cloths, which live in the scene (P-C161).
    if (actor.dyingTime >= FadeStart)
    {
        const double faded =
            static_cast<double>(FadeStart) - (static_cast<double>(actor.dyingTime) - static_cast<double>(FadeStart));
        actor.alpha = static_cast<float>(faded);
        if (faded >= static_cast<double>(LowestAlpha))
        {
            actor.position.z =
                static_cast<float>(static_cast<double>(actor.position.z) - static_cast<double>(SinkStep));
        }
        else if (!context.hero)
        {
            actor.alive = false;
            actor.deathSkill = 0;
            actor.byte1a1 = 0;
            actor.byte1bc = 0;
            if (context.world != nullptr)
                ForgetNamedBones(context.world->Runtime(), slot);
        }
        if (context.world != nullptr && slot < Kernel::ActorPoolSize)
            context.world->ActorCloths()[slot].ReleaseResources();
    }
    const std::int32_t map = context.map;
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap && actor.byte1a1 != 0)
    {
        FlyFromTheCastle(actor);
    }
    else if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
    {
        // 0x4fbb4a..0x4fbc19: with +0x1a1, the flight and the cap 15; without it, 25. From cap − 10 to the cap, on
        // odd steps, three rand() % 160 around the position and the sparks of 0x51fe10.
        std::uint8_t last = 25;
        if (actor.byte1a1 != 0)
        {
            FlyFromTheCastle(actor);
            last = 15;
        }
        const std::uint8_t dying = actor.dying;
        if (dying <= last && dying >= last - 10 && (dying & 1) != 0)
        {
            const auto burst = CreateTheChaosCastleBurst(actor, slot, context);
            if (!burst.HasValue())
                return Core::Unexpected(std::format("{} (paso {})", burst.Error(), dying));
        }
    }
    else if (actor.deathSkill == PushSkill || actor.deathSkill == SecondPushSkill)
    {
        // 0x4fbc30..0x4fbc84: +0xd0 drops by +0xd8, which rises by 1; below 0 (unordered, no), 0.
        if (actor.dying <= LastFlightStep)
        {
            actor.floatD0 = static_cast<float>(static_cast<double>(actor.floatD0) - static_cast<double>(actor.floatD8));
            actor.floatD8 = Sum(actor.floatD8, 1.0f);
            if (!(actor.floatD0 >= 0.0f))
                actor.floatD0 = 0.0f;
            FlyFromThePush(actor);
        }
        if (actor.dying <= LastSparkStep && actor.deathSkill == PushSkill)
        {
            if (auto flare = CreateTheDeathSkillFlares(actor, slot, context); !flare.HasValue())
                return Core::Unexpected(std::format("{} (paso {})", flare.Error(), actor.dying));
        }
    }
    if (context.scene == Scenes::SceneId::Game && map == BubbleMap)
        return BlowTheBubbles(actor, slot, context);
    return {};
}
} // namespace Client::Actors
