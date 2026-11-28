#include "Combat/CombatReplies.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Maps/MapId.h"
#include "Protocol/Combat/Combat.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatReceive.h"
#include "Combat/DamageNumbers.h"
#include "Events/EventMapObject.h"
#include "Chat/GameMessages.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Crt/SprintfFormat.h"
#include "Math/Ftol.h"
#include "Audio/SoundId.h"
#include "Tables/SkillId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Combat
{
namespace
{
// 0x4cf9a9..0x4cf9c3
const glm::vec3 KillDamageLight{1.0f, 0.6f, 0.0f};
constexpr float KillDamageScale = 15.0f;
// 0x76e0b64: Text.bmd's text 486 (0x76bd1dc + 486 · 300), the experience format.
constexpr std::size_t ExperienceText = 486;
constexpr std::uint8_t KillerOnRecord = 2;
// The 0x17's pushes (0x4d0ab3, 0x4d0b41).
constexpr std::uint8_t FirstPushSkill = Tables::NovaSkill;
constexpr std::uint8_t SecondPushSkill = 0x3b;
// 0x4d0a5f..0x4d0aa2: 0x4d07c0 runs in Blood Castle (11 to 17) and Chaos Castle (18 to 23), two adjacent ranges.
using Maps::FirstBloodCastleMap;
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
// 0x4d07c0's TerrainWall: 8 in Chaos Castle (it falls) and 0x20 (it is pushed); bit 8 of the neighbor cell decides
// which way.
constexpr std::uint16_t FallWall = 0x08;
constexpr std::uint16_t PushWall = 0x20;
constexpr Audio::SoundId ChaosCastleFallSound{0x60};
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

// FILD r; FADD float c; FSTP float: the sum in the x87, stored as float.
float Plus(std::int32_t drawn, float constant)
{
    return static_cast<float>(static_cast<double>(drawn) + static_cast<double>(constant));
}

// 0x4cfc93..0x4cfcb7: sprintf(text 486, experience) and 0x555190(0x576798c, text, 1); the 0x9C (0x4d0775..0x4d0799),
// the same with 0x5767990.
Core::Expected<void, std::string> AddTheExperienceMessage(std::uint32_t amount, const Chat::ChatReceiveContext& chat)
{
    const std::optional<std::string_view> format = Ui::TextAt(chat.texts, ExperienceText);
    if (!format)
        return Core::Unexpected(std::string("0x4cfc9e: el texto 486 de Text.bmd no está"));
    if (chat.measure == nullptr)
        return Core::Unexpected(std::string("0x555190: no hay con qué medir el mensaje de la experiencia"));
    const std::string text = Crt::Sprintf(CString(*format), amount);
    // 0x576798c and 0x5767990, in .bss: nothing MuRet ports writes a name to them (*inferred*, P-C252 d).
    return Chat::AddGameMessage(chat.world.Runtime(), *chat.measure, chat.screen, std::string_view{}, text,
                                Ui::SystemMessage);
}

// 0x4d08ea..0x4d0a06: pushed off the bridge, toward the side of the neighbor cell with bit 8: the right one (0x4d0949)
// or, without the bit there, the left one (0x4d0979); with neither, the push stays as it was. At the grid's edge (cell
// 0xffff, or 0 without the bit at 1) the original reads the memory next to TerrainWall (0x7d0d348): MuRet draws the
// three rand() before that read and discards the push (+0x1a1 back at 0), without cutting the handler (07, Q-C1180).
void PushOffTheBridge(Kernel::ActorState& actor, std::size_t cell,
                      const Data::Terrain::TerrainLayer<std::uint16_t>& cells, Crt::CrtRandom& random)
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
    // 0x4d09c2..0x4d09d4: NEG and the signed remainder of 2: 0 or −1.
    const std::int32_t side = (-random.Next()) % 2;
    actor.floatD4 = 5.0f;
    actor.pushOrigin = actor.position;
    actor.floatD0 = Plus(side, 13.0f);
}

// 0x4d07c0 with both arguments on the same character.
Core::Expected<void, std::string> DieInTheCastle(Kernel::ActorState& actor, const ActorCombatContext& combat)
{
    Kernel::WorldRuntime& runtime = combat.world.Runtime();
    const auto& attributes = Terrain::TerrainOrEmpty(combat.terrain).attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4d07ff: faltan los atributos del mundo cargado"));
    // 0x4d07ca..0x4d07ff: the cell from ftol(x · 0.01f) and ftol(y · 0.01f), one byte each.
    constexpr double CellScale = static_cast<double>(0.01f);
    const auto x = Math::OriginalFtolLow(static_cast<double>(actor.position.x) * CellScale);
    const auto y = Math::OriginalFtolLow(static_cast<double>(actor.position.y) * CellScale);
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
        // 0x4d0848..0x4d08dc: falls into Chaos Castle's void.
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
        runtime.RequestSound(ChaosCastleFallSound);
    }
    else if ((wall & PushWall) == PushWall)
    {
        PushOffTheBridge(actor, cell, attributes->cells, random);
    }
    // 0x4d0a0c..0x4d0a25: the hero with the event object returns it to 0x40ba20.
    if (&actor == &combat.world.Hero() && runtime.eventObject)
    {
        runtime.eventObject->field10 = 0;
        runtime.eventObject->field14 = {-1, -1, -1, -1};
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> ApplyKillExperience(const Protocol::Combat::KillExperience& reply,
                                                      const ActorCombatContext& combat,
                                                      const Chat::ChatReceiveContext& chat)
{
    Kernel::WorldState& world = combat.world;
    const std::size_t index = Kernel::ActorWithKey(world, reply.key);
    Kernel::ActorState& dead = world.Actors()[index];
    if (reply.shock)
    {
        if (auto died = SetActorDie(dead, combat); !died.HasValue())
            return died;
    }
    else
    {
        Kernel::ActorState& hero = world.Hero();
        hero.byte380 = KillerOnRecord;
        hero.word382 = reply.damage;
        hero.attackTarget = static_cast<std::uint16_t>(index);
    }
    CreateDamageNumber(world, dead.position, reply.damage, KillDamageLight, KillDamageScale, true);
    dead.dying = 1;
    dead.movement = 0;
    {
        const Models::LogicalAccess own(world.Runtime().ownRecordDepth, combat.random, Models::OwnRecordCloseRandoms);
        world.OwnCharacter().experience += reply.amount;
    }
    if (reply.amount == 0)
        return {};
    return AddTheExperienceMessage(reply.amount, chat);
}

Core::Expected<void, std::string> ApplyDeath(const Protocol::Combat::Death& reply, const ActorCombatContext& combat)
{
    Kernel::WorldState& world = combat.world;
    Kernel::ActorState& actor = world.Actors()[Kernel::ActorWithKey(world, reply.key)];
    actor.dying = 1;
    actor.movement = 0;
    actor.deathSkill = reply.skill;
    const std::int32_t map = world.Runtime().map;
    if (map >= FirstBloodCastleMap && map <= LastChaosCastleMap)
        return DieInTheCastle(actor, combat);
    actor.byte1a1 = 0;
    if (!IsPushedBySkill(reply.skill))
        return {};
    // 0x4d0ac1..0x4d0b38: the skill's push.
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
    // 0x4d0b49..0x4d0b98: the heading toward the killer (0x4e93f0) goes to +0x24 and +0x30; the head copies +0x1c and
    // +0x20.
    const Kernel::ActorState& killer = world.Actors()[Kernel::ActorWithKey(world, reply.killer)];
    const auto angle =
        static_cast<float>(Math::CreateAngle(actor.position.x, actor.position.y, killer.position.x, killer.position.y));
    actor.angles.z = angle;
    actor.angle30 = angle;
    actor.headTurn.angles.x = actor.angles.x;
    actor.headTurn.angles.y = actor.angles.y;
    return {};
}
} // namespace Client::Combat
