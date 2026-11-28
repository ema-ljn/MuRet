#include "CastleSiege/CastleBattleCommands.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Actors/ActorEffectFlags.h"
#include "Kernel/ActorState.h"
#include "CastleSiege/CastleBattleActors.h"
#include "CastleSiege/CastleSiegeMap.h"
#include "CastleSiege/CastleWeaponEffects.h"
#include "CastleSiege/CastleWindows.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Guild/GuildPanels.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Crt/OriginalFormat.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Client::Scenes
{
namespace
{
namespace Battle = Protocol::CastleSiege;

Core::Expected<std::string, std::string> Text(const CastleReceiveContext& context, std::size_t index)
{
    const auto text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected("falta el texto " + std::to_string(index) + " de Text.bmd para la batalla");
    return std::string(text->substr(0, text->find('\0')));
}

Core::Expected<std::string, std::string> Formatted(const CastleReceiveContext& context, std::size_t index,
                                                   std::span<const Crt::FormatArgument> arguments,
                                                   Crt::FormatFunction function = Crt::FormatFunction::Wsprintf)
{
    auto text = Text(context, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    return Crt::FormatOriginal(function, text.Value(), arguments, "la respuesta del asedio");
}

Core::Expected<void, std::string> CancelPopup(const CastleReceiveContext& context)
{
    return UI::CancelThePanelDialog(context.world.Runtime().panels.delegated.panelDialog,
                                    [&context](UI::PanelDialogOwner owner, std::uint32_t reason)
                                    { return AnswerThePanelDialog(context.world, context.send, owner, reason); });
}

Core::Expected<void, std::string> Popup(const CastleReceiveContext& context, std::span<const std::string> lines,
                                        std::uint32_t duration, std::int32_t placed = 1,
                                        UI::PanelDialogOwner owner = UI::PanelDialogOwner::None)
{
    auto& runtime = context.world.Runtime();
    runtime.panels.delegated.panelDialog.timerMs = duration;
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x6600d0: falta el medidor del diálogo de la batalla"));
    auto opened =
        PopUpThePanelDialog(runtime, {.lines = lines, .flags = UI::PanelDialogTimed, .owner = owner, .placed = placed},
                            *context.measure, context.screen, context.tick);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

Core::Expected<void, std::string> CrownAccess(const Battle::CrownAccess& access, const CastleReceiveContext& context)
{
    const auto first = Kernel::ActorWithKey(context.world, access.firstKey);
    if (access.status > 2)
        return {};
    std::array<std::string, 2> lines;
    if (access.status < 2)
    {
        auto message = Formatted(context, access.status == 0 ? 1484 : 1485, {});
        if (!message.HasValue())
            return Core::Unexpected(std::move(message).Error());
        lines[0] = std::move(message).Value();
        return Popup(context, std::span(lines).first(1), 3000);
    }
    const auto second = Kernel::ActorWithKey(context.world, access.secondKey);
    const std::array<Crt::FormatArgument, 1> secondName{
        Crt::StringArgument{Actors::HeroNameOf(context.world.Actors()[second])}};
    auto message = Formatted(context, 1486, secondName);
    if (!message.HasValue())
        return Core::Unexpected(std::move(message).Error());
    lines[0] = std::move(message).Value();
    const std::array<Crt::FormatArgument, 1> firstName{
        Crt::StringArgument{Actors::HeroNameOf(context.world.Actors()[first])}};
    message = Formatted(context, 1488, firstName);
    if (!message.HasValue())
        return Core::Unexpected(std::move(message).Error());
    lines[1] = std::move(message).Value();
    return Popup(context, lines, 3000);
}

Core::Expected<void, std::string> CrownRegistration(const Battle::CrownRegistration& crown,
                                                    const CastleReceiveContext& context)
{
    if (auto cancelled = CancelPopup(context); !cancelled.HasValue())
        return cancelled;
    if (crown.result > 4)
        return {};
    Core::Expected<std::string, std::string> line = std::string{};
    std::uint32_t duration = 3000;
    if (crown.result == 0 || crown.result == 2)
    {
        duration = crown.result == 0 ? 30000u - crown.elapsed : 3000u;
        // 0x6602a0 precede a sprintf; conserva el cambio del reloj incluso si la fuente falta.
        context.world.Runtime().panels.delegated.panelDialog.timerMs = duration;
        const std::size_t textIndex = crown.result == 0 ? 1489 : 1491;
        auto label = Text(context, textIndex);
        if (!label.HasValue())
            return Core::Unexpected(std::move(label).Error());
        const std::array<Crt::FormatArgument, 2> args{Crt::TextArgument{textIndex, label.Value()},
                                                      std::bit_cast<std::int32_t>(crown.elapsed / 1000u)};
        line = Formatted(context, 1980, args, Crt::FormatFunction::Sprintf);
    }
    else
        line = Text(context, crown.result == 1 ? 1490 : crown.result == 3 ? 1492 : 1982);
    if (!line.HasValue())
        return Core::Unexpected(std::move(line).Error());
    const std::array<std::string, 1> lines{std::move(line).Value()};
    return Popup(context, lines, duration, crown.result == 0 ? 0 : 1,
                 crown.result == 0 ? UI::PanelDialogOwner::CastleCrownRegistration : UI::PanelDialogOwner::None);
}

Core::Expected<void, std::string> CrownState(const Battle::CrownState& state, const CastleReceiveContext& context)
{
    if (auto cancelled = CancelPopup(context); !cancelled.HasValue())
        return cancelled;
    if (state.result > 2)
        return {};
    auto text = Text(context, std::size_t{1493} + state.result);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    const std::array<std::string, 1> lines{std::move(text).Value()};
    if (auto opened = Popup(context, lines, 3000); !opened.HasValue())
        return opened;
    if (state.result == 2)
        return {};
    std::size_t actor = 0;
    for (; actor < CastleBattleActorCount; ++actor)
        if (context.world.Actors()[actor].alive && context.world.Actors()[actor].word376 == 0xd8)
            break;
    context.world.Actors()[actor].flags78 = state.result == 0 ? 0x20000u : 0u;
    return {};
}

Core::Expected<void, std::string> CastleOwner(const Battle::CastleOwner& owner, const CastleReceiveContext& context)
{
    if (owner.result > 1)
        return {};
    std::string guild;
    for (const std::uint8_t byte : owner.guild)
    {
        if (byte == 0)
            break;
        guild.push_back(static_cast<char>(byte));
    }
    if (owner.result == 1)
        if (auto applied = ApplyTheCastleOwner(guild, context.world); !applied.HasValue())
            return applied;
    const std::array<Crt::FormatArgument, 1> args{Crt::StringArgument{guild}};
    auto message = Formatted(context, std::size_t{1496} + owner.result, args);
    if (!message.HasValue())
        return Core::Unexpected(std::move(message).Error());
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x554610: falta el medidor del anuncio del castillo"));
    auto& runtime = context.world.Runtime();
    UI::AddNotice(runtime.chat.area, *context.measure, runtime.hud.dcFont, message.Value(), 1);
    return {};
}

Core::Expected<void, std::string> WeaponFailure(const CastleReceiveContext& context, bool firing)
{
    // 0x6c8618 y 0x6c862c: literales EUC-KR del original, no índices de Text.bmd.
    constexpr std::string_view OpenFailed = "\xb0\xf8\xbc\xba \xb9\xab\xb1\xe2 \xbc\xb1\xc5\xc3 \xbd\xc7\xc6\xd0";
    constexpr std::string_view FireFailed =
        "\xb0\xf8\xbc\xba\xb9\xab\xb1\xe2 \xb9\xdf\xbb\xe7 \xbd\xc7\xc6\xd0\xc7\xdf\xbd\xc0\xb4\xcf\xb4\xd9.";
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x555190: falta el medidor del error del arma del castillo"));
    return AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {},
                          firing ? FireFailed : OpenFailed, UI::SystemMessage);
}

void SiegeSide(const Battle::SiegeSide& side, Kernel::WorldRuntime& runtime, Kernel::ActorState& hero)
{
    constexpr std::array<std::uint32_t, 5> Flags{0, 0x40000, 0x80000, 0x400000, 0x800000};
    if (side.side >= Flags.size())
        return;
    const auto flag = Flags[side.side];
    Actors::ApplyCastleSiegeFlags(hero, flag, runtime.map);
    if (side.side == 0)
        return;
    if (side.side == 1)
    {
        for (const std::uint32_t other : {0x80000u, 0x400000u, 0x800000u})
            if ((hero.flags78 & other) != 0)
            {
                hero.flags78 &= ~other;
                break;
            }
    }
    else
        hero.flags78 &= ~0x40000u;
    hero.flags78 |= flag;
}
} // namespace

Core::Expected<void, std::string> ApplyCastleBattleCommand(const Battle::CastleBattlePacket& packet,
                                                           const CastleReceiveContext& context)
{
    auto& world = context.world;
    auto& runtime = world.Runtime();
    return std::visit(
        [&](const auto& received) -> Core::Expected<void, std::string>
        {
            using Received = std::decay_t<decltype(received)>;
            if constexpr (std::is_same_v<Received, Battle::GateOpen>)
            {
                if (received.result != 1)
                    return {};
                GamePanels panels(world, context.random);
                LendTheCastlePanelServices(panels, context);
                auto opened = panels.Registry().Open(UI::SiegeGatePanel, received.key);
                if (!opened.HasValue())
                    return Core::Unexpected(std::move(opened).Error());
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::GateCommand> ||
                               std::is_same_v<Received, Battle::GateState>)
            {
                if constexpr (std::is_same_v<Received, Battle::GateCommand>)
                    if (received.result != 1)
                        return {};
                const auto index = Kernel::ActorWithKey(world, received.key);
                auto& actor = world.Actors()[index];
                if constexpr (std::is_same_v<Received, Battle::GateCommand>)
                {
                    if (actor.word376 != 0x115)
                        return {};
                }
                else if (received.open != 0 && runtime.selectedTargets[2] == static_cast<std::int32_t>(index) &&
                         runtime.selectedTargets[4] != -1)
                    runtime.selectedTargets[4] = -1;
                if (context.entryResources == nullptr)
                    return Core::Unexpected(std::string("0x44f340/0x44f560: faltan los recursos de la puerta"));
                return SetTheCastleGate(actor, actor, received.open != 0, *context.entryResources);
            }
            else if constexpr (std::is_same_v<Received, Battle::CrownAccess>)
                return CrownAccess(received, context);
            else if constexpr (std::is_same_v<Received, Battle::CrownRegistration>)
                return CrownRegistration(received, context);
            else if constexpr (std::is_same_v<Received, Battle::CrownState>)
                return CrownState(received, context);
            else if constexpr (std::is_same_v<Received, Battle::SiegeActivation>)
            {
                auto loaded = SetTheSiegeActive(received.active, world, context.terrain, context.dataRoot);
                ResetSiegeClock(runtime);
                return loaded;
            }
            else if constexpr (std::is_same_v<Received, Battle::CastleOwner>)
                return CastleOwner(received, context);
            else if constexpr (std::is_same_v<Received, Battle::SiegeSide>)
            {
                SiegeSide(received, runtime, world.Hero());
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::PanelStatus>)
                return ReceiveSiegePanelStatus(runtime, received.bytes);
            else if constexpr (std::is_same_v<Received, Battle::SiegeTime>)
            {
                SetSiegeClock(runtime, received.hours, received.minutes, context.tick);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::WeaponOpen>)
            {
                if (received.result == 1)
                    return ApplySiegeWeaponOpen(context, received.key, received.mode);
                if (received.result == 0)
                    return WeaponFailure(context, false);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::WeaponFire>)
            {
                if (received.mode == 0)
                    return WeaponFailure(context, true);
                if (context.entryResources == nullptr)
                    return Core::Unexpected(std::string("0x44eea0: faltan los recursos del disparo del castillo"));
                return ApplyTheSiegeWeaponFire(received.key, received.kind, received.mode, received.x, received.y,
                                               *context.entryResources);
            }
            else if constexpr (std::is_same_v<Received, Battle::WeaponImpact>)
            {
                if (context.entryResources == nullptr)
                    return Core::Unexpected(std::string("0x44efc0: faltan los recursos del impacto del castillo"));
                return ApplyTheSiegeWeaponImpact(received.kind, received.x, received.y, *context.entryResources);
            }
            else
                return Core::Unexpected(std::string("el paquete no es una orden B2 de la batalla"));
        },
        packet);
}
} // namespace Client::Scenes
