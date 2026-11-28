#include "CastleSiege/CastleWindowSupport.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Hud/GameUi.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Ui/PanelDialog.h"

#include <cstddef>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::CastleSiege::CastleUi
{
Core::Expected<std::string, std::string> Text(const Tables::TextTable* texts, std::uint32_t address)
{
    const auto text = Ui::TextAt(texts, TextId(address));
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(TextId(address)) + " del castillo no está en Text.bmd");
    return std::string(text->substr(0, text->find('\0')));
}

Core::Expected<void, std::string> Send(const Hud::GameUiContext& context, Protocol::Outgoing packet)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::string("la ventana del castillo envía sin el cable del GameServer"));
    context.requests->SendGamePacket(std::move(packet));
    return {};
}

CastleSender SenderOf(const Hud::GameUiContext& context)
{
    if (context.requests == nullptr)
        return {};
    return [requests = context.requests](Protocol::Outgoing packet) { requests->SendGamePacket(std::move(packet)); };
}

Core::Expected<std::uint32_t, std::string> Popup(const Hud::GameUiContext& context, std::span<const std::string> lines,
                                                 Ui::PanelDialogOwner owner, std::uint32_t flags, std::int32_t placed)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x6600d0: el diálogo del castillo sin medidor"));
    return Guild::PopUpThePanelDialog(context.world.Runtime(),
                                      {.lines = lines, .flags = flags, .owner = owner, .placed = placed},
                                      *context.measure, context.screen, context.systemTime);
}

Core::Expected<std::uint32_t, std::string> PopupAt(const Hud::GameUiContext& context,
                                                   std::initializer_list<std::uint32_t> addresses,
                                                   Ui::PanelDialogOwner owner, std::uint32_t flags, std::int32_t placed)
{
    std::vector<std::string> lines;
    for (const auto address : addresses)
    {
        auto line = Text(context.texts, address);
        if (!line.HasValue())
            return Core::Unexpected(std::move(line).Error());
        lines.push_back(std::move(line).Value());
    }
    return Popup(context, lines, owner, flags, placed);
}

Core::Expected<void, std::string> CancelPopup(Kernel::WorldState& world, const CastleSender& send)
{
    return Ui::CancelThePanelDialog(world.Runtime().panels.delegated.panelDialog,
                                    [&world, &send](Ui::PanelDialogOwner owner, std::uint32_t reason)
                                    { return Guild::AnswerThePanelDialog(world, send, owner, reason); });
}

void ConsumeClick(Kernel::WorldRuntime& runtime)
{
    runtime.mouse.pendingClick = false;
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = 6;
}

bool HeroCanRegister(Kernel::WorldState& world, Crt::CrtRandom& random)
{
    if (world.Hero().guildStatus != Master)
        return false;
    const Models::LogicalAccess access(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    return world.OwnCharacter().level >= 200;
}

std::uint32_t CountMarks(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint8_t& depth)
{
    const Models::LogicalAccess access(depth, random, Models::OwnRecordCloseRandoms);
    std::uint32_t count = 0;
    for (const auto& item : world.Inventory().cells)
    {
        if (item.occupied != 0 && item.id == 0x1c15 && (item.scalar1Low & 0x78u) == 0x18)
            count += item.durability;
    }
    return count;
}

std::int32_t FirstMark(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint8_t& depth)
{
    const Models::LogicalAccess access(depth, random, Models::OwnRecordCloseRandoms);
    const auto& cells = world.Inventory().cells;
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        const auto& item = cells[i];
        if (item.occupied != 0 && item.id == 0x1c15 && (item.scalar1Low & 0x78u) == 0x18)
            return static_cast<std::int32_t>(i);
    }
    return -1;
}

Core::Expected<std::pair<std::string_view, std::string_view>, std::string> GuildNames(const Kernel::WorldState& world)
{
    const auto index = static_cast<std::int16_t>(world.Hero().word1f2);
    const auto& state = world.Runtime().guild;
    const auto guild = Guild::GuildNameAt(state, index);
    const auto alliance = Guild::UnionNameAt(state, index);
    if (!guild || !alliance)
        return Core::Unexpected(std::string("el castillo lee el gremio del héroe fuera de la tabla 0x787aa48"));
    // Index −1 overwrites member 74 before the table; the shared helpers keep that alias, Q-C1281.
    return std::pair<std::string_view, std::string_view>{*alliance, *guild};
}

Core::Expected<bool, std::string> HeroCanEditTaxes(const Kernel::WorldState& world)
{
    if (world.Hero().guildStatus != Master)
        return false;
    auto names = GuildNames(world);
    if (!names.HasValue())
        return Core::Unexpected(std::move(names).Error());
    // 0x669334 JNZ and 0x66936e JZ: only both names empty deny; they are not required to match.
    return names.Value().first != names.Value().second || !names.Value().first.empty();
}

std::int32_t DefenseLevel(const CastleSiege::CastleData& data, std::uint32_t at)
{
    const auto type = data.U32(at + CastleSiege::RecordType);
    return type == CastleSiege::GateType || type == CastleSiege::StatueType
               ? std::bit_cast<std::int32_t>(data.U32(at + CastleSiege::RecordDefense))
               : -1;
}

std::int32_t RecoveryLevel(const CastleSiege::CastleData& data, std::uint32_t at)
{
    const auto type = data.U32(at + CastleSiege::RecordType);
    return type == CastleSiege::GateType     ? 0
           : type == CastleSiege::StatueType ? std::bit_cast<std::int32_t>(data.U32(at + CastleSiege::RecordRecovery))
                                             : -1;
}

std::int32_t NpcCost(const CastleSiege::CastleData& data, std::uint32_t at)
{
    const auto type = data.U32(at + CastleSiege::RecordType);
    const auto defense = data.U32(at + CastleSiege::RecordDefense);
    const auto life = data.U32(at + CastleSiege::RecordLife);
    const auto maxLife = data.U32(at + CastleSiege::RecordMaxLife);
    if (type == CastleSiege::GateType)
        return std::bit_cast<std::int32_t>((defense * 200000u - life + maxLife) * 5u);
    if (type == CastleSiege::StatueType)
        return std::bit_cast<std::int32_t>((maxLife - life) * 3u + defense * 1000000u);
    return -1;
}
} // namespace Client::CastleSiege::CastleUi
