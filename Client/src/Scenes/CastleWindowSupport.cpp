#include "Scenes/CastleWindowSupport.h"

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/GameUi.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/CastleSiegeState.h"
#include "UI/PanelDialog.h"

#include <cstddef>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes::CastleUi
{
Core::Expected<std::string, std::string> Text(const Data::Text::TextTable* texts, std::uint32_t address)
{
    const auto text = UI::TextAt(texts, TextId(address));
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(TextId(address)) + " del castillo no está en Text.bmd");
    return std::string(text->substr(0, text->find('\0')));
}

Core::Expected<void, std::string> Send(const GameUiContext& context, Protocol::Outgoing packet)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::string("la ventana del castillo envía sin el cable del GameServer"));
    context.requests->SendGamePacket(std::move(packet));
    return {};
}

CastleSender SenderOf(const GameUiContext& context)
{
    if (context.requests == nullptr)
        return {};
    return [requests = context.requests](Protocol::Outgoing packet) { requests->SendGamePacket(std::move(packet)); };
}

Core::Expected<std::uint32_t, std::string> Popup(const GameUiContext& context, std::span<const std::string> lines,
                                                 UI::PanelDialogOwner owner, std::uint32_t flags, std::int32_t placed)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x6600d0: el diálogo del castillo sin medidor"));
    return PopUpThePanelDialog(context.world.Runtime(),
                               {.lines = lines, .flags = flags, .owner = owner, .placed = placed}, *context.measure,
                               context.screen, context.systemTime);
}

Core::Expected<std::uint32_t, std::string> PopupAt(const GameUiContext& context,
                                                   std::initializer_list<std::uint32_t> addresses,
                                                   UI::PanelDialogOwner owner, std::uint32_t flags, std::int32_t placed)
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

Core::Expected<void, std::string> CancelPopup(WorldState& world, const CastleSender& send)
{
    return UI::CancelThePanelDialog(world.Runtime().panels.delegated.panelDialog,
                                    [&world, &send](UI::PanelDialogOwner owner, std::uint32_t reason)
                                    { return AnswerThePanelDialog(world, send, owner, reason); });
}

void ConsumeClick(WorldRuntime& runtime)
{
    runtime.movement.pendingClick = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = 6;
}

bool HeroCanRegister(WorldState& world, Render::Models::CrtRandom& random)
{
    if (world.Actors()[world.Runtime().hero].guildStatus != Master)
        return false;
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                               Render::Models::OwnRecordCloseRandoms);
    return world.OwnCharacter().level >= 200;
}

std::uint32_t CountMarks(WorldState& world, Render::Models::CrtRandom& random, std::uint8_t& depth)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::OwnRecordCloseRandoms);
    std::uint32_t count = 0;
    for (const auto& item : world.Inventory().cells)
    {
        if (item.occupied != 0 && item.id == 0x1c15 && (item.scalar1Low & 0x78u) == 0x18)
            count += item.durability;
    }
    return count;
}

std::int32_t FirstMark(WorldState& world, Render::Models::CrtRandom& random, std::uint8_t& depth)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::OwnRecordCloseRandoms);
    const auto& cells = world.Inventory().cells;
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        const auto& item = cells[i];
        if (item.occupied != 0 && item.id == 0x1c15 && (item.scalar1Low & 0x78u) == 0x18)
            return static_cast<std::int32_t>(i);
    }
    return -1;
}

Core::Expected<std::pair<std::string_view, std::string_view>, std::string> GuildNames(const WorldState& world)
{
    const auto index = static_cast<std::int16_t>(world.Actors()[world.Runtime().hero].word1f2);
    const auto& state = world.Runtime().guild;
    const auto guild = GuildNameAt(state, index);
    const auto alliance = UnionNameAt(state, index);
    if (!guild || !alliance)
        return Core::Unexpected(std::string("el castillo lee el gremio del héroe fuera de la tabla 0x787aa48"));
    // El índice −1 pisa el miembro74 anterior a la tabla; los helpers compartidos conservan ese alias Q-C1281.
    return std::pair<std::string_view, std::string_view>{*alliance, *guild};
}

Core::Expected<bool, std::string> HeroCanEditTaxes(const WorldState& world)
{
    if (world.Actors()[world.Runtime().hero].guildStatus != Master)
        return false;
    auto names = GuildNames(world);
    if (!names.HasValue())
        return Core::Unexpected(std::move(names).Error());
    // 0x669334 JNZ y 0x66936e JZ: sólo ambos nombres vacíos deniegan, no se exige que coincidan.
    return names.Value().first != names.Value().second || !names.Value().first.empty();
}

std::int32_t DefenseLevel(const UI::CastleData& data, std::uint32_t at)
{
    const auto type = data.U32(at + UI::RecordType);
    return type == UI::GateType || type == UI::StatueType
               ? std::bit_cast<std::int32_t>(data.U32(at + UI::RecordDefense))
               : -1;
}

std::int32_t RecoveryLevel(const UI::CastleData& data, std::uint32_t at)
{
    const auto type = data.U32(at + UI::RecordType);
    return type == UI::GateType     ? 0
           : type == UI::StatueType ? std::bit_cast<std::int32_t>(data.U32(at + UI::RecordRecovery))
                                    : -1;
}

std::int32_t NpcCost(const UI::CastleData& data, std::uint32_t at)
{
    const auto type = data.U32(at + UI::RecordType);
    const auto defense = data.U32(at + UI::RecordDefense);
    const auto life = data.U32(at + UI::RecordLife);
    const auto maxLife = data.U32(at + UI::RecordMaxLife);
    if (type == UI::GateType)
        return std::bit_cast<std::int32_t>((defense * 200000u - life + maxLife) * 5u);
    if (type == UI::StatueType)
        return std::bit_cast<std::int32_t>((maxLife - life) * 3u + defense * 1000000u);
    return -1;
}
} // namespace Client::Scenes::CastleUi
