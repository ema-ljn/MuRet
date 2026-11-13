#include "Scenes/GuildReceive.h"

#include "Core/Expected.h"
#include "Protocol/Guild.h"
#include "Protocol/Movement.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/Emote.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/GuildWindow.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/Emotes.h"
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
constexpr std::size_t FirstWarRequestText = 519;
constexpr std::uint8_t LastWarRequestResult = 6;
constexpr std::size_t WarStartText = 526;      // 0x76e3a44
constexpr std::size_t TypedWarStartText = 533; // 0x76e4278
constexpr std::size_t WarEndTexts[] = {527, 528, 529, 530, 531, 532, 480};
constexpr std::size_t UnionListEmptyText = 1328; // 0x771e61c, el EB con 0
// E5 (0x6565e0): la alianza (1) y la rivalidad (2), con el pedido 1 o el otro, y la pregunta.
constexpr std::size_t UnionRequestText = 1280;
constexpr std::size_t UnionJoinText = 1281;
constexpr std::size_t UnionOtherText = 1282;
constexpr std::size_t RelationQuestionText = 1283;
constexpr std::size_t RivalRequestText = 1284;
constexpr std::size_t RivalOtherText = 1285;
constexpr std::size_t RivalJoinText = 1286;
// 0x6c845c y 0x6c8470: lo que queda en el buffer sin texto.
constexpr std::string_view InvalidAssign = "Invalid GuildAssign";
constexpr std::string_view InvalidRelationship = "Invalid GuildRelationShipResult";
// 0x4d74b0 y 0x6565e0: los buffers de los renglones (sprintf sin tope) y cuántos son.
constexpr std::size_t JoinLineBytes = 0x32;
constexpr std::size_t RelationLineBytes = 0x40;
// 0x4d7c30: los resultados que sacan del gremio, el que además borra la entrada y el que pide la lista.
constexpr std::uint8_t LeftGuild = 1;
constexpr std::uint8_t GuildRemoved = 4;
constexpr std::uint8_t AskTheListAgain = 5;
// 0x4d7ff0: el 56 con 1 cierra la ventana 2 y vacía el campo.
constexpr std::uint8_t GuildCreated = 1;
constexpr std::int32_t NoEditFieldLimit = 10; // 0x6c8e78
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x4d8150: el resultado que no toca a los personajes.
constexpr std::uint8_t WarRequested = 1;
// 0x4e43b5 y 0x4d82b2: el tipo que prende 0x576796d.
constexpr std::uint8_t TypedWar = 1;
// +0x374 de cada personaje: 0 otro gremio, 1 el del héroe, 2 el rival.
constexpr std::uint8_t OtherGuild = 0;
constexpr std::uint8_t HeroGuild = 1;
constexpr std::uint8_t RivalGuild = 2;
// 0x4d832a..0x4d8369: la búsqueda del rival mira de la entrada 0 a la 1998 (EDI < 0x78a597d).
constexpr std::size_t RivalSearched = GuildTableSize - 1;
// 0x4d8467 y 0x4d8a2d/0x4d8e11: los gestos y sus acciones.
constexpr std::uint8_t WarStartAnimation = 0xc0;
constexpr std::uint8_t WarStartAction = 0x94;
constexpr std::uint8_t WarWonAnimation = 0xa7;
constexpr std::uint8_t WarWonAction = 0x89;
constexpr std::uint8_t WarLostAnimation = 0xad;
constexpr std::uint8_t WarLostAction = 0x8d;
// 0x4d8870: el gesto de cada resultado: 0 el de ganar, 2 el de perder, 1 ninguno.
constexpr std::int32_t WonGesture = 0;
constexpr std::int32_t NoGesture = 1;
constexpr std::int32_t LostGesture = 2;
// 0x4d9940: rompe la alianza (la relación 1, el pedido 2, el resultado 1).
constexpr std::uint8_t UnionRelation = 1;
constexpr std::uint8_t RivalRelation = 2;
constexpr std::uint8_t BreakRequest = 2;
// 0x4d9e5f y 0x4d9b80.
constexpr std::uint8_t ListResultOk = 1;
constexpr std::uint8_t RefreshKind = 1;
constexpr std::uint8_t RefreshEmpty = 0;
constexpr std::uint8_t RefreshAgain = 1;
// 0x4d9bb7: la cuenta de la lista de la alianza desde la que pide la lista otra vez.
constexpr std::size_t UnionRowsBeforeAsking = 2;
// 0x4d9940 con el índice −1: 0x787aa4c − 0x58 = 0x787a9f4, el primer byte del miembro 74 (Q-C1281,
// GuildState::MemberBeforeTheTable).
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

// 0x554610(texto, 1): el aviso resaltado.
Core::Expected<void, std::string> AddHighlightedNotice(const GuildReceiveContext& context, std::string_view text)
{
    auto measure = MeasureOf(context, "0x554610");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    WorldRuntime& runtime = context.world.Runtime();
    UI::AddNotice(runtime.chat.area, *measure.Value(), runtime.hud.dcFont, text, 1);
    return {};
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

template <std::size_t Size> std::string_view NameOf(const std::array<char, Size>& name)
{
    return CString(std::string_view(name.data(), name.size()));
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

// 0x4d8150, 0x4d8260 y 0x4d8870: +0x374 de cada personaje vivo: 1 los del gremio del héroe (con gremio), 2 los del
// rival (solo el 62, con `rival`), 0 los demás.
void MarkTheGuilds(WorldState& world, std::optional<std::int32_t> rival)
{
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    for (std::size_t index = 0; index < ActorCount; ++index)
    {
        ActorState& actor = world.Actors()[index];
        if (!actor.alive)
            continue;
        // El héroe es uno de los 400: su gremio se lee de nuevo en cada vuelta.
        const std::int16_t heroGuild = GuildIndexOf(hero);
        actor.byte374 = OtherGuild;
        if (heroGuild != NoGuildIndex && GuildIndexOf(actor) == heroGuild)
            actor.byte374 = HeroGuild;
        if (rival && static_cast<std::int32_t>(GuildIndexOf(actor)) == *rival)
            actor.byte374 = RivalGuild;
    }
}

// 0x576a60(héroe, héroe, animación, acción) (Scenes::PlayTheEmote: parado, la animación y un C1 18) y después, haya
// actuado o no, el C1 05 18 propio con la acción y el sector de +0x24 del héroe (0x4d84f5..0x4d851f, la cuenta de
// 0x59b456). Con el héroe parado salen dos C1 18 iguales, como en un emote (Q-C776).
Core::Expected<void, std::string> Gesture(const GuildReceiveContext& context, std::uint8_t animation,
                                          std::uint8_t action, std::string_view where)
{
    if (context.models == nullptr)
        return Core::Unexpected(std::string(where) + ": 0x576a60, el gesto, sin los modelos (0x4ea030)");
    ActionSender send;
    if (context.send)
    {
        send = [&context](std::uint8_t direction, std::uint8_t sent)
        { context.send(Protocol::Movement::WriteAction(direction, sent)); };
    }
    ActorState& hero = HeroOf(context);
    const EmoteContext emote{.hero = hero, .models = *context.models, .send = send};
    if (auto played = PlayTheEmote(hero, UI::EmoteCase{animation, action}, emote); !played.HasValue())
        return Core::Unexpected(played.Error());
    return SendTheAction(hero.angles.z, action, send, where);
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
    ClearTheGuildMembers(window);
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
        AddTheGuildMember(context.world, slot);
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

Core::Expected<void, std::string> ApplyWarRequestResult(const WarRequestResult& result,
                                                        const GuildReceiveContext& context)
{
    if (result.result <= LastWarRequestResult)
    {
        if (auto added = AddTextMessage(context, FirstWarRequestText + result.result, UI::ErrorMessage);
            !added.HasValue())
            return added;
    }
    GuildState& guild = context.world.Runtime().guild;
    if (result.result == WarRequested || guild.warOn != 0)
        return {};
    guild.warOn = 0;
    guild.rivalIndex = -1;
    guild.rival[0] = '\0';
    MarkTheGuilds(context.world, std::nullopt);
    return {};
}

Core::Expected<void, std::string> ApplyWarRequest(const WarRequest& request, const GuildReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    CopyName(guild.rival, request.name);
    UI::PushDialog(runtime.dialogs, UI::IgnoredOverChaosCastle);
    if (request.type == TypedWar)
        guild.warType = 1;
    return {};
}

Core::Expected<void, std::string> ApplyWarStart(const WarStart& start, const GuildReceiveContext& context)
{
    GuildState& guild = context.world.Runtime().guild;
    CopyName(guild.rival, start.name);
    guild.warOn = 1;
    const std::size_t format = start.type == 0 ? WarStartText : TypedWarStartText;
    if (start.type != 0)
        guild.warType = 1;
    // 0x4d82c6: sprintf a un buffer de 100.
    constexpr std::size_t NoticeBytes = 100;
    auto line = Formatted(context, format, NameOf(guild.rival), NoticeBytes, "0x4d82c6");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    if (auto noticed = AddHighlightedNotice(context, line.Value()); !noticed.HasValue())
        return noticed;
    guild.warTeam = start.team;
    // 0x4d830b..0x4d8369: strcmp de +0xd de cada entrada con el nombre del paquete y su cero; sin ninguna, queda el
    // índice que había.
    const std::string_view name = NameOf(guild.rival);
    for (std::size_t index = 0; index < RivalSearched; ++index)
    {
        if (NameOf(guild.table[index].guildName) == name)
        {
            guild.rivalIndex = static_cast<std::int32_t>(index);
            break;
        }
    }
    if (guild.rivalIndex >= 0)
        MarkTheGuilds(context.world, guild.rivalIndex);
    return Gesture(context, WarStartAnimation, WarStartAction, "0x4d8473");
}

Core::Expected<void, std::string> ApplyWarEnd(const WarEnd& end, const GuildReceiveContext& context)
{
    if (end.result >= std::size(WarEndTexts))
        return Core::Unexpected(std::string("0x4d894f: con un resultado mayor que 6, 0x554610 muestra el buffer de la "
                                            "pila sin inicializar"));
    auto text = TextOf(context, WarEndTexts[end.result]);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    // 0x4d88ba: sprintf sin argumentos.
    const std::string line = UI::PlaceArguments(text.Value(), {});
    std::int32_t gesture = WonGesture;
    if (end.result == 1 || end.result == 2 || end.result == 4)
        gesture = LostGesture;
    else if (end.result == 6)
        gesture = NoGesture;
    GuildState& guild = context.world.Runtime().guild;
    guild.byte5767816 = 0;
    if (auto noticed = AddHighlightedNotice(context, line); !noticed.HasValue())
        return noticed;
    guild.warOn = 0;
    guild.warType = 0;
    guild.rivalIndex = -1;
    guild.rival[0] = '\0';
    // 0x4d898c..0x4d8a13: con la guerra apagada, el rival no se busca.
    MarkTheGuilds(context.world, std::nullopt);
    if (gesture == WonGesture)
        return Gesture(context, WarWonAnimation, WarWonAction, "0x4d8e1b");
    if (gesture == LostGesture)
        return Gesture(context, WarLostAnimation, WarLostAction, "0x4d8a39");
    return {};
}

Core::Expected<void, std::string> ApplyWarScore(const WarScore& score, const GuildReceiveContext& context)
{
    GuildState& guild = context.world.Runtime().guild;
    guild.warOn = 1;
    guild.ourScore = score.ours;
    guild.theirScore = score.theirs;
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

// 0x4d96f4..0x4d976b: con la guerra (0x576796c) y sin el índice del rival (0x6c82f0 en −1), el 65 lo busca con el
// nombre (0x5767804), si hay, de la entrada 0 a la 1998, y deja el que encuentra o −1, como no hace el 62. Devuelve el
// índice con el que marca, o nada sin nombre.
std::optional<std::int32_t> RivalForTheViewport(GuildState& guild)
{
    if (guild.rivalIndex == -1)
    {
        if (guild.rival[0] == '\0')
            return std::nullopt;
        const std::string_view name = NameOf(guild.rival);
        for (std::size_t index = 0; index < RivalSearched; ++index)
        {
            if (NameOf(guild.table[index].guildName) == name)
            {
                guild.rivalIndex = static_cast<std::int32_t>(index);
                break;
            }
        }
    }
    return guild.rivalIndex;
}

Core::Expected<void, std::string> ApplyViewportGuild(const ViewportGuild& viewport, const GuildReceiveContext& context)
{
    WorldState& world = context.world;
    GuildState& guild = world.Runtime().guild;
    for (const ViewportGuildEntry& entry : viewport.entries)
    {
        const std::size_t index = ActorWithKey(world, entry.key);
        if (index >= ActorCount)
            return Core::Unexpected(std::string("0x4d92b2: sin el personaje, 0x5110d0 da 400 y el 65 escribe fuera de "
                                                "los 400 registros"));
        ActorState& actor = world.Actors()[index];
        actor.guildStatus = entry.status;
        actor.byte369 = entry.byte369;
        actor.byte370 = entry.relation;
        // 0x4d92ef: 0x447b30 (0x447b00 distinto de −1) y 0x447b00 otra vez; sin el gremio, el C1 08 66 que lo pide y
        // 0x447b50, que lo agrega (−1 con la tabla llena).
        const auto found = FindGuild(guild, entry.guildKey);
        if (!found)
            return Core::Unexpected(std::string("0x447b00: la búsqueda pasa de las 2.000 entradas de la tabla"));
        std::int32_t guildIndex = *found;
        if (guildIndex == -1)
        {
            if (auto sent = Send(context, WriteGuildInfoRequest(entry.guildKey), "0x4d95c6"); !sent.HasValue())
                return sent;
            guildIndex = AddGuild(guild, entry.guildKey);
        }
        actor.word1f2 = static_cast<std::uint16_t>(static_cast<std::int16_t>(guildIndex));
        // 0x4d96b9: con el personaje vivo (siempre: 0x5110d0 solo encuentra a los vivos), +0x374 en 0; 1 con el gremio
        // del héroe (leído otra vez de 0x743a170); 2 con el del rival, en guerra.
        if (actor.alive)
        {
            actor.byte374 = OtherGuild;
            const std::int16_t heroGuild = GuildIndexOf(HeroOf(context));
            if (heroGuild != NoGuildIndex && GuildIndexOf(actor) == heroGuild)
                actor.byte374 = HeroGuild;
            if (guild.warOn != 0)
            {
                const std::optional<std::int32_t> rival = RivalForTheViewport(guild);
                if (rival && *rival >= 0 && static_cast<std::int32_t>(GuildIndexOf(actor)) == *rival)
                    actor.byte374 = RivalGuild;
            }
        }
        // 0x4d9772: el del héroe, 0x412120(+0x1f2, +0x368).
        if (index == world.Runtime().hero)
        {
            guild.heroGuildIndex = GuildIndexOf(actor);
            guild.heroGuildStatus = actor.guildStatus;
        }
    }
    return {};
}

Core::Expected<void, std::string> ApplyViewportUnion(const ViewportUnion& viewport, const GuildReceiveContext& context)
{
    WorldState& world = context.world;
    GuildState& guild = world.Runtime().guild;
    for (const ViewportUnionEntry& entry : viewport.entries)
    {
        // 0x4e44c6..0x4e44e5: el nombre de la alianza en +4..+0xc de la entrada de 0x447b00, aunque sea −1: entonces
        // va a los bytes 0 a 8 del nombre del miembro 74 (Q-C1281, Q-C1284).
        const auto found = FindGuild(guild, entry.guildKey);
        if (!found)
            return Core::Unexpected(std::string("0x447b00: la búsqueda pasa de las 2.000 entradas de la tabla"));
        std::span<char> target = *found == -1
                                     ? std::span<char>(guild.members[MemberBeforeTheTable].name)
                                     : std::span<char>(guild.table[static_cast<std::size_t>(*found)].unionName);
        for (std::size_t i = 0; i < GuildNameSize; ++i)
            target[i] = std::to_integer<char>(entry.unionName[i]);
        target[GuildNameSize] = '\0';
        const std::size_t index = ActorWithKey(world, entry.key);
        if (index >= ActorCount)
            return Core::Unexpected(std::string("0x4e44f5: sin el personaje, 0x5110d0 da 400 y el 67 escribe fuera de "
                                                "los 400 registros"));
        world.Actors()[index].byte370 = entry.relation;
    }
    return {};
}

Core::Expected<void, std::string> ApplyAssignResult(const AssignResult& result, const GuildReceiveContext& context)
{
    std::optional<std::size_t> index;
    if (result.result == 1)
    {
        constexpr std::array<std::size_t, 3> Assigned{1374, 1375, 1376};
        if (result.type >= 1 && result.type <= Assigned.size())
            index = Assigned[result.type - 1u];
    }
    else
    {
        switch (result.result)
        {
        case 0x10:
            index = 522;
            break;
        case 0x11:
            index = 1386;
            break;
        case 0x12:
            index = 1326;
            break;
        case 0x13:
            index = 1327;
            break;
        default:
            break;
        }
    }
    if (!index)
        return AddMessage(context, InvalidAssign, UI::SystemMessage);
    return AddTextMessage(context, *index, UI::SystemMessage);
}

Core::Expected<void, std::string> ApplyRelationshipRequest(const RelationshipRequest& request,
                                                           const GuildReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    // 0x656603: con el diálogo ocupado, no (C1 08 E6 con 0).
    if (UI::PanelDialogHandle(runtime.panels.delegated.panelDialog) != 0)
        return Send(context,
                    WriteRelationshipAnswer(request.relation, request.request, 0, request.keyHigh, request.keyLow),
                    "0x656616");
    guild.relationRequest = request.request;
    guild.relation = request.relation;
    guild.relationKeyHigh = request.keyHigh;
    guild.relationKeyLow = request.keyLow;
    const auto key = static_cast<std::uint16_t>((request.keyHigh << 8) | request.keyLow);
    const std::size_t index = ActorWithKey(context.world, key);
    if (index >= ActorCount)
        return {};
    if (request.relation != UnionRelation && request.relation != RivalRelation)
        return Core::Unexpected(std::string("0x656db1: con una relación que no es 1 ni 2, 0x6600d0 abre el diálogo con "
                                            "los buffers de la pila sin inicializar"));
    const bool isUnion = request.relation == UnionRelation;
    const std::size_t asked = isUnion ? UnionRequestText : RivalRequestText;
    std::size_t detail = 0;
    if (isUnion)
        detail = request.request == 1 ? UnionJoinText : UnionOtherText;
    else
        detail = request.request == 1 ? RivalJoinText : RivalOtherText;
    auto first = Formatted(context, asked, HeroNameOf(context.world.Actors()[index]), RelationLineBytes, "0x656cd2");
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    auto second = Formatted(context, detail, {}, RelationLineBytes, "0x656d8a");
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    auto third = Formatted(context, RelationQuestionText, {}, RelationLineBytes, "0x656d9f");
    if (!third.HasValue())
        return Core::Unexpected(third.Error());
    const std::array<std::string, 3> lines{first.Value(), second.Value(), third.Value()};
    auto opened = OpenTheDialog(context, lines, UI::PanelDialogOwner::Relationship);
    if (!opened.HasValue())
        return Core::Unexpected(opened.Error());
    guild.secondWindow.dialog2a4 = opened.Value();
    return {};
}

Core::Expected<void, std::string> ApplyRelationshipResult(const RelationshipResult& result,
                                                          const GuildReceiveContext& context)
{
    std::optional<std::size_t> index;
    if (result.result == 1)
    {
        if (result.relation == UnionRelation)
        {
            if (result.request == 1)
                index = 1381;
            else if (result.request == BreakRequest)
                index = 1382;
            else if (result.request == 0x10)
                index = 1635;
        }
        else
        {
            index = result.request == 1 ? 1383 : 1384;
        }
    }
    else if (result.result == 0)
    {
        index = 1328;
    }
    else
    {
        // 0x4d9a04: el switch de pkt[5].
        constexpr std::array<std::pair<std::uint8_t, std::size_t>, 13> Results{
            {{std::uint8_t{0x10}, std::size_t{1637}},
             {std::uint8_t{0x11}, std::size_t{1386}},
             {std::uint8_t{0x15}, std::size_t{1250}},
             {std::uint8_t{0x16}, std::size_t{1251}},
             {std::uint8_t{0x17}, std::size_t{1252}},
             {std::uint8_t{0x18}, std::size_t{1253}},
             {std::uint8_t{0x19}, std::size_t{1254}},
             {std::uint8_t{0x1a}, std::size_t{1255}},
             {std::uint8_t{0x1b}, std::size_t{1333}},
             {std::uint8_t{0x1c}, std::size_t{1329}},
             {std::uint8_t{0x1d}, std::size_t{1331}},
             {std::uint8_t{0x1e}, std::size_t{1287}},
             {std::uint8_t{0x20}, std::size_t{1268}}}};
        for (const auto& [code, text] : Results)
        {
            if (code == result.result)
                index = text;
        }
    }
    const auto added = index ? AddTextMessage(context, *index, UI::SystemMessage)
                             : AddMessage(context, InvalidRelationship, UI::SystemMessage);
    if (!added.HasValue())
        return added;
    if (result.key != context.playerKey || result.result != 1 || result.relation != UnionRelation ||
        result.request != BreakRequest)
        return {};
    // 0x4d9b2b: el primer byte del nombre de la alianza del gremio del héroe. Con −1, el del miembro 74 (Q-C1281).
    GuildState& guild = context.world.Runtime().guild;
    const std::int16_t heroGuild = GuildIndexOf(HeroOf(context));
    if (heroGuild == NoGuildIndex)
    {
        guild.members[MemberBeforeTheTable].name[0] = '\0';
        return {};
    }
    auto entry = EntryOf(guild, heroGuild, "0x4d9b2b");
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    entry.Value()->unionName[0] = '\0';
    return {};
}

Core::Expected<void, std::string> ApplyUnionList(const UnionList& list, const GuildReceiveContext& context)
{
    if (list.result != ListResultOk)
        return {};
    GuildWindowState& window = context.world.Runtime().guild.window;
    for (const UnionEntry& entry : list.entries)
    {
        GuildUnionRow row;
        constexpr std::uint8_t NibbleShift = 4;
        constexpr std::uint8_t LowNibble = 0x0f;
        for (std::size_t i = 0; i < GuildMarkNibbles; ++i)
        {
            const std::uint8_t packed = entry.mark[i / 2];
            row.mark[i] = (i % 2) == 0 ? static_cast<std::uint8_t>(packed >> NibbleShift)
                                       : static_cast<std::uint8_t>(packed & LowNibble);
        }
        CopyName(row.name, entry.name);
        row.byte0 = entry.byte0;
        window.unions.push_back(row);
    }
    return {};
}

Core::Expected<void, std::string> ApplyUnionListRefresh(const UnionListRefresh& refresh,
                                                        const GuildReceiveContext& context)
{
    if (refresh.kind != RefreshKind)
        return {};
    if (refresh.result == RefreshEmpty)
        return AddTextMessage(context, UnionListEmptyText, UI::SystemMessage);
    if (refresh.result != RefreshAgain)
        return {};
    GuildWindowState& window = context.world.Runtime().guild.window;
    // 0x4d9bb2: la vtable + 0x9c de la lista (*inferido*: cuántas filas tiene). Con más de 2, Sleep(500), que MuRet no
    // hace (P-C282), y el C1 03 E9.
    if (window.unions.size() > UnionRowsBeforeAsking)
    {
        if (auto sent = Send(context, WriteUnionListRequest(), "0x4d9bc8"); !sent.HasValue())
            return sent;
    }
    window.unions.clear();
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
            else if constexpr (std::is_same_v<Received, WarRequestResult>)
                return ApplyWarRequestResult(received, context);
            else if constexpr (std::is_same_v<Received, WarRequest>)
                return ApplyWarRequest(received, context);
            else if constexpr (std::is_same_v<Received, WarStart>)
                return ApplyWarStart(received, context);
            else if constexpr (std::is_same_v<Received, WarEnd>)
                return ApplyWarEnd(received, context);
            else if constexpr (std::is_same_v<Received, WarScore>)
                return ApplyWarScore(received, context);
            else if constexpr (std::is_same_v<Received, GuildInfo>)
                return ApplyGuildInfo(received, context);
            else if constexpr (std::is_same_v<Received, AssignResult>)
                return ApplyAssignResult(received, context);
            else if constexpr (std::is_same_v<Received, RelationshipRequest>)
                return ApplyRelationshipRequest(received, context);
            else if constexpr (std::is_same_v<Received, RelationshipResult>)
                return ApplyRelationshipResult(received, context);
            else if constexpr (std::is_same_v<Received, UnionList>)
                return ApplyUnionList(received, context);
            else if constexpr (std::is_same_v<Received, ViewportGuild>)
                return ApplyViewportGuild(received, context);
            else if constexpr (std::is_same_v<Received, ViewportUnion>)
                return ApplyViewportUnion(received, context);
            else
                return ApplyUnionListRefresh(received, context);
        },
        packet);
}
} // namespace Client::Scenes
