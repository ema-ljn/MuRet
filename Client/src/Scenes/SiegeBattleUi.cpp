#include "Scenes/SiegeBattleUi.h"
#include "Scenes/CastleWindows.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Maps/MapId.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/Outgoing.h"
#include "Protocol/SiegeControls.h"
#include "Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameUi.h"
#include "Scenes/GuildState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/SiegeBattleUiState.h"
#include "UI/SiegePanelState.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
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
constexpr std::uint8_t FirstSiegeSkill = 0x43;
constexpr std::uint8_t LastSiegeSkill = 0x48;

bool HasBattleRole(const WorldRuntime& runtime, const ActorState& hero, bool guildRequired)
{
    const bool role = hero.byte360 == 6 || hero.byte360 == 7 || hero.byte360 == 8 || hero.byte360 == 12;
    return runtime.siegeActive && role && hero.guildStatus != 0 &&
           (!guildRequired || static_cast<std::int16_t>(hero.word1f2) >= 0);
}

std::int32_t SkillIndex(WorldState& world, Crt::CrtRandom& random, std::uint32_t skill)
{
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                               Render::Models::OwnRecordCloseRandoms);
    constexpr std::size_t OwnSkillSlots = 64;
    for (std::size_t index = 0; index < OwnSkillSlots; ++index)
    {
        if (world.OwnCharacter().SkillAt(index) == skill)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}

bool SkillForPost(std::uint32_t skill, const ActorState& hero, bool initialize)
{
    if (hero.guildStatus == Master)
        return skill == 0x46 || skill == 0x47 || ((hero.characterClass & 7u) == 4 && skill == LastSiegeSkill);
    if (hero.guildStatus == Assistant)
        return skill == 0x46 || skill == 0x47;
    return !initialize && hero.guildStatus == BattleMaster && FirstSiegeSkill <= skill && skill <= 0x45;
}

Core::Expected<std::uint8_t, std::string> SkillCost(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const Render::Models::LogicalAccess access(runtime.movement.skillDepths[hero.byte371], context.random, 60);
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

void ConsumeLeft(WorldRuntime& runtime)
{
    runtime.movement.pendingClick = false;
    runtime.movement.leftHeld = false;
}

Core::Expected<void, std::string> Send(const GameUiContext& context, Protocol::Outgoing packet)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::string("Castle Siege: sin cable para el paquete de la ventana"));
    context.requests->SendGamePacket(std::move(packet));
    return {};
}

Core::Expected<void, std::string> CloseGate(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.panels.delegated.siegeGate = 0;
    runtime.siegeBattleUi.gateKey = 0;
    return Send(context, Protocol::Connection::WriteWindowClose());
}

Core::Expected<void, std::string> CloseWeapon(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.panels.delegated.siegeWeapon = 0;
    runtime.siegeBattleUi.weaponChoice = 0;
    return Send(context, Protocol::Connection::WriteWindowClose());
}

Core::Expected<void, std::string> StepWeapon(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
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
        if (runtime.movement.pendingClick &&
            InsideInclusive(x, y, 470 + index * stride, 140, 470 + index * stride + width, bottom))
        {
            runtime.siegeBattleUi.weaponChoice = static_cast<std::uint8_t>(index + 1);
            ConsumeLeft(runtime);
        }
    }
    if (count != 0 && runtime.movement.pendingClick && InsideInclusive(x, y, 500, 200, 590, 264))
    {
        runtime.siegeBattleUi.weaponChoice = static_cast<std::uint8_t>(count + 1);
        ConsumeLeft(runtime);
    }
    if (runtime.movement.pendingClick && runtime.siegeBattleUi.weaponChoice != 0 &&
        InsideInclusive(x, y, 470, 330, 620, 378))
    {
        auto sent = Send(context, Protocol::SiegeControls::WriteWeapon(runtime.siegeBattleUi.weaponKey,
                                                                       runtime.siegeBattleUi.weaponChoice));
        if (!sent.HasValue())
            return sent;
        if (auto closed = CloseWeapon(context); !closed.HasValue())
            return closed;
    }
    if (runtime.movement.pendingClick && 470 <= x && x < 494 && 395 <= y && y < 419)
    {
        ConsumeLeft(runtime);
        return CloseWeapon(context);
    }
    return {};
}

Core::Expected<void, std::string> StepGate(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.panels.delegated.siegeGate == 0)
        return {};
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (450 <= x && x < 640 && 0 <= y && y < 400)
        runtime.ui.mouseOnUi = 1;
    if (runtime.movement.pendingClick && 470 <= x && x < 683 && 325 <= y && y < 389)
    {
        ConsumeLeft(runtime);
        auto sent =
            Send(context, Protocol::SiegeControls::WriteGate(runtime.siegeBattleUi.gateState != OpenGateFlag ? 1 : 0,
                                                             runtime.siegeBattleUi.gateKey));
        if (!sent.HasValue())
            return sent;
        return CloseGate(context);
    }
    if (runtime.movement.pendingClick && 470 <= x && x < 494 && 395 <= y && y < 419)
    {
        ConsumeLeft(runtime);
        return CloseGate(context);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> RefreshTheSiegePanel(WorldRuntime& runtime)
{
    if (runtime.map != CastleMap)
    {
        runtime.siegePanelKind.reset();
        runtime.siegePanel = UI::SiegePanelState{};
        return {};
    }
    const GuildState& guild = runtime.guild;
    const std::uint8_t post = guild.heroGuildStatus;
    std::uint8_t kind = 1;
    if (post == NoGuild)
        kind = 0;
    else if (post == Master)
    {
        if (guild.heroGuildIndex < 0 || static_cast<std::size_t>(guild.heroGuildIndex) >= GuildTableSize)
            return Core::Unexpected(std::format("0x412203: el gremio {} cae fuera de la tabla", guild.heroGuildIndex));
        const GuildTableEntry& entry = guild.table[static_cast<std::size_t>(guild.heroGuildIndex)];
        if (entry.unionName[0] == '\0' || std::string_view(entry.unionName.data()) == entry.guildName.data())
            kind = 2;
    }
    else if (post != 0 && post != Assistant && post != BattleMaster)
        return {};
    if (runtime.siegePanelKind == kind)
        return {};
    runtime.siegePanel = UI::SiegePanelState{};
    if (kind == 2)
        runtime.siegePanel.members.reserve(MemberReservation);
    runtime.siegePanelKind = kind;
    return {};
}

Core::Expected<void, std::string> ReceiveSiegePanelStatus(WorldRuntime& runtime,
                                                          const std::array<std::uint8_t, 4>& status)
{
    if (!runtime.siegePanelKind || *runtime.siegePanelKind == 0)
        return {};
    if (status[0] >= UI::SiegeStatusSlots)
        return Core::Unexpected(
            std::format("0x4102b0/0x410580: el índice {} pisa fuera de las siete marcas", status[0]));
    auto& slot = runtime.siegePanel.status[status[0]];
    slot[3] = status[3];
    slot[0] = status[0];
    slot[1] = status[1];
    slot[2] = status[2];
    slot[4] = 100;
    return {};
}

void AppendSiegePanelMember(WorldRuntime& runtime, std::uint8_t index, std::uint8_t x, std::uint8_t y)
{
    if (runtime.siegePanelKind != std::uint8_t{2})
        return;
    runtime.siegePanel.members.push_back({index, x, y});
    runtime.siegePanel.membersHidden = 0;
}

void ResetSiegeClock(WorldRuntime& runtime)
{
    auto& clock = runtime.siegeBattleUi.clock;
    clock.blink = 1;
    clock.x = 243;
    clock.y = 400;
    clock.blinkTime = 0.0f;
}
void SetSiegeClock(WorldRuntime& runtime, std::uint8_t hours, std::uint8_t minutes, std::uint32_t tick)
{
    auto& clock = runtime.siegeBattleUi.clock;
    clock.hours = hours;
    clock.minutes = minutes;
    clock.tick = tick;
}
void OpenTheSiegeGate(WorldState& world, std::uint16_t key)
{
    WorldRuntime& runtime = world.Runtime();
    runtime.siegeBattleUi.gateKey = key;
    runtime.siegeBattleUi.gateState = world.Actors()[ActorWithKey(world, key)].flags78;
    runtime.panels.delegated.siegeGate = 1;
}

Core::Expected<void, std::string> ApplySiegeWeaponOpen(const CastleReceiveContext& context, std::uint16_t key,
                                                       std::uint8_t mode)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.panels.delegated.siegeWeapon = 0;
    runtime.siegeBattleUi.weaponChoice = 0;
    runtime.siegeBattleUi.weaponKey = key;
    GamePanels panels(context.world, context.random);
    LendTheCastlePanelServices(panels, context);
    if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
        return closed;
    runtime.panels.delegated.siegeWeapon = mode;
    runtime.siegeBattleUi.weaponChoice = 0;
    return {};
}

Core::Expected<void, std::string> StepTheSiegeControls(const GameUiContext& context)
{
    if (auto weapon = StepWeapon(context); !weapon.HasValue())
        return weapon;
    return StepGate(context);
}

void InitializeTheBattleSkill(WorldState& world, Crt::CrtRandom& random)
{
    const WorldRuntime& runtime = world.Runtime();
    ActorState& hero = world.Actors()[runtime.hero];
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

Core::Expected<void, std::string> StepTheBattleSkill(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    ActorState& hero = context.world.Actors()[runtime.hero];
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

Core::Expected<void, std::string> StepTheBattleSkillChoice(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    ActorState& hero = context.world.Actors()[runtime.hero];
    if (!HasBattleRole(runtime, hero, true))
        return {};
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if ((x < 0 || x >= 128 || y < 0 || y >= 32) && runtime.ui.wheel == 0)
        return {};
    std::uint8_t& pressed = runtime.siegeBattleUi.skillPressed;
    if (runtime.ui.wheel > 0 || (43 <= x && x < 56 && 4 <= y && y < 15 && runtime.movement.leftHeld))
    {
        ConsumeLeft(runtime);
        pressed = 1;
    }
    if (runtime.ui.wheel < 0 || (43 <= x && x < 56 && 17 <= y && y < 29 && runtime.movement.leftHeld))
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
} // namespace Client::Scenes
