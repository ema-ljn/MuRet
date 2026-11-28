#include "CastleSiege/CastleBattleReceive.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "CastleSiege/CastleFlagUpload.h"
#include "CastleSiege/CastleBattleCommands.h"
#include "CastleSiege/CastleWindows.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "Hud/GamePanels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "CastleSiege/CastleBattleState.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Assets/FaunaModels.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace Client::CastleSiege
{
namespace
{
namespace Battle = Protocol::CastleSiege;

// B9 01, 0x4e0750: also on record 400 when the key is missing.
void ApplyNpcLevel(const Battle::NpcLevel& level, Kernel::WorldState& world)
{
    world.Actors()[Kernel::ActorWithKey(world, level.key)].byte93 = level.level;
}

// B9 02, 0x4e0790: byte i / 2, the even one in the high nibble (SHR 4) and the odd one in the low one (AND 0xf).
Core::Expected<void, std::string> ApplyOwnerMark(const Battle::OwnerMark& mark, Kernel::WorldState& world,
                                                 Assets::FaunaModels* faunaModels)
{
    auto& battle = world.Runtime().castleSiege.battle;
    for (std::size_t i = 0; i < CastleSiege::OwnerMarkColors; ++i)
    {
        const std::uint8_t packed = mark.packed[i / 2];
        battle.ownerMark[i] =
            (i % 2 == 0) ? static_cast<std::uint8_t>(packed >> 4u) : static_cast<std::uint8_t>(packed & 0xfu);
    }
    ++battle.ownerMarkRevision;
    if (faunaModels == nullptr)
        return Core::Unexpected(std::string("0x4e0790: no fauna models (0x5769ad8) for the castle's flag"));
    return UploadTheCastleFlag(battle.ownerMark, faunaModels->TextureTable());
}

// BA, 0x4cf1b0: 0x5110d0 always; +0x1bc only with which at 0x28, also on record 400.
void ApplyActorField(const Battle::ActorField& field, Kernel::WorldState& world)
{
    const std::size_t index = Kernel::ActorWithKey(world, field.key);
    if (field.kind == Battle::ActorField1bcKind)
    {
        world.Actors()[index].byte1bc = field.value;
    }
}

// BB, 0x4e0f30: only with a [0x5763530] of class 2 and a non-zero count.
Core::Expected<void, std::string> ApplyMinimapMembers(const Battle::MinimapMembers& members,
                                                      const CastleReceiveContext& context)
{
    constexpr std::uint8_t MinimapPanelKind = 2;
    if (context.world.Runtime().castleSiege.siegePanelKind != MinimapPanelKind || members.members.empty())
    {
        return {};
    }
    for (const auto& member : members.members)
        AppendSiegePanelMember(context.world.Runtime(), static_cast<std::uint8_t>(member.index + 1u), member.x,
                               member.y);
    return {};
}

// B2 20, 0x4e04e0.
void ApplySwitchHolder(const Battle::SwitchHolder& holder, Kernel::WorldRuntime& runtime)
{
    const bool first = holder.key == CastleSiege::FirstHalfKey;
    const std::size_t half = first ? 0 : CastleSiege::SwitchHolderHalf;
    if (!runtime.castleSiege.castleSiegeInfo)
    {
        // 0x4e04f3..0x4e0528: new(0x36) with +4, +5, +6, +0x1f, +0x20 and +0x21 at 0 (the rest, whatever the heap left:
        // Q-C1400) and the half the key picks, as is.
        runtime.castleSiege.castleSiegeInfo.emplace();
        std::ranges::copy(holder.bytes,
                          runtime.castleSiege.castleSiegeInfo->begin() + static_cast<std::ptrdiff_t>(half));
        return;
    }
    // 0x4e0558..0x4e05e4: the half as is and then, with the same, +8..+0x19 again and the last byte at 0 (DL at 0,
    // 0x4e05c7): Q-C1401.
    std::ranges::copy(holder.bytes, runtime.castleSiege.castleSiegeInfo->begin() + static_cast<std::ptrdiff_t>(half));
    (*runtime.castleSiege.castleSiegeInfo)[half + CastleSiege::HolderEndAt] = 0;
}
} // namespace

void ForgetTheSwitchHolder(Kernel::WorldRuntime& runtime, std::uint16_t key)
{
    if (!runtime.castleSiege.castleSiegeInfo)
    {
        return;
    }
    const std::size_t half = key == CastleSiege::FirstHalfKey ? 0 : CastleSiege::SwitchHolderHalf;
    for (const std::size_t at : {CastleSiege::HolderShownAt, std::size_t{4}, std::size_t{5}})
    {
        (*runtime.castleSiege.castleSiegeInfo)[half + at] = 0;
    }
}

Core::Expected<void, std::string> ApplyCastleBattlePacket(const Protocol::CastleSiege::CastleBattlePacket& packet,
                                                          const CastleReceiveContext& context)
{
    return std::visit(
        [&context, &packet](const auto& received) -> Core::Expected<void, std::string>
        {
            using Received = std::decay_t<decltype(received)>;
            if constexpr (std::is_same_v<Received, Battle::NpcLevel>)
            {
                ApplyNpcLevel(received, context.world);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::OwnerMark>)
            {
                return ApplyOwnerMark(received, context.world, context.faunaModels);
            }
            else if constexpr (std::is_same_v<Received, Battle::HuntZoneInfo>)
                return ApplyHuntInformation(received.mode, received.allowed, received.fee, received.at0c, received.at10,
                                            context);
            else if constexpr (std::is_same_v<Received, Battle::HuntZoneEntryResult>)
            {
                return ApplyHuntEntryResult(received.result, context);
            }
            else if constexpr (std::is_same_v<Received, Battle::ActorField>)
            {
                ApplyActorField(received, context.world);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Battle::MinimapMembers>)
                return ApplyMinimapMembers(received, context);
            else if constexpr (std::is_same_v<Received, Battle::SwitchHolder>)
            {
                ApplySwitchHolder(received, context.world.Runtime());
                return {};
            }
            else
                return ApplyCastleBattleCommand(packet, context);
        },
        packet);
}
} // namespace Client::CastleSiege
