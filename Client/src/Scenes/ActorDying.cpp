#include "Scenes/ActorDying.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleBurst.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t FrozenFlag = 0x1000;
constexpr std::uint32_t Flag4000 = 0x4000;
constexpr std::uint32_t LockedFlag = 0x20;
// 0x4fba46..0x4fba89: lo que sube +0x3c8 por paso (0x6b8a48 y 0x6b8b90).
constexpr std::uint16_t FastDyingType = 0x172;
constexpr std::int32_t FastDyingMap = 7;
constexpr float FastDyingStep = 0.05f;
constexpr float DyingStep = 0.02f;
constexpr std::uint16_t SlowDyingType = 0x192;
// 0x4fba6d..0x4fba7b: con +0x60 del objeto en 100 o más (con signo), 0,01 (0x6b88d4); si no, 0,02.
constexpr std::int32_t SlowDyingLevel = 100;
constexpr float SlowDyingStep = 0.01f;
// 0x4fba95..0x4fbad6: el uno (0x6b87d4), el alpha más bajo que sigue (0x6b88d4) y lo que se hunde (0x6b87b4).
constexpr float FadeStart = 1.0f;
constexpr float LowestAlpha = 0.01f;
constexpr float SinkStep = 0.4f;
using Maps::FirstBloodCastleMap;
using Maps::FirstChaosCastleMap;
using Maps::LastBloodCastleMap;
using Maps::LastChaosCastleMap;
constexpr int ChaosOffsetSpan = 160;
constexpr int ChaosOffsetCenter = 80;
// 0x4fbc1e..0x4fbc9b: las skills que empujan y hasta qué paso.
constexpr std::uint8_t PushSkill = 0x28;
constexpr std::uint8_t SecondPushSkill = 0x3b;
constexpr std::uint8_t LastFlightStep = 14;
constexpr std::uint8_t LastSparkStep = 30;
// 0x4fbd45..0x4fbdf5: cuatro burbujas 0x607 en el mapa 7.
constexpr std::int32_t BubbleMap = 7;
constexpr int Bubbles = 4;
constexpr std::int32_t BubbleParticle = 0x607;
// 0x4fb343: el ángulo X que pierde el vuelo del castillo (0x6b87f8).
constexpr float CastleSpin = 5.0f;
constexpr int DeathSkillBoneCount = 32;
constexpr int DeathSkillScaleSpan = 100;
constexpr std::int32_t DeathSkillFlareSubtype = 5;
constexpr std::int32_t DeathSkillSound = 0x40;

float Sum(float a, float b)
{
    return static_cast<float>(static_cast<double>(a) + static_cast<double>(b));
}

// 0x4fbd52..0x4fbdf5: cada burbuja, x e y con rand() & 0x7f − 64 y z con rand() & 0xff, sobre la posición, con los
// ángulos (+0x1c) y la luz propia (+0xf4) del objeto: 0x53fe70(0x607, punto, +0x1c, +0xf4, 0, 1, 0).
Core::Expected<void, std::string> BlowTheBubbles(const ActorState& actor, std::size_t slot,
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

glm::vec3 ChaosCastleBurstPoint(const ActorState& actor, Crt::CrtRandom& random)
{
    const auto offset = [&random]()
    {
        const auto draw = random.Next() % ChaosOffsetSpan;
        return static_cast<float>(draw - ChaosOffsetCenter);
    };
    return {Sum(actor.position.x, offset()), Sum(actor.position.y, offset()), Sum(actor.position.z, offset())};
}

Core::Expected<void, std::string> CreateTheChaosCastleBurst(ActorState& actor, std::size_t slot,
                                                            const ActorDyingContext& context)
{
    const glm::vec3 point = ChaosCastleBurstPoint(actor, context.random);
    if (context.particles == nullptr)
    {
        // 0x51fe4b y 0x51fe6e sortean antes de que 0x53fe70 busque su primer lugar libre.
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

// 0x4fbca1..0x4fbd43: diez veces, el hueso rand() % 32 por 0x4ec110 (Translate 1) con el corrimiento (0, 0, 0) y la
// partícula 0x5ac/5 en ese punto, con los ángulos del objeto (+0x1c), la luz (0,3f; 0,3f; 1) y la escala
// (rand() % 100) · 0,02f + 0,5 (0x6b89e4 y 0x6b882c). Dos rand() por vuelta, también con el pool lleno.
Core::Expected<void, std::string> CreateTheDeathSkillFlares(const ActorState& actor, std::size_t slot,
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
    const SharedModelPlacement placement =
        context.world != nullptr ? context.world->ModelPlacement(actor.type) : SharedModelPlacement{};
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

void FlyFromTheCastle(ActorState& actor)
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

void FlyFromThePush(ActorState& actor)
{
    const glm::vec3 angles{actor.headTurn.angles.x, actor.headTurn.angles.y, actor.angle30};
    const auto matrix = Math::OriginalAngleMatrix(angles);
    const glm::vec3 moved = Math::OriginalVectorRotate({actor.floatCC, actor.floatD0, actor.floatD4}, matrix);
    actor.position.x = Sum(moved.x, actor.position.x);
    actor.position.y = Sum(moved.y, actor.position.y);
    actor.position.z = Sum(moved.z, actor.position.z);
}

Core::Expected<void, std::string> StepActorDying(ActorState& actor, std::size_t slot, const ActorDyingContext& context)
{
    if (actor.dying == 0)
        return {};
    // 0x4fb9fa..0x4fba43: cada bandera se resta si está.
    actor.flags78 &= ~(FrozenFlag | Flag4000 | LockedFlag);
    // 0x4fba46..0x4fba8f.
    float step = DyingStep;
    if (actor.type == FastDyingType && context.map == FastDyingMap)
        step = FastDyingStep;
    else if (actor.type == SlowDyingType && actor.dword60 >= SlowDyingLevel)
        step = SlowDyingStep;
    actor.dyingTime = Sum(actor.dyingTime, step);
    // 0x4fba95..0x4fbb0d: con +0x3c8 en 1 o más (sin orden, no), el alpha es 1 − (+0x3c8 − 1) en la x87, guardado en
    // float pero comparado entero con 0,01: sin bajar de ahí la Z baja 0,4; debajo (o sin orden), el que no es el
    // héroe se borra. Después, 0x4fb1a0(c, o, 0) suelta las telas, que viven en la escena (P-C161).
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
        if (context.world != nullptr && slot < ActorPoolSize)
            context.world->ActorCloths()[slot].ReleaseResources();
    }
    const std::int32_t map = context.map;
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap && actor.byte1a1 != 0)
    {
        FlyFromTheCastle(actor);
    }
    else if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
    {
        // 0x4fbb4a..0x4fbc19: con +0x1a1, el vuelo y el tope 15; sin él, 25. Del tope − 10 al tope, en los pasos
        // impares, tres rand() % 160 alrededor de la posición y las chispas de 0x51fe10.
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
        // 0x4fbc30..0x4fbc84: +0xd0 baja +0xd8, que sube 1; debajo de 0 (sin orden, no), 0.
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
    if (context.scene == SceneId::Game && map == BubbleMap)
        return BlowTheBubbles(actor, slot, context);
    return {};
}
} // namespace Client::Scenes
