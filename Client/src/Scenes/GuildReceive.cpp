#include "Scenes/GuildReceive.h"

#include "Core/Expected.h"
#include "Protocol/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
using namespace Protocol::Guild;

// 0x5110d0: los 400 que recorre; con 400 no encontró a nadie.
constexpr std::size_t ActorCount = 400;
// Text.bmd (la dirección − 0x76bd1dc, / 300).
constexpr std::size_t JoinRequestText = 418;  // 0x76dbbb4, con el nombre
constexpr std::size_t JoinQuestionText = 429; // 0x76dc898
constexpr std::size_t FirstJoinResultText = 503;
constexpr std::uint8_t LastJoinResult = 7;
constexpr std::size_t FirstLeaveResultText = 511;
constexpr std::uint8_t LastNumberedLeaveResult = 4;
constexpr std::size_t ListAgainText = 568; // 0x76e6b7c, el 53 con 5
constexpr std::size_t CreateResultTexts[] = {516, 0, 517, 518, 940, 941, 942};
// 0x4d74b0: el buffer de los renglones (sprintf sin tope).
constexpr std::size_t JoinLineBytes = 0x32;
// 0x4d7c30: los resultados que sacan del gremio, el que además borra la entrada y el que pide la lista.
constexpr std::uint8_t LeftGuild = 1;
constexpr std::uint8_t GuildRemoved = 4;
constexpr std::uint8_t AskTheListAgain = 5;
// 0x4d7ff0: el 56 con 1 cierra la ventana 2 y vacía el campo.
constexpr std::uint8_t GuildCreated = 1;
constexpr std::int32_t NoEditFieldLimit = 10; // 0x6c8e78
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x4d7b60: el bit del servidor conectado y lo que queda sin él.
constexpr std::uint8_t ConnectedBit = 0x80;
constexpr std::uint8_t ServerMask = 0x7f;
constexpr std::uint8_t NoServer = 0xff;

Core::Expected<const Render::Text::ITextMeasure*, std::string> MeasureOf(const GuildReceiveContext& context,
                                                                         std::string_view what)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string(what) + ": no hay con qué medir");
    return context.measure;
}

// Hasta el primer '\0', como lo leen strlen y sprintf.
std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

Core::Expected<std::string_view, std::string> TextOf(const GuildReceiveContext& context, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x555190(nombre vacío de la .bss, texto, tipo).
Core::Expected<void, std::string> AddMessage(const GuildReceiveContext& context, std::string_view text,
                                             std::int32_t type)
{
    auto measure = MeasureOf(context, "0x555190");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    return AddGameMessage(context.world.Runtime(), *measure.Value(), context.screen, {}, text, type);
}

Core::Expected<void, std::string> AddTextMessage(const GuildReceiveContext& context, std::size_t index,
                                                 std::int32_t type)
{
    auto text = TextOf(context, index);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return AddMessage(context, text.Value(), type);
}

// sprintf(buffer, Text.bmd[index], argumento) a un buffer de `bytes`: uno más largo pisa lo que sigue en la pila
// (Q-C1282).
Core::Expected<std::string, std::string> Formatted(const GuildReceiveContext& context, std::size_t index,
                                                   std::string_view argument, std::size_t bytes, std::string_view where)
{
    auto format = TextOf(context, index);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    const std::array<std::string_view, 1> arguments{argument};
    std::string line = UI::PlaceArguments(format.Value(), std::span(arguments).first(argument.data() ? 1 : 0));
    if (line.size() >= bytes)
        return Core::Unexpected(std::string(where) + ": el renglón no entra en su buffer de la pila (Q-C1282)");
    return line;
}

template <std::size_t Size> void CopyName(std::array<char, Size>& target, const GuildName& source)
{
    static_assert(Size == GuildNameSize + 1);
    for (std::size_t i = 0; i < GuildNameSize; ++i)
        target[i] = std::to_integer<char>(source[i]);
    target[GuildNameSize] = '\0';
}

ActorState& HeroOf(const GuildReceiveContext& context)
{
    return context.world.Actors()[context.world.Runtime().hero];
}

// +0x1f2 como lo lee MOVSX.
std::int16_t GuildIndexOf(const ActorState& actor)
{
    return static_cast<std::int16_t>(actor.word1f2);
}

Core::Expected<GuildTableEntry*, std::string> EntryOf(GuildState& guild, std::int16_t index, std::string_view where)
{
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(std::string(where) + ": el índice del gremio cae fuera de la tabla (0x787aa48)");
    return &guild.table[static_cast<std::size_t>(index)];
}

// El registro de los paneles con los ganchos del gremio, para abrir y cerrar como el original.
Core::Expected<bool, std::string> OpenThePanel(const GuildReceiveContext& context, UI::PanelId id)
{
    GamePanels panels(context.world, context.random);
    panels.LendTheGuild(context.world, context.send);
    return panels.Registry().Open(id);
}

Core::Expected<bool, std::string> CloseThePanel(const GuildReceiveContext& context, UI::PanelId id)
{
    GamePanels panels(context.world, context.random);
    panels.LendTheGuild(context.world, context.send);
    return panels.Registry().Close(id);
}

Core::Expected<void, std::string> Send(const GuildReceiveContext& context, Protocol::Outgoing packet,
                                       std::string_view where)
{
    if (!context.send)
        return Core::Unexpected(std::string(where) + ": el paquete del gremio sin el cable");
    context.send(std::move(packet));
    return {};
}

// 0x6600d0 con los renglones; ocupado, OutputDebugStringA y el aviso de 0x6d69a8 (tipo 1).
Core::Expected<std::uint32_t, std::string> OpenTheDialog(const GuildReceiveContext& context,
                                                         std::span<const std::string> lines, UI::PanelDialogOwner owner)
{
    auto measure = MeasureOf(context, "0x6600d0");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    WorldRuntime& runtime = context.world.Runtime();
    const auto opened = UI::OpenThePanelDialog(
        runtime.panels.delegated.panelDialog,
        UI::PanelDialogRequest{.lines = lines, .flags = UI::PanelDialogConfirm, .owner = owner, .placed = 0},
        *measure.Value(), runtime.hud.dcFont, context.now);
    if (opened == UI::PanelDialogOpening::Busy)
        return Core::Unexpected(std::string("0x6600f9: el aviso del diálogo ocupado (0x6d69a8, en EUC-KR) no está en "
                                            "Text.bmd"));
    if (opened != UI::PanelDialogOpening::Opened)
        return 0U;
    return UI::PanelDialogHandle(runtime.panels.delegated.panelDialog);
}

Core::Expected<void, std::string> ApplyJoinRequest(const JoinRequest& request, const GuildReceiveContext& context)
{
    GuildState& guild = context.world.Runtime().guild;
    guild.requester = request.key;
    // 0x4d74ec: 0x660340 distinto de 0, el diálogo ocupado: no.
    if (UI::PanelDialogHandle(context.world.Runtime().panels.delegated.panelDialog) != 0)
        return Send(context, WriteJoinAnswer(false, request.key), "0x4d754d");
    const std::size_t index = ActorWithKey(context.world, request.key);
    if (index >= ActorCount)
        return Core::Unexpected(std::string("0x4d74f8: sin el personaje, 0x5110d0 da 400 y sprintf lee el nombre fuera "
                                            "de los 400 registros"));
    auto first =
        Formatted(context, JoinRequestText, HeroNameOf(context.world.Actors()[index]), JoinLineBytes, "0x4d7514");
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    auto second = Formatted(context, JoinQuestionText, {}, JoinLineBytes, "0x4d7524");
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    const std::array<std::string, 2> lines{first.Value(), second.Value()};
    auto opened = OpenTheDialog(context, lines, UI::PanelDialogOwner::GuildJoin);
    if (!opened.HasValue())
        return Core::Unexpected(opened.Error());
    return {};
}

Core::Expected<void, std::string> ApplyJoinResult(const JoinResult& result, const GuildReceiveContext& context)
{
    if (result.result <= LastJoinResult)
    {
        if (auto added = AddTextMessage(context, FirstJoinResultText + result.result, UI::ErrorMessage);
            !added.HasValue())
            return added;
    }
    // 0x4d7a3f..: 0x6236f0(0) con cualquier resultado.
    UI::PushDialog(context.world.Runtime().dialogs, 0);
    return {};
}

Core::Expected<void, std::string> ApplyMemberList(const MemberList& list, const GuildReceiveContext& context)
{
    if (list.members.size() > GuildMemberSlots)
        return Core::Unexpected(std::string("0x4d7b60: más de 80 miembros pisan la tabla de los gremios (0x787aa48)"));
    GuildState& guild = context.world.Runtime().guild;
    guild.memberCount = list.count;
    guild.score = std::max(list.score, 0);
    GuildWindowState& window = guild.window;
    window.members.clear();
    window.unions.clear();
    CopyName(window.name88, list.name);
    for (std::size_t i = 0; i < list.members.size(); ++i)
    {
        const MemberEntry& entry = list.members[i];
        GuildMemberSlot& slot = guild.members[i];
        for (std::size_t j = 0; j < CharacterNameSize; ++j)
            slot.name[j] = std::to_integer<char>(entry.name[j]);
        slot.name[CharacterNameSize] = '\0';
        slot.number = entry.number;
        slot.server =
            (entry.server & ConnectedBit) == 0 ? NoServer : static_cast<std::uint8_t>(entry.server & ServerMask);
        slot.status = entry.status;
        window.members.push_back(slot);
    }
    return {};
}

Core::Expected<void, std::string> ApplyLeaveResult(const LeaveResult& result, const GuildReceiveContext& context)
{
    if (result.result <= LastNumberedLeaveResult)
    {
        if (auto added = AddTextMessage(context, FirstLeaveResultText + result.result, UI::ErrorMessage);
            !added.HasValue())
            return added;
    }
    else if (result.result == AskTheListAgain)
    {
        if (auto added = AddTextMessage(context, ListAgainText, UI::ErrorMessage); !added.HasValue())
            return added;
    }
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (result.result == LeftGuild || result.result == GuildRemoved)
    {
        ActorState& hero = HeroOf(context);
        if (result.result == GuildRemoved && GuildIndexOf(hero) != NoGuildIndex)
        {
            auto entry = EntryOf(guild, GuildIndexOf(hero), "0x4d7cdb");
            if (!entry.HasValue())
                return Core::Unexpected(entry.Error());
            hero.word1f2 = static_cast<std::uint16_t>(NoGuildIndex);
            entry.Value()->key = -1;
            entry.Value()->unionName[0] = '\0';
            entry.Value()->guildName[0] = '\0';
        }
        hero.guildStatus = 0xff;
        hero.byte370 = 0;
        guild.warOn = 0;
        guild.memberCount = -1;
        auto closed = CloseThePanel(context, UI::GuildPanel);
        if (!closed.HasValue())
            return Core::Unexpected(closed.Error());
        return {};
    }
    if (result.result == AskTheListAgain)
        return Send(context, WriteMemberListRequest(), "0x4d7d95");
    return {};
}

Core::Expected<void, std::string> ApplyCreateResult(const CreateResult& result, const GuildReceiveContext& context)
{
    if (result.result != GuildCreated)
    {
        if (result.result >= std::size(CreateResultTexts))
            return {};
        return AddTextMessage(context, CreateResultTexts[result.result], UI::ErrorMessage);
    }
    // 0x4d808f..0x4d80cf: los diez primeros bytes del buffer del mensaje (0x7798bf8..0x7798c01: dos DWORD y un WORD) y
    // su largo (0x7758590) en 0, 0x6c8e78 en 10 (el tope de la entrada sin EDIT, que MuRet no guarda), la ventana 2
    // cerrada (0x65cae0) y los botones (0x77f3b10 y 0x6c8dcc).
    WorldRuntime& runtime = context.world.Runtime();
    constexpr std::size_t ClearedBytes = 10;
    std::fill_n(runtime.chat.input.messageBuffer.begin(), ClearedBytes, '\0');
    static_cast<void>(NoEditFieldLimit);
    runtime.chat.input.messageLength = 0;
    auto closed = CloseThePanel(context, UI::SecondGuildPanel);
    if (!closed.HasValue())
        return Core::Unexpected(closed.Error());
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    return {};
}

Core::Expected<void, std::string> ApplyViewportGuildDelete(const ViewportGuildDelete& deleted,
                                                           const GuildReceiveContext& context)
{
    const std::size_t index = ActorWithKey(context.world, deleted.key);
    if (index >= ActorCount)
        return Core::Unexpected(std::string("0x4e4306: sin el personaje, 0x5110d0 da 400 y el 5D escribe fuera de los "
                                            "400 registros"));
    context.world.Runtime().guild.memberCount = -1;
    ActorState& actor = context.world.Actors()[index];
    actor.word1f2 = static_cast<std::uint16_t>(NoGuildIndex);
    actor.guildStatus = 0xff;
    actor.byte369 = 0;
    actor.dword36c = 0;
    actor.byte370 = 0;
    auto closed = CloseThePanel(context, UI::GuildPanel);
    if (!closed.HasValue())
        return Core::Unexpected(closed.Error());
    return {};
}

Core::Expected<void, std::string> ApplyGuildInfo(const GuildInfo& info, const GuildReceiveContext& context)
{
    GuildState& guild = context.world.Runtime().guild;
    const auto index = FindGuild(guild, info.key);
    if (!index)
        return Core::Unexpected(std::string("0x447b00: la búsqueda pasa de las 2.000 entradas de la tabla"));
    if (*index < 0)
        return {};
    GuildTableEntry& entry = guild.table[static_cast<std::size_t>(*index)];
    CopyName(entry.unionName, info.unionName);
    CopyName(entry.guildName, info.guildName);
    // 0x447bd8..: el nibble alto del byte i / 2 en los pares y el bajo en los impares.
    constexpr std::uint8_t NibbleShift = 4;
    constexpr std::uint8_t LowNibble = 0x0f;
    for (std::size_t i = 0; i < GuildMarkNibbles; ++i)
    {
        const std::uint8_t packed = info.mark[i / 2];
        entry.mark[i] = (i % 2) == 0 ? static_cast<std::uint8_t>(packed >> NibbleShift)
                                     : static_cast<std::uint8_t>(packed & LowNibble);
    }
    return {};
}

} // namespace

Core::Expected<void, std::string> ApplyGuildPacket(const GuildPacket& packet, const GuildReceiveContext& context)
{
    return std::visit(
        [&context](const auto& received) -> Core::Expected<void, std::string>
        {
            using Received = std::decay_t<decltype(received)>;
            if constexpr (std::is_same_v<Received, JoinRequest>)
                return ApplyJoinRequest(received, context);
            else if constexpr (std::is_same_v<Received, JoinResult>)
                return ApplyJoinResult(received, context);
            else if constexpr (std::is_same_v<Received, MemberList>)
                return ApplyMemberList(received, context);
            else if constexpr (std::is_same_v<Received, LeaveResult>)
                return ApplyLeaveResult(received, context);
            else if constexpr (std::is_same_v<Received, MasterQuestion>)
            {
                // 0x4e4279: 0x6599a0(0x40000, 0).
                auto opened = OpenThePanel(context, UI::SecondGuildPanel);
                if (!opened.HasValue())
                    return Core::Unexpected(opened.Error());
                return {};
            }
            else if constexpr (std::is_same_v<Received, CreateResult>)
                return ApplyCreateResult(received, context);
            else if constexpr (std::is_same_v<Received, ViewportGuildDelete>)
                return ApplyViewportGuildDelete(received, context);
            else
                return ApplyGuildInfo(received, context);
        },
        packet);
}
} // namespace Client::Scenes
