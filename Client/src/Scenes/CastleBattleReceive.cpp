#include "Scenes/CastleBattleReceive.h"

#include "Core/Expected.h"
#include "Protocol/CastleBattle.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GamePanels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleBattleState.h"
#include "UI/CastleSiegeState.h"
#include "UI/PanelRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Client::Scenes
{
namespace
{
namespace Battle = Protocol::CastleBattle;

UI::CastleSiegeState& CastleOf(const CastleReceiveContext& context)
{
    return context.world.Runtime().castleSiege;
}

// 0x4e07fd y 0x4e086d: con 0, 0x6603c0 y 0x6600d0(Text[860], 1, 0x32, 1, 0, 0) sobre [0x7d53704], el diálogo de los
// paneles, como el B2 1F con 0 (0x4e5594).
Core::Unexpected<std::string> DialogCut(std::string_view at, std::string_view what)
{
    return Core::Unexpected(std::string(at) + ": " + std::string(what) +
                            " con 0 cierra el diálogo de los paneles (0x6603c0) y abre el del texto 860 (0x6600d0): es "
                            "del hito 8 de la CL9 (P-C90)");
}

// B9 01, 0x4e0750: también en el registro 400 cuando la clave no está.
void ApplyNpcLevel(const Battle::NpcLevel& level, WorldState& world)
{
    world.Actors()[ActorWithKey(world, level.key)].byte93 = level.level;
}

// B9 02, 0x4e0790: el byte i / 2, el par en el nibble de arriba (SHR 4) y el impar en el de abajo (AND 0xf).
void ApplyOwnerMark(const Battle::OwnerMark& mark, UI::CastleBattleState& battle)
{
    for (std::size_t i = 0; i < UI::OwnerMarkColors; ++i)
    {
        const std::uint8_t packed = mark.packed[i / 2];
        battle.ownerMark[i] =
            (i % 2 == 0) ? static_cast<std::uint8_t>(packed >> 4u) : static_cast<std::uint8_t>(packed & 0xfu);
    }
    ++battle.ownerMarkRevision;
}

// B9 03, 0x4e07f0: con pkt[4] distinto de 0 escribe [0x7d53700] y abre el 0x200000 (0x6599a0(0x200000, 0) sobre
// [0x576352c], sin mirar lo que devuelve).
Core::Expected<void, std::string> ApplyHuntZoneInfo(const Battle::HuntZoneInfo& info,
                                                    const CastleReceiveContext& context)
{
    if (info.mode == 0)
    {
        return DialogCut("0x4e07fd", "el B9 03");
    }
    UI::HuntZoneWindow& window = CastleOf(context).huntZone;
    window.mode = info.mode;
    window.allowed = info.allowed != 0 ? 1 : 0;
    window.fee = info.fee;
    window.field1c = info.fee;
    window.field24 = info.at0c;
    window.field20 = info.at10;
    GamePanels panels(context.world, context.random);
    auto opened = panels.Registry().Open(UI::SecondCastleSiegePanel);
    if (!opened.HasValue())
    {
        return Core::Unexpected(std::move(opened).Error());
    }
    return {};
}

// BA, 0x4cf1b0: 0x5110d0 siempre; +0x1bc solo con cuál en 0x28, también en el registro 400.
void ApplyActorField(const Battle::ActorField& field, WorldState& world)
{
    const std::size_t index = ActorWithKey(world, field.key);
    if (field.kind == Battle::ActorField1bcKind)
    {
        world.Actors()[index].byte1bc = field.value;
    }
}

// BB, 0x4e0f30: solo con [0x5763530] de la clase 2 y una cuenta distinta de 0.
Core::Expected<void, std::string> ApplyMinimapMembers(const Battle::MinimapMembers& members,
                                                      const CastleReceiveContext& context)
{
    constexpr std::uint8_t MinimapPanelKind = 2;
    if (context.world.Runtime().siegePanelKind != MinimapPanelKind || members.members.empty())
    {
        return {};
    }
    return Core::Unexpected(std::string("0x4e0f61: los miembros del panel del asedio de la clase 2 (0x411ed0 con el "
                                        "índice + 1, x e y) son del panel de la batalla, la fase 2 del hito 13 de "
                                        "la CL9 (P-C301)"));
}

// B2 20, 0x4e04e0.
void ApplySwitchHolder(const Battle::SwitchHolder& holder, UI::CastleBattleState& battle)
{
    const bool first = holder.key == UI::FirstHalfKey;
    const std::size_t half = first ? 0 : UI::SwitchHolderHalf;
    if (!battle.switchHolders)
    {
        // 0x4e04f3..0x4e0528: new(0x36) con +4, +5, +6, +0x1f, +0x20 y +0x21 en 0 (lo demás, lo que dejó el heap:
        // Q-C1400) y la mitad que elige la clave, tal cual.
        battle.switchHolders.emplace();
        std::ranges::copy(holder.bytes, battle.switchHolders->begin() + static_cast<std::ptrdiff_t>(half));
        return;
    }
    // 0x4e0558..0x4e05e4: la mitad tal cual y después, con lo mismo, +8..+0x19 otra vez y el último byte en 0 (DL en 0,
    // 0x4e05c7): Q-C1401.
    std::ranges::copy(holder.bytes, battle.switchHolders->begin() + static_cast<std::ptrdiff_t>(half));
    (*battle.switchHolders)[half + UI::HolderEndAt] = 0;
}
} // namespace

void ForgetTheSwitchHolder(UI::CastleBattleState& battle, std::uint16_t key)
{
    if (!battle.switchHolders)
    {
        return;
    }
    const std::size_t half = key == UI::FirstHalfKey ? 0 : UI::SwitchHolderHalf;
    for (std::size_t at = 4; at <= UI::HolderShownAt; ++at)
    {
        (*battle.switchHolders)[half + at] = 0;
    }
}

Core::Expected<void, std::string> ApplyCastleBattlePacket(const Protocol::CastleBattle::CastleBattlePacket& packet,
                                                          const CastleReceiveContext& context)
{
    return std::visit(
        [&context](const auto& received) -> Core::Expected<void, std::string>
        {
            using Received = std::decay_t<decltype(received)>;
            if constexpr (std::is_same_v<Received, Battle::NpcLevel>)
            {
                ApplyNpcLevel(received, context.world);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::OwnerMark>)
            {
                ApplyOwnerMark(received, CastleOf(context).battle);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::HuntZoneInfo>)
                return ApplyHuntZoneInfo(received, context);
            else if constexpr (std::is_same_v<Received, Battle::HuntZoneEntryResult>)
            {
                // 0x4e0860: con otro valor, nada.
                if (received.result == 0)
                    return DialogCut("0x4e086d", "el B9 05");
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::ActorField>)
            {
                ApplyActorField(received, context.world);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::MinimapMembers>)
                return ApplyMinimapMembers(received, context);
            else
            {
                static_assert(std::is_same_v<Received, Battle::SwitchHolder>);
                ApplySwitchHolder(received, CastleOf(context).battle);
                return {};
            }
        },
        packet);
}
} // namespace Client::Scenes
