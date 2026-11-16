#include "Scenes/CombatReplies.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Protocol/Combat.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DamageNumbers.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameMessages.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/SprintfFormat.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x4cf9a9..0x4cf9c3: la luz del número del muerto, 1, 0,6f y 0, y su escala, 15.
const glm::vec3 KillDamageLight{1.0f, 0.6f, 0.0f};
constexpr float KillDamageScale = 15.0f;
// 0x76e0b64: el texto 486 de Text.bmd (0x76bd1dc + 486 · 300), el formato de la experiencia.
constexpr std::size_t ExperienceText = 486;
constexpr std::uint8_t KillerOnRecord = 2;
// Los empujes del 0x17: la skill 0x28 y la 0x3b (0x4d0ab3, 0x4d0b41).
constexpr std::uint8_t FirstPushSkill = 0x28;
constexpr std::uint8_t SecondPushSkill = 0x3b;
// 0x4d0a5f..0x4d0aa2: 0x4d07c0 corre en Blood Castle (11 a 17) y en Chaos Castle (18 a 23), dos rangos seguidos.
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// El TerrainWall de 0x4d07c0: 8 en Chaos Castle (cae) y 0x20 (lo empujan); el bit 8 de la celda de al lado decide
// hacia dónde.
constexpr std::uint16_t FallWall = 0x08;
constexpr std::uint16_t PushWall = 0x20;
constexpr std::int32_t ChaosCastleFallSound = 0x60;
constexpr std::size_t TerrainCells = 0x10000;

std::string_view CString(std::string_view text)
{
    const auto end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

bool IsPushedBySkill(std::uint8_t skill)
{
    return skill == FirstPushSkill || skill == SecondPushSkill;
}

// FILD r; FADD float c; FSTP float: la suma en la x87 y guardada en float.
float Plus(std::int32_t drawn, float constant)
{
    return static_cast<float>(static_cast<double>(drawn) + static_cast<double>(constant));
}

// 0x4cfc93..0x4cfcb7: sprintf(texto 486, experiencia) y 0x555190(0x576798c, texto, 1); el 0x9C (0x4d0775..0x4d0799),
// lo mismo con 0x5767990.
Core::Expected<void, std::string> AddTheExperienceMessage(std::uint32_t amount, const ChatReceiveContext& chat)
{
    const std::optional<std::string_view> format = UI::TextAt(chat.texts, ExperienceText);
    if (!format)
        return Core::Unexpected(std::string("0x4cfc9e: el texto 486 de Text.bmd no está"));
    if (chat.measure == nullptr)
        return Core::Unexpected(std::string("0x555190: no hay con qué medir el mensaje de la experiencia"));
    const std::string text = UI::Sprintf(CString(*format), amount);
    // 0x576798c y 0x5767990, en la .bss: nadie les escribe un nombre en lo que MuRet porta (*inferido*, P-C252 d).
    return AddGameMessage(chat.world.Runtime(), *chat.measure, chat.screen, std::string_view{}, text,
                          UI::SystemMessage);
}

// 0x4d08ea..0x4d0a06: lo empujan del puente, hacia el lado de la celda vecina con el bit 8: la de la derecha
// (0x4d0949) o, sin el bit ahí, la de la izquierda (0x4d0979); sin ninguna, el empujón queda como estaba. En el borde
// de la grilla (la celda 0xffff, o la 0 sin el bit en la 1) el original lee la memoria de al lado de TerrainWall
// (0x7d0d348): MuRet saca los tres rand() de antes de esa lectura y descarta el empujón (+0x1a1 de nuevo en 0), sin
// cortar el handler (07, Q-C1180).
void PushOffTheBridge(ActorState& actor, std::size_t cell, const Data::Terrain::TerrainLayer<std::uint16_t>& cells,
                      Render::Models::CrtRandom& random)
{
    actor.byte1a1 = 1;
    actor.byte398 = 0;
    actor.floatE4 = Plus(random.Next() % 10, 10.0f);
    actor.floatD8 = Plus(random.Next() % 20, 20.0f);
    const float push = Plus(random.Next() % 10, 85.0f);
    const auto falls = [&cells](std::size_t index) { return (cells[index] & FallWall) == FallWall; };
    if (cell + 1 >= TerrainCells)
    {
        actor.byte1a1 = 0;
        return;
    }
    if (falls(cell + 1))
    {
        actor.push = {0.0f, 0.0f, -push};
    }
    else
    {
        if (cell == 0)
        {
            actor.byte1a1 = 0;
            return;
        }
        if (falls(cell - 1))
            actor.push = {0.0f, 0.0f, push};
    }
    actor.angles.z = actor.push.z;
    actor.floatCC = Plus(random.Next() % 6, 8.0f);
    // 0x4d09c2..0x4d09d4: NEG y el resto con signo de 2: 0 o −1.
    const std::int32_t side = (-random.Next()) % 2;
    actor.floatD4 = 5.0f;
    actor.pushOrigin = actor.position;
    actor.floatD0 = Plus(side, 13.0f);
}

// 0x4d07c0 con los dos argumentos en el mismo personaje.
Core::Expected<void, std::string> DieInTheCastle(ActorState& actor, const ActorCombatContext& combat)
{
    WorldRuntime& runtime = combat.world.Runtime();
    const auto& attributes = combat.world.Map().terrain.attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4d07ff: faltan los atributos del mundo cargado"));
    // 0x4d07ca..0x4d07ff: la celda de ftol(x · 0,01f) y ftol(y · 0,01f), de a un byte.
    constexpr double CellScale = static_cast<double>(0.01f);
    const auto x = World::OriginalFtolLow(static_cast<double>(actor.position.x) * CellScale);
    const auto y = World::OriginalFtolLow(static_cast<double>(actor.position.y) * CellScale);
    if (!x || !y)
        return Core::Unexpected(std::string("0x4d07d3: la celda del que muere fuera del perfil ftol"));
    const std::size_t cell =
        Data::Terrain::CellIndex(static_cast<std::uint8_t>(*x & 0xff), static_cast<std::uint8_t>(*y & 0xff));
    const std::uint16_t wall = attributes->cells[cell];
    actor.byte1a1 = 0;
    const std::int32_t map = runtime.map;
    auto& random = combat.random;
    if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap && (wall & FallWall) == FallWall)
    {
        // 0x4d0848..0x4d08dc: cae al vacío de Chaos Castle.
        actor.byte1a1 = 1;
        actor.byte398 = 0;
        actor.floatE4 = Plus(random.Next() % 10, 10.0f);
        const std::int32_t drawn = random.Next() % 3;
        actor.floatD4 = 1.0f;
        actor.floatCC = 0.0f;
        actor.floatD0 = 0.0f;
        actor.pushOrigin = actor.position;
        actor.floatD8 = static_cast<float>(static_cast<double>(actor.floatD8) -
                                           (static_cast<double>(drawn) + static_cast<double>(3.0f)));
        runtime.lastSoundId = ChaosCastleFallSound;
    }
    else if ((wall & PushWall) == PushWall)
    {
        PushOffTheBridge(actor, cell, attributes->cells, random);
    }
    // 0x4d0a0c..0x4d0a25: el héroe con el objeto del evento lo vuelve a 0x40ba20.
    if (&actor == &combat.world.Actors()[runtime.hero] && runtime.eventObject)
    {
        runtime.eventObject->field10 = 0;
        runtime.eventObject->field14 = {-1, -1, -1, -1};
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> ApplyKillExperience(const Protocol::Combat::KillExperience& reply,
                                                      const ActorCombatContext& combat, const ChatReceiveContext& chat)
{
    WorldState& world = combat.world;
    const std::size_t index = ActorWithKey(world, reply.key);
    ActorState& dead = world.Actors()[index];
    if (reply.shock)
    {
        if (auto died = SetActorDie(dead, combat); !died.HasValue())
            return died;
    }
    else
    {
        ActorState& hero = world.Actors()[world.Runtime().hero];
        hero.byte380 = KillerOnRecord;
        hero.word382 = reply.damage;
        hero.attackTarget = static_cast<std::uint16_t>(index);
    }
    CreateDamageNumber(world, dead.position, reply.damage, KillDamageLight, KillDamageScale, true);
    dead.dying = 1;
    dead.movement = 0;
    {
        const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, combat.random,
                                                Render::Models::OwnRecordCloseRandoms);
        world.OwnCharacter().experience += reply.amount;
    }
    if (reply.amount == 0)
        return {};
    return AddTheExperienceMessage(reply.amount, chat);
}

Core::Expected<void, std::string> ApplyDeath(const Protocol::Combat::Death& reply, const ActorCombatContext& combat)
{
    WorldState& world = combat.world;
    ActorState& actor = world.Actors()[ActorWithKey(world, reply.key)];
    actor.dying = 1;
    actor.movement = 0;
    actor.deathSkill = reply.skill;
    const std::int32_t map = world.Runtime().map;
    if (map >= FirstBloodCastleMap && map <= LastChaosCastleMap)
        return DieInTheCastle(actor, combat);
    actor.byte1a1 = 0;
    if (!IsPushedBySkill(reply.skill))
        return {};
    // 0x4d0ac1..0x4d0b38: el empuje de la skill.
    actor.byte1a1 = 1;
    actor.byte398 = 0;
    const std::int32_t first = combat.random.Next() % 5;
    actor.byte1bc = 0;
    actor.floatCC = 0.0f;
    actor.floatD8 =
        static_cast<float>((static_cast<double>(first) + static_cast<double>(10.0f)) * static_cast<double>(0.1f));
    const std::int32_t second = combat.random.Next() % 15;
    actor.floatD4 = 0.0f;
    actor.pushOrigin = actor.position;
    actor.floatD0 = Plus(second, 40.0f);
    // 0x4d0b49..0x4d0b98: el rumbo hacia el que lo mató (0x4e93f0) va a +0x24 y +0x30; la cabeza copia +0x1c y +0x20.
    const ActorState& killer = world.Actors()[ActorWithKey(world, reply.killer)];
    const auto angle = static_cast<float>(
        Render::Models::CreateAngle(actor.position.x, actor.position.y, killer.position.x, killer.position.y));
    actor.angles.z = angle;
    actor.angle30 = angle;
    actor.headTurn.angles.x = actor.angles.x;
    actor.headTurn.angles.y = actor.angles.y;
    return {};
}
} // namespace Client::Scenes
