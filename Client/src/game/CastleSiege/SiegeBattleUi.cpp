#include "CastleSiege/SiegeBattleUi.h"
#include "Kernel/ActorSearch.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Maps/MapId.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Outgoing.h"
#include "Protocol/CastleSiege/SiegeControls.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Hud/GamePanels.h"
#include "Movement/GameMovementState.h"
#include "Hud/GameUi.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "CastleSiege/SiegeBattleUiState.h"
#include "CastleSiege/SiegePanelState.h"
#include "Tables/SkillId.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::CastleSiege
{
namespace
{
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
constexpr std::uint32_t OpenGateFlag = 0x20000;
constexpr std::uint8_t Master = 0x80;
constexpr std::uint8_t Assistant = 0x40;
constexpr std::uint8_t BattleMaster = 0x20;
constexpr std::uint8_t NoGuild = 0xff;
constexpr std::size_t MemberReservation = 1600;
using Tables::FirstSiegeSkill;
using Tables::LastSiegeSkill;

bool HasBattleRole(const Kernel::WorldRuntime& runtime, const Kernel::ActorState& hero, bool guildRequired)
{
    const bool role = hero.byte360 == 6 || hero.byte360 == 7 || hero.byte360 == 8 || hero.byte360 == 12;
    return runtime.castleSiege.siegeActive && role && hero.guildStatus != 0 &&
           (!guildRequired || static_cast<std::int16_t>(hero.word1f2) >= 0);
}

std::int32_t SkillIndex(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint32_t skill)
{
    const Models::LogicalAccess access(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    constexpr std::size_t OwnSkillSlots = 64;
    for (std::size_t index = 0; index < OwnSkillSlots; ++index)
    {
        if (world.OwnCharacter().SkillAt(index) == skill)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}

bool SkillForPost(std::uint32_t skill, const Kernel::ActorState& hero, bool initialize)
{
    if (hero.guildStatus == Master)
        return skill == 0x46 || skill == 0x47 || ((hero.characterClass & 7u) == 4 && skill == LastSiegeSkill);
    if (hero.guildStatus == Assistant)
        return skill == 0x46 || skill == 0x47;
    return !initialize && hero.guildStatus == BattleMaster && FirstSiegeSkill <= skill && skill <= 0x45;
}

Core::Expected<std::uint8_t, std::string> SkillCost(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ActorState& hero = context.world.Hero();
    const Models::LogicalAccess access(runtime.movement.skillDepths[hero.byte371], context.random, 60);
    if (context.skills == nullptr)
        return Core::Unexpected(std::string("0x65243e: sin Skill.bmd para la habilidad del asedio"));
    const auto record = context.skills->Record(hero.byte371);
    if (!record)
        return Core::Unexpected(std::string("0x65243e: falta el registro de la habilidad del asedio"));
    return std::to_integer<std::uint8_t>((*record)[0x33]);
}

bool InsideInclusive(std::int32_t x, std::int32_t y, std::int32_t left, std::int32_t top, std::int32_t right,
                     std::int32_t bottom)
{
    return left <= x && x <= right && top <= y && y <= bottom;
}

void ConsumeLeft(Kernel::WorldRuntime& runtime)
{
    runtime.mouse.pendingClick = false;
    runtime.mouse.leftHeld = false;
}

Core::Expected<void, std::string> Send(const Hud::GameUiContext& context, Protocol::Outgoing packet)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::string("Castle Siege: sin cable para el paquete de la ventana"));
    context.requests->SendGamePacket(std::move(packet));
    return {};
}

Core::Expected<void, std::string> CloseGate(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.panels.delegated.siegeGate = 0;
    runtime.castleSiege.siegeBattleUi.gateKey = 0;
    return Send(context, Protocol::Session::WriteWindowClose());
}

Core::Expected<void, std::string> CloseWeapon(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.panels.delegated.siegeWeapon = 0;
    runtime.castleSiege.siegeBattleUi.weaponChoice = 0;
    return Send(context, Protocol::Session::WriteWindowClose());
}

Core::Expected<void, std::string> StepWeapon(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::uint8_t mode = runtime.panels.delegated.siegeWeapon;
    if (mode == 0)
        return {};
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (450 <= x && x < 640 && 0 <= y && y < 400)
        runtime.ui.mouseOnUi = 1;
    const std::int32_t count = mode == 1 ? 3 : mode == 2 ? 2 : 0;
    const std::int32_t stride = mode == 1 ? 58 : 95;
    const std::int32_t width = mode == 1 ? 32 : 54;
    const std::int32_t bottom = mode == 1 ? 172 : 182;
    for (std::int32_t index = 0; index < count; ++index)
    {
        if (runtime.mouse.pendingClick &&
            InsideInclusive(x, y, 470 + index * stride, 140, 470 + index * stride + width, bottom))
        {
            runtime.castleSiege.siegeBattleUi.weaponChoice = static_cast<std::uint8_t>(index + 1);
            ConsumeLeft(runtime);
        }
    }
    if (count != 0 && runtime.mouse.pendingClick && InsideInclusive(x, y, 500, 200, 590, 264))
    {
        runtime.castleSiege.siegeBattleUi.weaponChoice = static_cast<std::uint8_t>(count + 1);
        ConsumeLeft(runtime);
    }
    if (runtime.mouse.pendingClick && runtime.castleSiege.siegeBattleUi.weaponChoice != 0 &&
        InsideInclusive(x, y, 470, 330, 620, 378))
    {
        auto sent = Send(context, Protocol::CastleSiege::WriteWeapon(runtime.castleSiege.siegeBattleUi.weaponKey,
                                                                     runtime.castleSiege.siegeBattleUi.weaponChoice));
        if (!sent.HasValue())
            return sent;
        if (auto closed = CloseWeapon(context); !closed.HasValue())
            return closed;
    }
    if (runtime.mouse.pendingClick && 470 <= x && x < 494 && 395 <= y && y < 419)
    {
        ConsumeLeft(runtime);
        return CloseWeapon(context);
    }
    return {};
}

Core::Expected<void, std::string> StepGate(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.panels.delegated.siegeGate == 0)
        return {};
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (450 <= x && x < 640 && 0 <= y && y < 400)
        runtime.ui.mouseOnUi = 1;
    if (runtime.mouse.pendingClick && 470 <= x && x < 683 && 325 <= y && y < 389)
    {
        ConsumeLeft(runtime);
        auto sent =
            Send(context,
                 Protocol::CastleSiege::WriteGate(runtime.castleSiege.siegeBattleUi.gateState != OpenGateFlag ? 1 : 0,
                                                  runtime.castleSiege.siegeBattleUi.gateKey));
        if (!sent.HasValue())
            return sent;
        return CloseGate(context);
    }
    if (runtime.mouse.pendingClick && 470 <= x && x < 494 && 395 <= y && y < 419)
    {
        ConsumeLeft(runtime);
        return CloseGate(context);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> RefreshTheSiegePanel(Kernel::WorldRuntime& runtime)
{
    if (runtime.map != CastleMap)
    {
        runtime.castleSiege.siegePanelKind.reset();
        runtime.castleSiege.siegePanel = CastleSiege::SiegePanelState{};
        return {};
    }
    const Guild::GuildState& guild = runtime.guild;
    const std::uint8_t post = guild.heroGuildStatus;
    std::uint8_t kind = 1;
    if (post == NoGuild)
        kind = 0;
    else if (post == Master)
    {
        if (guild.heroGuildIndex < 0 || static_cast<std::size_t>(guild.heroGuildIndex) >= Guild::GuildTableSize)
            return Core::Unexpected(std::format("0x412203: el gremio {} cae fuera de la tabla", guild.heroGuildIndex));
        const Guild::GuildTableEntry& entry = guild.table[static_cast<std::size_t>(guild.heroGuildIndex)];
        if (entry.unionName[0] == '\0' || std::string_view(entry.unionName.data()) == entry.guildName.data())
            kind = 2;
    }
    else if (post != 0 && post != Assistant && post != BattleMaster)
        return {};
    if (runtime.castleSiege.siegePanelKind == kind)
        return {};
    runtime.castleSiege.siegePanel = CastleSiege::SiegePanelState{};
    if (kind == 2)
        runtime.castleSiege.siegePanel.members.reserve(MemberReservation);
    runtime.castleSiege.siegePanelKind = kind;
    return {};
}

Core::Expected<void, std::string> ReceiveSiegePanelStatus(Kernel::WorldRuntime& runtime,
                                                          const std::array<std::uint8_t, 4>& status)
{
    if (!runtime.castleSiege.siegePanelKind || *runtime.castleSiege.siegePanelKind == 0)
        return {};
    if (status[0] >= CastleSiege::SiegeStatusSlots)
        return Core::Unexpected(
            std::format("0x4102b0/0x410580: el índice {} pisa fuera de las siete marcas", status[0]));
    auto& slot = runtime.castleSiege.siegePanel.status[status[0]];
    slot[3] = status[3];
    slot[0] = status[0];
    slot[1] = status[1];
    slot[2] = status[2];
    slot[4] = 100;
    return {};
}

void AppendSiegePanelMember(Kernel::WorldRuntime& runtime, std::uint8_t index, std::uint8_t x, std::uint8_t y)
{
    if (runtime.castleSiege.siegePanelKind != std::uint8_t{2})
        return;
    runtime.castleSiege.siegePanel.members.push_back({index, x, y});
    runtime.castleSiege.siegePanel.membersHidden = 0;
}

void ResetSiegeClock(Kernel::WorldRuntime& runtime)
{
    auto& clock = runtime.castleSiege.siegeBattleUi.clock;
    clock.blink = 1;
    clock.x = 243;
    clock.y = 400;
    clock.blinkTime = 0.0f;
}
void SetSiegeClock(Kernel::WorldRuntime& runtime, std::uint8_t hours, std::uint8_t minutes, std::uint32_t tick)
{
    auto& clock = runtime.castleSiege.siegeBattleUi.clock;
    clock.hours = hours;
    clock.minutes = minutes;
    clock.tick = tick;
}
void OpenTheSiegeGate(Kernel::WorldState& world, std::uint16_t key)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.castleSiege.siegeBattleUi.gateKey = key;
    runtime.castleSiege.siegeBattleUi.gateState = world.Actors()[Kernel::ActorWithKey(world, key)].flags78;
    runtime.panels.delegated.siegeGate = 1;
}

Core::Expected<void, std::string> ApplySiegeWeaponOpen(const CastleReceiveContext& context, std::uint16_t key,
                                                       std::uint8_t mode)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.panels.delegated.siegeWeapon = 0;
    runtime.castleSiege.siegeBattleUi.weaponChoice = 0;
    runtime.castleSiege.siegeBattleUi.weaponKey = key;
    Hud::GamePanels panels(context.world, context.random);
    LendTheCastlePanelServices(panels, context);
    if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
        return closed;
    runtime.panels.delegated.siegeWeapon = mode;
    runtime.castleSiege.siegeBattleUi.weaponChoice = 0;
    return {};
}

Core::Expected<void, std::string> StepTheSiegeControls(const Hud::GameUiContext& context)
{
    if (auto weapon = StepWeapon(context); !weapon.HasValue())
        return weapon;
    return StepGate(context);
}

void InitializeTheBattleSkill(Kernel::WorldState& world, Crt::CrtRandom& random)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::ActorState& hero = world.Hero();
    if (!HasBattleRole(runtime, hero, false))
        return;
    std::uint32_t selected = FirstSiegeSkill;
    for (std::uint32_t skill = FirstSiegeSkill; skill <= LastSiegeSkill; ++skill)
    {
        if (SkillIndex(world, random, skill) > 0 && SkillForPost(skill, hero, true))
        {
            selected = skill;
            break;
        }
    }
    hero.byte371 = static_cast<std::uint8_t>(selected);
}

Core::Expected<void, std::string> StepTheBattleSkill(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::ActorState& hero = context.world.Hero();
    if (!HasBattleRole(runtime, hero, false))
        return {};
    auto cost = SkillCost(context);
    if (!cost.HasValue())
        return Core::Unexpected(std::move(cost).Error());
    if (cost.Value() > hero.byte372)
        return {};
    if (context.input.IsDown(Input::Key::Shift))
    {
        if (hero.byte373 == 0xff)
            hero.byte373 = hero.skillIndex;
        hero.skillIndex = static_cast<std::uint8_t>(SkillIndex(context.world, context.random, hero.byte371));
    }
    else if (hero.byte373 != 0xff)
    {
        hero.skillIndex = hero.byte373;
        hero.byte373 = 0xff;
    }
    return {};
}

Core::Expected<void, std::string> StepTheBattleSkillChoice(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::ActorState& hero = context.world.Hero();
    if (!HasBattleRole(runtime, hero, true))
        return {};
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if ((x < 0 || x >= 128 || y < 0 || y >= 32) && runtime.ui.wheel == 0)
        return {};
    std::uint8_t& pressed = runtime.castleSiege.siegeBattleUi.skillPressed;
    if (runtime.ui.wheel > 0 || (43 <= x && x < 56 && 4 <= y && y < 15 && runtime.mouse.leftHeld))
    {
        ConsumeLeft(runtime);
        pressed = 1;
    }
    if (runtime.ui.wheel < 0 || (43 <= x && x < 56 && 17 <= y && y < 29 && runtime.mouse.leftHeld))
    {
        ConsumeLeft(runtime);
        pressed = 2;
    }
    if (runtime.ui.wheel == 0 && pressed == 0)
        return {};
    if (runtime.ui.wheel != 0)
        pressed = runtime.ui.wheel > 0 ? 1 : 2;
    const std::uint32_t original = hero.byte371;
    std::uint32_t candidate = original;
    if (pressed == 2)
    {
        do
        {
            ++candidate;
            if (SkillIndex(context.world, context.random, candidate) > 0 && SkillForPost(candidate, hero, false))
            {
                hero.byte371 = static_cast<std::uint8_t>(candidate);
                break;
            }
        } while (std::bit_cast<std::int32_t>(candidate) < 0x49);
    }
    else if (pressed == 1)
    {
        do
        {
            --candidate;
            if (SkillIndex(context.world, context.random, candidate) > 0 && SkillForPost(candidate, hero, false))
            {
                hero.byte371 = static_cast<std::uint8_t>(candidate);
                break;
            }
        } while (std::bit_cast<std::int32_t>(candidate) > 0x42);
    }
    runtime.ui.wheel = 0;
    return {};
}
} // namespace Client::CastleSiege
