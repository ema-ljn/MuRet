#include "Scenes/ActorDying.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

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
// 0x4fba95..0x4fbad6: el uno (0x6b87d4), el alpha más bajo que sigue (0x6b88d4) y lo que se hunde (0x6b87b4).
constexpr float FadeStart = 1.0f;
constexpr float LowestAlpha = 0.01f;
constexpr float SinkStep = 0.4f;
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
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
} // namespace

void FlyFromTheCastle(ActorState& actor)
{
    const auto matrix = Render::Models::OriginalAngleMatrix(actor.push);
    const glm::vec3 turned = Render::Models::OriginalVectorRotate({0.0f, actor.floatD0, 0.0f}, matrix);
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
    const auto matrix = Render::Models::OriginalAngleMatrix(angles);
    const glm::vec3 moved = Render::Models::OriginalVectorRotate({actor.floatCC, actor.floatD0, actor.floatD4}, matrix);
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
    if (actor.type == SlowDyingType)
        return Core::Unexpected(std::format("0x4fba6d: el 0x192 del lugar {} se muere más lento con +0x60 del objeto "
                                            "en 100 o más, que MuRet no tiene (hito 2)",
                                            slot));
    const float step = actor.type == FastDyingType && context.map == FastDyingMap ? FastDyingStep : DyingStep;
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
        }
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
            return Core::Unexpected(std::format("0x4fbba8: las chispas del que se muere en Chaos Castle (tres rand() "
                                                "y 0x51fe10, con veinte 0x5c5 y un 0x5ed) del lugar {}, paso {}, son "
                                                "de la CL11",
                                                slot, dying));
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
            return Core::Unexpected(std::format("0x4fbcdd: las diez partículas 0x5ac del que se muere por la skill "
                                                "0x28 (rand() % 32 por el hueso, 0x4ec110, y rand() % 100 por la "
                                                "escala) del lugar {}, paso {}, son de la CL11",
                                                slot, actor.dying));
    }
    if (context.scene == SceneId::Game && map == BubbleMap)
        return BlowTheBubbles(actor, slot, context);
    return {};
}
} // namespace Client::Scenes
