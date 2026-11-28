#include "Guild/GuildReceive.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Actors/Emote.h"
#include "Events/EventGlobals.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "CastleSiege/CastleWindows.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Guild/GuildWindow.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Chat/ChatRuntime.h"
#include "Chat/ChatInput.h"

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

namespace Client::Guild
{
namespace
{
using namespace Protocol::Guild;

// 0x5110d0: the 400 it walks; with 400 it found nobody.
constexpr std::size_t ActorCount = 400;
// Text.bmd (the address − 0x76bd1dc, / 300).
constexpr std::size_t JoinRequestText = 418;  // 0x76dbbb4, with the name
constexpr std::size_t JoinQuestionText = 429; // 0x76dc898
constexpr std::size_t FirstJoinResultText = 503;
constexpr std::uint8_t LastJoinResult = 7;
constexpr std::size_t FirstLeaveResultText = 511;
constexpr std::uint8_t LastNumberedLeaveResult = 4;
constexpr std::size_t ListAgainText = 568; // 0x76e6b7c, the 53 with 5
constexpr std::size_t CreateResultTexts[] = {516, 0, 517, 518, 940, 941, 942};
constexpr std::size_t FirstWarRequestText = 519;
constexpr std::uint8_t LastWarRequestResult = 6;
constexpr std::size_t WarStartText = 526;      // 0x76e3a44
constexpr std::size_t TypedWarStartText = 533; // 0x76e4278
constexpr std::size_t WarEndTexts[] = {527, 528, 529, 530, 531, 532, 480};
constexpr std::size_t UnionListEmptyText = 1328; // 0x771e61c, the EB with 0
// E5 (0x6565e0): the union (1) and the rivalry (2), with request 1 or the other, and the question.
constexpr std::size_t UnionRequestText = 1280;
constexpr std::size_t UnionJoinText = 1281;
constexpr std::size_t UnionOtherText = 1282;
constexpr std::size_t RelationQuestionText = 1283;
constexpr std::size_t RivalRequestText = 1284;
constexpr std::size_t RivalOtherText = 1285;
constexpr std::size_t RivalJoinText = 1286;
// 0x6c845c and 0x6c8470: what stays in the buffer without a text.
constexpr std::string_view InvalidAssign = "Invalid GuildAssign";
constexpr std::string_view InvalidRelationship = "Invalid GuildRelationShipResult";
// 0x4d74b0 and 0x6565e0: the row buffers (sprintf without a limit) and how many there are.
constexpr std::size_t JoinLineBytes = 0x32;
constexpr std::size_t RelationLineBytes = 0x40;
// 0x4d7c30: the results that take out of the guild, the one that also erases the entry and the one that asks for the
// list.
constexpr std::uint8_t LeftGuild = 1;
constexpr std::uint8_t GuildRemoved = 4;
constexpr std::uint8_t AskTheListAgain = 5;
// 0x4d7ff0: the 56 with 1 closes window 2 and empties the field.
constexpr std::uint8_t GuildCreated = 1;
constexpr std::int32_t NoEditFieldLimit = 10; // 0x6c8e78
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x4d8150: the result that does not touch the characters.
constexpr std::uint8_t WarRequested = 1;
// 0x4e43b5 and 0x4d82b2: the type that turns 0x576796d on.
constexpr std::uint8_t TypedWar = 1;
// +0x374 of each character: 0 another guild, 1 the hero's, 2 the rival.
constexpr std::uint8_t OtherGuild = 0;
constexpr std::uint8_t HeroGuild = 1;
constexpr std::uint8_t RivalGuild = 2;
// 0x4d832a..0x4d8369: the rival search looks from entry 0 to 1998 (EDI < 0x78a597d).
constexpr std::size_t RivalSearched = GuildTableSize - 1;
// 0x4d8467 and 0x4d8a2d/0x4d8e11: the gestures and their actions.
constexpr std::uint8_t WarStartAnimation = 0xc0;
constexpr std::uint8_t WarStartAction = 0x94;
constexpr std::uint8_t WarWonAnimation = 0xa7;
constexpr std::uint8_t WarWonAction = 0x89;
constexpr std::uint8_t WarLostAnimation = 0xad;
constexpr std::uint8_t WarLostAction = 0x8d;
// 0x4d8870: each result's gesture: 0 the winning one, 2 the losing one, 1 none.
constexpr std::int32_t WonGesture = 0;
constexpr std::int32_t NoGesture = 1;
constexpr std::int32_t LostGesture = 2;
// 0x4d9940: breaks the union (relation 1, request 2, result 1).
constexpr std::uint8_t UnionRelation = 1;
constexpr std::uint8_t RivalRelation = 2;
constexpr std::uint8_t BreakRequest = 2;
// 0x4d9e5f and 0x4d9b80.
constexpr std::uint8_t ListResultOk = 1;
constexpr std::uint8_t RefreshKind = 1;
constexpr std::uint8_t RefreshEmpty = 0;
constexpr std::uint8_t RefreshAgain = 1;
// 0x4d9bb7: the union list's count from which it asks for the list again.
constexpr std::size_t UnionRowsBeforeAsking = 2;
// 0x4d9940 with index −1: 0x787aa4c − 0x58 = 0x787a9f4, the first byte of member 74 (Q-C1281,
// GuildState::MemberBeforeTheTable).
// 0x4d7b60: the connected-server bit and what stays without it.
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

// Up to the first '\0', as strlen and sprintf read it.
std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

Core::Expected<std::string_view, std::string> TextOf(const GuildReceiveContext& context, std::size_t index)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x555190(the .bss empty name, text, type).
Core::Expected<void, std::string> AddMessage(const GuildReceiveContext& context, std::string_view text,
                                             std::int32_t type)
{
    auto measure = MeasureOf(context, "0x555190");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    return Chat::AddGameMessage(context.world.Runtime(), *measure.Value(), context.screen, {}, text, type);
}

Core::Expected<void, std::string> AddTextMessage(const GuildReceiveContext& context, std::size_t index,
                                                 std::int32_t type)
{
    auto text = TextOf(context, index);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return AddMessage(context, text.Value(), type);
}

// 0x554610(text, 1): the highlighted notice.
Core::Expected<void, std::string> AddHighlightedNotice(const GuildReceiveContext& context, std::string_view text)
{
    auto measure = MeasureOf(context, "0x554610");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::AddNotice(runtime.chat.area, *measure.Value(), runtime.hud.dcFont, text, 1);
    return {};
}

// sprintf(buffer, Text.bmd[index], argument) into a buffer of `bytes`: a longer one overruns what follows on the stack
// (Q-C1282).
Core::Expected<std::string, std::string> Formatted(const GuildReceiveContext& context, std::size_t index,
                                                   std::string_view argument, std::size_t bytes, std::string_view where)
{
    auto format = TextOf(context, index);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    const std::array<std::string_view, 1> arguments{argument};
    std::string line = Ui::PlaceArguments(format.Value(), std::span(arguments).first(argument.data() ? 1 : 0));
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

Kernel::ActorState& HeroOf(const GuildReceiveContext& context)
{
    return context.world.Hero();
}

// +0x1f2 as MOVSX reads it.
std::int16_t GuildIndexOf(const Kernel::ActorState& actor)
{
    return static_cast<std::int16_t>(actor.word1f2);
}

Core::Expected<GuildTableEntry*, std::string> EntryOf(GuildState& guild, std::int16_t index, std::string_view where)
{
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(std::string(where) + ": el índice del gremio cae fuera de la tabla (0x787aa48)");
    return &guild.table[static_cast<std::size_t>(index)];
}

// The panels' registry with the guild hooks, to open and close like the original.
Core::Expected<bool, std::string> OpenThePanel(const GuildReceiveContext& context, Ui::PanelId id)
{
    Hud::GamePanels panels(context.world, context.random);
    if (context.worldEntry != nullptr)
        CastleSiege::LendTheCastlePanelServices(panels, *context.worldEntry, context.send);
    else
        CastleSiege::LendTheCastlePanelServices(
            panels, context.world, context.random, context.send, nullptr,
            {.measure = context.measure, .texts = context.texts, .screen = context.screen, .now = context.now});
    panels.LendTheGuild(context.world, context.send);
    return panels.Registry().Open(id);
}

Core::Expected<bool, std::string> CloseThePanel(const GuildReceiveContext& context, Ui::PanelId id)
{
    Hud::GamePanels panels(context.world, context.random);
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

// 0x4d8150, 0x4d8260 and 0x4d8870: +0x374 of each live character: 1 those of the hero's guild (with a guild), 2 the
// rival's (only the 62, with `rival`), 0 the others.
void MarkTheGuilds(Kernel::WorldState& world, std::optional<std::int32_t> rival)
{
    const Kernel::ActorState& hero = world.Hero();
    for (std::size_t index = 0; index < ActorCount; ++index)
    {
        Kernel::ActorState& actor = world.Actors()[index];
        if (!actor.alive)
            continue;
        // The hero is one of the 400: its guild is read again on every pass.
        const std::int16_t heroGuild = GuildIndexOf(hero);
        actor.byte374 = OtherGuild;
        if (heroGuild != NoGuildIndex && GuildIndexOf(actor) == heroGuild)
            actor.byte374 = HeroGuild;
        if (rival && static_cast<std::int32_t>(GuildIndexOf(actor)) == *rival)
            actor.byte374 = RivalGuild;
    }
}

// 0x576a60(hero, hero, animation, action) (Actors::PlayTheEmote: standing, the animation and a C1 18) and then,
// whether it acted or not, the own C1 05 18 with the action and the hero's +0x24 sector (0x4d84f5..0x4d851f,
// 0x59b456's arithmetic). With the hero standing two equal C1 18 go out, as in an emote (Q-C776).
Core::Expected<void, std::string> Gesture(const GuildReceiveContext& context, std::uint8_t animation,
                                          std::uint8_t action, std::string_view where)
{
    if (context.models == nullptr)
        return Core::Unexpected(std::string(where) + ": 0x576a60, el gesto, sin los modelos (0x4ea030)");
    Actors::ActionSender send;
    if (context.send)
    {
        send = [&context](std::uint8_t direction, std::uint8_t sent)
        { context.send(Protocol::Movement::WriteAction(direction, sent)); };
    }
    Kernel::ActorState& hero = HeroOf(context);
    const Actors::EmoteContext emote{.hero = hero, .models = *context.models, .send = send};
    if (auto played = Actors::PlayTheEmote(hero, Actors::EmoteCase{animation, action}, emote); !played.HasValue())
        return Core::Unexpected(played.Error());
    return Actors::SendTheAction(hero.angles.z, action, send, where);
}

// 0x6600d0 with the rows (busy, the notice of 0x6d69a8 and 0: the 50 and the E5 check 0x660340 before and do not get
// there).
Core::Expected<std::uint32_t, std::string> OpenTheDialog(const GuildReceiveContext& context,
                                                         std::span<const std::string> lines, Ui::PanelDialogOwner owner)
{
    auto measure = MeasureOf(context, "0x6600d0");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    return PopUpThePanelDialog(
        context.world.Runtime(),
        Ui::PanelDialogRequest{.lines = lines, .flags = Ui::PanelDialogConfirm, .owner = owner, .placed = 0},
        *measure.Value(), context.screen, context.now);
}

Core::Expected<void, std::string> ApplyJoinRequest(const JoinRequest& request, const GuildReceiveContext& context)
{
    GuildState& guild = context.world.Runtime().guild;
    // 0x4d74e2: the key to 0x78aba40 and, with the dialog busy (0x4d74ec: 0x660340 other than 0), not.
    if (Ui::PanelDialogHandle(context.world.Runtime().panels.delegated.panelDialog) != 0)
    {
        guild.requester = request.key;
        return Send(context, WriteJoinAnswer(false, request.key), "0x4d754d");
    }
    // 0x4d74f8: without the character, 0x5110d0 gives 400 and the original opens the dialog with the name it reads past
    // the 400 records (Q-C1297). MuRet drops the whole 50, without storing the key, like the 18 (Q-C778).
    const std::size_t index = Kernel::ActorWithKey(context.world, request.key);
    if (index >= ActorCount)
        return {};
    guild.requester = request.key;
    auto first = Formatted(context, JoinRequestText, Actors::HeroNameOf(context.world.Actors()[index]), JoinLineBytes,
                           "0x4d7514");
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    auto second = Formatted(context, JoinQuestionText, {}, JoinLineBytes, "0x4d7524");
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    const std::array<std::string, 2> lines{first.Value(), second.Value()};
    auto opened = OpenTheDialog(context, lines, Ui::PanelDialogOwner::GuildJoin);
    if (!opened.HasValue())
        return Core::Unexpected(opened.Error());
    return {};
}

Core::Expected<void, std::string> ApplyJoinResult(const JoinResult& result, const GuildReceiveContext& context)
{
    if (result.result <= LastJoinResult)
    {
        if (auto added = AddTextMessage(context, FirstJoinResultText + result.result, Ui::ErrorMessage);
            !added.HasValue())
            return added;
    }
    // 0x4d7a3f..: 0x6236f0(0) with any result.
    Ui::PushDialog(context.world.Runtime().dialogs, 0);
    return {};
}

Core::Expected<void, std::string> ApplyMemberList(const MemberList& list, const GuildReceiveContext& context)
{
    // 0x4d7bc2..0x4d7c1f: member 80 onward falls on the guild table (0x787aa48, Q-C1303). MuRet drops the whole 52.
    if (list.members.size() > GuildMemberSlots)
        return {};
    GuildState& guild = context.world.Runtime().guild;
    guild.memberCount = list.count;
    guild.score = std::max(list.score, 0);
    GuildWindowState& window = guild.window;
    ClearTheGuildMembers(window);
    // 0x4d7b96: 0x651830.
    ClearTheUnions(window);
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
        if (auto added = AddTextMessage(context, FirstLeaveResultText + result.result, Ui::ErrorMessage);
            !added.HasValue())
            return added;
    }
    else if (result.result == AskTheListAgain)
    {
        if (auto added = AddTextMessage(context, ListAgainText, Ui::ErrorMessage); !added.HasValue())
            return added;
    }
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (result.result == LeftGuild || result.result == GuildRemoved)
    {
        Kernel::ActorState& hero = HeroOf(context);
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
        auto closed = CloseThePanel(context, Ui::GuildPanel);
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
        return AddTextMessage(context, CreateResultTexts[result.result], Ui::ErrorMessage);
    }
    // 0x4d808f..0x4d80cf: the first ten bytes of the message buffer (0x7798bf8..0x7798c01: two DWORDs and a WORD) and
    // its length (0x7758590) at 0, 0x6c8e78 at 10 (the limit of the input without an EDIT, which MuRet does not store),
    // window 2 closed (0x65cae0) and the buttons (0x77f3b10 and 0x6c8dcc).
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    constexpr std::size_t ClearedBytes = 10;
    std::fill_n(runtime.chatInput.messageBuffer.begin(), ClearedBytes, '\0');
    static_cast<void>(NoEditFieldLimit);
    runtime.chatInput.messageLength = 0;
    auto closed = CloseThePanel(context, Ui::SecondGuildPanel);
    if (!closed.HasValue())
        return Core::Unexpected(closed.Error());
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    return {};
}

Core::Expected<void, std::string> ApplyViewportGuildDelete(const ViewportGuildDelete& deleted,
                                                           const GuildReceiveContext& context)
{
    // 0x4e4306: without the character, 0x5110d0 gives 400 and the original writes past the 400 records (Q-C1298). MuRet
    // drops the whole 5D: it does not touch the member count nor close the guild either.
    const std::size_t index = Kernel::ActorWithKey(context.world, deleted.key);
    if (index >= ActorCount)
        return {};
    context.world.Runtime().guild.memberCount = -1;
    Kernel::ActorState& actor = context.world.Actors()[index];
    actor.word1f2 = static_cast<std::uint16_t>(NoGuildIndex);
    actor.guildStatus = 0xff;
    actor.byte369 = 0;
    actor.dword36c = 0;
    actor.byte370 = 0;
    auto closed = CloseThePanel(context, Ui::GuildPanel);
    if (!closed.HasValue())
        return Core::Unexpected(closed.Error());
    return {};
}

Core::Expected<void, std::string> ApplyWarRequestResult(const WarRequestResult& result,
                                                        const GuildReceiveContext& context)
{
    if (result.result <= LastWarRequestResult)
    {
        if (auto added = AddTextMessage(context, FirstWarRequestText + result.result, Ui::ErrorMessage);
            !added.HasValue())
            return added;
    }
    if (result.result == WarRequested || context.world.Runtime().guild.warOn != 0)
        return {};
    ClearTheWar(context.world);
    return {};
}

Core::Expected<void, std::string> ApplyWarRequest(const WarRequest& request, const GuildReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    CopyName(guild.rival, request.name);
    Ui::PushDialog(runtime.dialogs, Ui::IgnoredOverChaosCastle);
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
    // 0x4d82c6: sprintf into a 100-byte buffer.
    constexpr std::size_t NoticeBytes = 100;
    auto line = Formatted(context, format, NameOf(guild.rival), NoticeBytes, "0x4d82c6");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    if (auto noticed = AddHighlightedNotice(context, line.Value()); !noticed.HasValue())
        return noticed;
    guild.warTeam = start.team;
    // 0x4d830b..0x4d8369: strcmp of each entry's +0xd with the packet's name and its zero; with none, the index it had
    // stays.
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
    // 0x4d889a: with a result above 6, 0x554610 shows the uninitialized stack buffer (0x4d894f, Q-C1301). MuRet drops
    // the whole 63: the war goes on.
    if (end.result >= std::size(WarEndTexts))
        return {};
    auto text = TextOf(context, WarEndTexts[end.result]);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    // 0x4d88ba: sprintf without arguments.
    const std::string line = Ui::PlaceArguments(text.Value(), {});
    std::int32_t gesture = WonGesture;
    if (end.result == 1 || end.result == 2 || end.result == 4)
        gesture = LostGesture;
    else if (end.result == 6)
        gesture = NoGesture;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    // 0x4d8958: MOV word ptr [0x5767816],BX with BX at 0, the F3 22's time that the duel's AB also sets to 0 (0x4e4d60,
    // EventGlobals::timeValue).
    runtime.events.timeValue = 0;
    if (auto noticed = AddHighlightedNotice(context, line); !noticed.HasValue())
        return noticed;
    guild.warOn = 0;
    guild.warType = 0;
    guild.rivalIndex = -1;
    guild.rival[0] = '\0';
    // 0x4d898c..0x4d8a13: with the war off, the rival is not searched for.
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
    // 0x447bd8..: the high nibble of byte i / 2 in the even ones and the low one in the odd ones.
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

// 0x4d96f4..0x4d976b: with the war (0x576796c) and without the rival's index (0x6c82f0 at −1), the 65 looks it up by
// the name (0x5767804), if any, from entry 0 to 1998, and keeps the one it finds or −1, which the 62 does not do.
// Returns the index it marks with, or nothing without a name.
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
    Kernel::WorldState& world = context.world;
    GuildState& guild = world.Runtime().guild;
    for (const ViewportGuildEntry& entry : viewport.entries)
    {
        // 0x4d92b2: without the character, 0x5110d0 gives 400 and the original writes past the 400 records (Q-C1299).
        // MuRet drops the whole entry (without the 66 or 0x447b50) and goes on with the next.
        const std::size_t index = Kernel::ActorWithKey(world, entry.key);
        if (index >= ActorCount)
            continue;
        Kernel::ActorState& actor = world.Actors()[index];
        actor.guildStatus = entry.status;
        actor.byte369 = entry.byte369;
        actor.byte370 = entry.relation;
        // 0x4d92ef: 0x447b30 (0x447b00 other than −1) and 0x447b00 again; without the guild, the C1 08 66 that asks for
        // it and 0x447b50, which adds it (−1 with the table full).
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
        // 0x4d96b9: with the character alive (always: 0x5110d0 only finds live ones), +0x374 at 0; 1 with the hero's
        // guild (read again from 0x743a170); 2 with the rival's, at war.
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
        // 0x4d9772: the hero's, 0x412120(+0x1f2, +0x368).
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
    Kernel::WorldState& world = context.world;
    GuildState& guild = world.Runtime().guild;
    for (const ViewportUnionEntry& entry : viewport.entries)
    {
        // 0x4e44f5: without the character, 0x5110d0 gives 400 and the original writes the relation past the 400 records
        // (Q-C1300). MuRet drops the whole entry, also the union name the original writes before, and goes on with the
        // next.
        const std::size_t index = Kernel::ActorWithKey(world, entry.key);
        if (index >= ActorCount)
            continue;
        // 0x4e44c6..0x4e44e5: the union name at +4..+0xc of 0x447b00's entry, even if it is −1: then it goes to bytes 0
        // to 8 of member 74's name (Q-C1281, Q-C1284).
        const auto found = FindGuild(guild, entry.guildKey);
        if (!found)
            return Core::Unexpected(std::string("0x447b00: la búsqueda pasa de las 2.000 entradas de la tabla"));
        std::span<char> target = *found == -1
                                     ? std::span<char>(guild.members[MemberBeforeTheTable].name)
                                     : std::span<char>(guild.table[static_cast<std::size_t>(*found)].unionName);
        for (std::size_t i = 0; i < GuildNameSize; ++i)
            target[i] = std::to_integer<char>(entry.unionName[i]);
        target[GuildNameSize] = '\0';
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
        return AddMessage(context, InvalidAssign, Ui::SystemMessage);
    return AddTextMessage(context, *index, Ui::SystemMessage);
}

Core::Expected<void, std::string> ApplyRelationshipRequest(const RelationshipRequest& request,
                                                           const GuildReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    // 0x656603: with the dialog busy, no (C1 08 E6 with 0).
    if (Ui::PanelDialogHandle(runtime.panels.delegated.panelDialog) != 0)
        return Send(context,
                    WriteRelationshipAnswer(request.relation, request.request, 0, request.keyHigh, request.keyLow),
                    "0x656616");
    const auto key = static_cast<std::uint16_t>((request.keyHigh << 8) | request.keyLow);
    const std::size_t index = Kernel::ActorWithKey(context.world, key);
    // 0x656d44..0x656db1: with the character and a relation that is neither 1 nor 2, 0x6600d0 opens the dialog with the
    // uninitialized stack buffers (Q-C1302). MuRet drops the whole E5, without storing the four bytes.
    const bool related = request.relation == UnionRelation || request.relation == RivalRelation;
    if (index < ActorCount && !related)
        return {};
    // 0x656c95..0x656cba: the four bytes, and without the character (0x656ccb..0x656cd6), nothing else.
    guild.relationRequest = request.request;
    guild.relation = request.relation;
    guild.relationKeyHigh = request.keyHigh;
    guild.relationKeyLow = request.keyLow;
    if (index >= ActorCount)
        return {};
    const bool isUnion = request.relation == UnionRelation;
    const std::size_t asked = isUnion ? UnionRequestText : RivalRequestText;
    std::size_t detail = 0;
    if (isUnion)
        detail = request.request == 1 ? UnionJoinText : UnionOtherText;
    else
        detail = request.request == 1 ? RivalJoinText : RivalOtherText;
    auto first =
        Formatted(context, asked, Actors::HeroNameOf(context.world.Actors()[index]), RelationLineBytes, "0x656cd2");
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    // 0x656d91: the second without an argument; a %s of the text stays written (Q-C1368).
    auto second = Formatted(context, detail, {}, RelationLineBytes, "0x656d8a");
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    auto third = Formatted(context, RelationQuestionText, {}, RelationLineBytes, "0x656d9f");
    if (!third.HasValue())
        return Core::Unexpected(third.Error());
    const std::array<std::string, 3> lines{first.Value(), second.Value(), third.Value()};
    auto opened = OpenTheDialog(context, lines, Ui::PanelDialogOwner::Relationship);
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
        // 0x4d9a04: the switch on pkt[5].
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
    const auto added = index ? AddTextMessage(context, *index, Ui::SystemMessage)
                             : AddMessage(context, InvalidRelationship, Ui::SystemMessage);
    if (!added.HasValue())
        return added;
    if (result.key != context.playerKey || result.result != 1 || result.relation != UnionRelation ||
        result.request != BreakRequest)
        return {};
    // 0x4d9b2b: the first byte of the union name of the hero's guild. With −1, member 74's (Q-C1281).
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
        std::array<std::uint8_t, GuildMarkNibbles> mark{};
        constexpr std::uint8_t NibbleShift = 4;
        constexpr std::uint8_t LowNibble = 0x0f;
        for (std::size_t i = 0; i < GuildMarkNibbles; ++i)
        {
            const std::uint8_t packed = entry.mark[i / 2];
            mark[i] = (i % 2) == 0 ? static_cast<std::uint8_t>(packed >> NibbleShift)
                                   : static_cast<std::uint8_t>(packed & LowNibble);
        }
        // 0x4d9ebb: 0x6517e0(mark, pkt + 0x21, pkt[0]), with the 8-byte name without its 0.
        std::array<char, GuildNameSize + 1> name{};
        CopyName(name, entry.name);
        AddTheUnion(window, mark, GuildTextOf(name), entry.byte0);
    }
    return {};
}

Core::Expected<void, std::string> ApplyUnionListRefresh(const UnionListRefresh& refresh,
                                                        const GuildReceiveContext& context)
{
    if (refresh.kind != RefreshKind)
        return {};
    if (refresh.result == RefreshEmpty)
        return AddTextMessage(context, UnionListEmptyText, Ui::SystemMessage);
    if (refresh.result != RefreshAgain)
        return {};
    GuildWindowState& window = context.world.Runtime().guild.window;
    // 0x4d9bb2: 0x651810, the vtable + 0x9c of the union list (0x46e280: +0xa8, how many rows it has; in this instance
    // + 0x9c is not the row's y, which is + 0xa0, *static*). With more than 2, Sleep(500), which MuRet does not do
    // (P-C282), and the C1 03 E9.
    if (window.unions.size() > UnionRowsBeforeAsking)
    {
        if (auto sent = Send(context, WriteUnionListRequest(), "0x4d9bc8"); !sent.HasValue())
            return sent;
    }
    // 0x4d9d95: 0x651830.
    ClearTheUnions(window);
    return {};
}
} // namespace

void ClearTheWar(Kernel::WorldState& world)
{
    GuildState& guild = world.Runtime().guild;
    guild.warOn = 0;
    guild.rivalIndex = -1;
    guild.rival[0] = '\0';
    MarkTheGuilds(world, std::nullopt);
}

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
                auto opened = OpenThePanel(context, Ui::SecondGuildPanel);
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
} // namespace Client::Guild
