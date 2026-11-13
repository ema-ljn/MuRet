#include "Scenes/SkillHits.h"

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorName.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// 0x517a20, cotejada con el PE c32bbd5f (06, "La flecha", el choque de las flechas de skill).
namespace Client::Scenes
{
namespace
{
constexpr std::int32_t CastleSiegeMap = 30;
constexpr double CellsPerUnit = static_cast<double>(0.01f); // 0x6b86c0
constexpr std::uint8_t MonsterKind = 2;
constexpr std::uint8_t PlayerKind = 1;
constexpr std::string_view GameMasterName = "webzen"; // 0x6c8c40

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// 0x517bc3..0x517bda: con la skill 8 o 9, el blanco con +0x394 en 0 queda con +0x398 en 10.
void MarkTheStunned(ActorState& actor, std::uint8_t skill)
{
    constexpr std::uint8_t FirstMarkingSkill = 8;
    constexpr std::uint8_t SecondMarkingSkill = 9;
    constexpr std::uint8_t Mark = 10;
    if ((skill == FirstMarkingSkill || skill == SecondMarkingSkill) && actor.dword394 == 0)
        actor.byte398 = Mark;
}

// 0x517b44..0x517c0b: hasta cinco claves, de los 400 en orden.
Protocol::Skill::SkillHitsSend CollectTheTargets(const SkillHitsRequest& request, WorldState& world, std::uint8_t skill)
{
    Protocol::Skill::SkillHitsSend hits{};
    const auto actors = world.Actors();
    const ActorState* hero = &actors[world.Runtime().hero];
    for (ActorState& actor : actors)
    {
        // 0x517b54..0x517b73: la distancia se calcula antes de mirar nada.
        const double dx = D(request.point.x) - D(actor.position.x);
        const double dy = D(request.point.y) - D(actor.position.y);
        const float distance = F32(std::sqrt((dy * dy) + (dx * dx)));
        if (!actor.alive || !actor.seen || &actor == hero || actor.dying != 0)
            continue;
        if (distance > request.radius)
            continue;
        const bool chosen = actor.byte88 == PlayerKind && actor.key == request.key;
        if (actor.byte88 != MonsterKind && !chosen)
            continue;
        MarkTheStunned(actor, skill);
        hits.keys[hits.hits] = actor.key;
        ++hits.hits;
        if (hits.hits >= Protocol::Skill::MaxSkillHits)
            break;
    }
    return hits;
}
} // namespace

Core::Expected<bool, std::string> HitTheTargetsAround(const SkillHitsRequest& request, WorldState& world,
                                                      Render::Models::CrtRandom& random,
                                                      const std::function<void(Protocol::Outgoing)>& send)
{
    WorldRuntime& runtime = world.Runtime();
    // 0x517a44..0x517af3.
    std::uint8_t skill = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        skill = OwnSkillByte(world.OwnCharacter(), request.skillIndex);
    }
    // 0x517af8..0x517b3f: en el 30 con el asedio (0x4294b0), el bit 0x100 de la celda del punto (0x5f82c0) sale con 0;
    // sin él, un jugador entra aunque no tenga la clave.
    if (runtime.map == CastleSiegeMap && runtime.siegeActive)
    {
        return Core::Unexpected(std::string("0x517b1e: los blancos de la skill en el asedio (el atributo de 0x5f82c0), "
                                            "sin portar (CL9.5)"));
    }
    Protocol::Skill::SkillHitsSend hits = CollectTheTargets(request, world, skill);
    if (hits.hits == 0)
        return false;
    // 0x517c11..0x517c72.
    if (ActorNameContains(world.Actors()[runtime.hero], GameMasterName))
        return true;
    // 0x517c74..0x5184c7: el C1 10, con la celda del punto (0x517deb y 0x517eed).
    const auto x = World::OriginalFtolLow(D(request.point.x) * CellsPerUnit);
    const auto y = World::OriginalFtolLow(D(request.point.y) * CellsPerUnit);
    if (!x || !y)
        return Core::Unexpected(
            std::string("0x517deb: el punto de la skill por 0,01 no entra en el entero de 0x69fd3c"));
    hits.skill = skill;
    hits.x = static_cast<std::uint8_t>(*x & 0xff);
    hits.y = static_cast<std::uint8_t>(*y & 0xff);
    hits.volley = request.volley;
    hits.extra = request.extra;
    // 0x518515..0x518606: el relleno y el serial del C3.
    random.Discard(2);
    if (send)
        send(Protocol::Skill::WriteSkillHits(hits));
    return true;
}
} // namespace Client::Scenes
