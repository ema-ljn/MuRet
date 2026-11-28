#include "Messenger/GameLetters.h"

#include "Actors/ActorName.h"
#include "App/IChatRoomLinks.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Messenger/Letters.h"
#include "Chat/ChatReceive.h"
#include "Messenger/LetterPortrait.h"
#include "Messenger/GameFriends.h"
#include "Chat/GameMessages.h"
#include "Hud/GameUi.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Messenger/FriendModel.h"
#include "Ui/GameText.h"
#include "Messenger/LetterModel.h"
#include "Messenger/LetterWindows.h"
#include "Ui/MessageArea.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowSteps.h"
#include "Ui/OldWindows.h"
#include "Crt/SprintfFormat.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Messenger
{
namespace
{
// Text.bmd.
constexpr std::size_t LetterSentText = 1046;
constexpr std::size_t SendFailedText = 1053;
constexpr std::size_t ReadTitleText = 1054;
constexpr std::size_t DeleteFailedText = 1055;
constexpr std::size_t InviteFailedText = 1056;
constexpr std::size_t InvitedText = 1057;
constexpr std::size_t JoinFailedText = 1058;
constexpr std::size_t CameInText = 1059;
constexpr std::size_t WentOutText = 1060;
constexpr std::size_t NoSuchReceiverText = 1061;
constexpr std::size_t NewLetterText = 1062;
constexpr std::size_t ChatWaitingText = 1063;
constexpr std::size_t ReceiverFullText = 1064;
constexpr std::size_t NoMoneyText = 1065;
constexpr std::size_t OtherLevelText = 1068;
constexpr std::size_t NotThereText = 1069;
constexpr std::size_t OfflineText = 1070;
constexpr std::size_t LettersFullText = 1073;
constexpr std::size_t InviteFullText = 1074;
constexpr std::size_t SeventhSendText = 423;
// 0x49e480 with FD_CLOSE: the room's connection was cut.
constexpr std::size_t RoomClosedText = 402;
// 0x4dc840 with the 1 and 0x49d0b0.
constexpr std::int32_t LetterCost = 1000;
// 0x41ed10(0x29, 0, 0): a letter arrived. It stays in lastSoundId (CL10).
constexpr Audio::SoundId NewLetterSound{0x29};
// 0x4dcb70: the read window, at (100, 100) without the previous one's place.
constexpr std::int32_t ReadPlace = 100;
// 0x4dce10.
constexpr std::int32_t ChatPlace = 100;
// 0x4dcd16..0x4dcd69: the portrait's class, action, angle and zoom.
constexpr std::int32_t ActionBase = 0x78;
constexpr std::int32_t AngleUnit = 6;
constexpr float ZoomUnit = 0.01f;
// 0x6c8610.
constexpr std::string_view WebzenName = "webzen";
// The ones _strnicmp compares (0x4dccbd).
constexpr std::size_t WebzenCompared = 10;
// 0x49e280: 0x555190(0x6e57a4, "max chat count over", 1).
constexpr std::string_view MaxChatCountOver = "max chat count over";
// 0x4dd3d0: the chat window's states.
constexpr std::int32_t MinimizedState = 5;
constexpr std::int32_t HiddenState = 3;
// 0x4dd1e0 and 0x4dd3d0: the system line (1) and a member's (3), and the speaker without a number (0xff).
constexpr std::int32_t SystemLine = 1;
constexpr std::int32_t MemberLine = 3;
constexpr std::uint8_t NoSpeaker = 0xff;
// The model's mark: 0x4a0270 leaves 0x4dcb70's at 1.
constexpr std::int32_t ReadType = 1;

std::string CString(std::string_view text)
{
    return std::string(text.substr(0, text.find('\0')));
}

Core::Expected<std::string, std::string> TextOf(const Chat::ChatReceiveContext& context, std::size_t index,
                                                std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::string(where) + ": el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x555190(empty, text, 1).
Core::Expected<void, std::string> SystemMessage(const Chat::ChatReceiveContext& context, std::string_view text,
                                                std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": no hay con qué medir el mensaje");
    return Chat::AddGameMessage(context.world.Runtime(), *context.measure, context.screen, "", text, Ui::SystemMessage);
}

// GetTextExtentPointA over the DC with its font, if there is something to measure with.
std::optional<Ui::OldWindowMeasure> OldWindowMeasureOf(const Chat::ChatReceiveContext& context)
{
    if (context.measure == nullptr)
        return std::nullopt;
    return Ui::OldWindowMeasure{
        .measure = *context.measure, .dcFont = context.world.Runtime().hud.dcFont, .screen = context.screen};
}

// What the factory and the old windows' steps read from the game from a packet: the texts, the measure, the globals
// and the view without the mouse or the keys (the cable is absent: what it sends cuts).
class OldWindowsFromAPacket
{
public:
    explicit OldWindowsFromAPacket(const Chat::ChatReceiveContext& context)
        : m_texts(OldWindowTextsOf(context.texts)), m_measure(OldWindowMeasureOf(context)),
          m_globals(Hud::OldWindowGlobalsOf(context.world.Runtime()))
    {
        Kernel::WorldRuntime& runtime = context.world.Runtime();
        m_view.texts = &m_texts;
        m_view.measure = m_measure ? &*m_measure : nullptr;
        m_view.heroName = std::string(Actors::HeroNameOf(context.world.Hero()));
        // CL9 milestone 10: the portrait's character (0x4dcd0e..0x4dcd69).
        if (context.worldEntry != nullptr)
            m_view.portrait = LetterPortraitHook(*context.worldEntry);
        m_view.field78abaac = runtime.ui.field78abaac;
    }
    OldWindowsFromAPacket(const OldWindowsFromAPacket&) = delete;
    OldWindowsFromAPacket& operator=(const OldWindowsFromAPacket&) = delete;
    OldWindowsFromAPacket(OldWindowsFromAPacket&&) = delete;
    OldWindowsFromAPacket& operator=(OldWindowsFromAPacket&&) = delete;
    ~OldWindowsFromAPacket() = default;

    [[nodiscard]] const Ui::OldWindowTexts& Texts() const
    {
        return m_texts;
    }
    [[nodiscard]] const Ui::OldWindowGlobals& Globals() const
    {
        return m_globals;
    }
    [[nodiscard]] const Ui::OldWindowStepView& View() const
    {
        return m_view;
    }
    [[nodiscard]] const Ui::OldWindowMeasure* Measure() const
    {
        return m_view.measure;
    }

    // 0x48ee60(case, x, y, text, 0, flags).
    [[nodiscard]] Core::Expected<std::int32_t, std::string> Create(Ui::OldWindowManager& manager, std::int32_t kind,
                                                                   std::int32_t x, std::int32_t y, std::string text,
                                                                   std::int32_t flags, std::string_view where) const
    {
        if (!m_measure)
            return Core::Unexpected(std::string(where) + ": la ventana sin con qué medir su texto");
        return Ui::CreateTheOldWindow(
            manager,
            Ui::OldWindowRequest{.kind = kind, .x = x, .y = y, .data = std::move(text), .aux = 0, .flags = flags},
            m_texts, *m_measure, &m_globals, &m_view);
    }

private:
    Ui::OldWindowTexts m_texts;
    std::optional<Ui::OldWindowMeasure> m_measure;
    Ui::OldWindowGlobals m_globals;
    Ui::OldWindowStepView m_view{};
};

// 0x48ee60(10, −1, −1, texto, 0, 0).
Core::Expected<void, std::string> Notice(const Chat::ChatReceiveContext& context, std::size_t index,
                                         std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    const OldWindowsFromAPacket windows(context);
    auto created =
        windows.Create(context.world.Runtime().oldWindows, Ui::NoticeCase, -1, -1, std::move(text).Value(), 0, where);
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    return {};
}

// 0x4a0960 over the letters tab, with the friends window.
void RefreshTheTab(Ui::OldWindowManager& manager)
{
    Messenger::RefreshTheLettersTab(manager);
}

// 0x493530(ip, room, ticket) over the chat: +0x770 = room and 0x49de50: a slot (0x49e280; without any, "max chat
// count over" and nothing else), the connection to port 55980 with message 0x500 + slot and the room noted with the
// window; and through it, the C1 10 00 with the room and the ticket. The original's connect waits (0x4e8570): if it
// fails, the room is not noted, nothing goes out and the slot stays taken (Q-C1005). MuRet connects without waiting:
// the room is noted at the start, the C1 10 00 waits in the queue and, if the connect fails, ApplyChatRoomClosed
// forgets it.
Core::Expected<void, std::string> JoinTheRoom(const Chat::ChatReceiveContext& context, Ui::OldWindow& chat,
                                              const Protocol::Messenger::RoomAnswer& answer)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    chat.chat.room = answer.room;
    if (!Messenger::TakeAChatSlot(manager.chatRooms, answer.room))
        return SystemMessage(context, MaxChatCountOver, "0x49e280");
    if (context.rooms == nullptr)
        return Core::Unexpected(std::string("0x49de50: la conexión de la sala del chat con un amigo, sin la sesión"));
    context.rooms->OpenChatRoom(answer.room, chat.base.id, answer.address);
    manager.chatRooms.rooms.try_emplace(answer.room, Messenger::ChatRoom{.room = answer.room, .window = chat.base.id});
    context.rooms->SendToChatRoom(answer.room, Protocol::Messenger::WriteJoin(answer.room, answer.ticket));
    return {};
}

// Window `id`, if it exists and is a chat (0x490a00).
Ui::OldWindow* ChatOf(Ui::OldWindowManager& manager, std::int32_t id)
{
    Ui::OldWindow* window = Ui::FindTheOldWindow(manager, id);
    if (window == nullptr || window->kind != Ui::OldWindowKind::ChatWindow)
        return nullptr;
    return window;
}
} // namespace

Core::Expected<void, std::string> ApplyLetterSendResult(const Protocol::Messenger::LetterSendResult& result,
                                                        const Chat::ChatReceiveContext& context)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    const auto window = static_cast<std::int32_t>(result.window);
    std::size_t text = 0;
    switch (result.result)
    {
    case 0:
        text = SendFailedText;
        break;
    case 1:
    {
        // 0x4dc87a: the 4 to the window and sprintf(1046, 1000) to the chat.
        if (window != 0)
            Ui::PostToTheManager(manager, Ui::CloseMessage, window, 0);
        auto sent = TextOf(context, LetterSentText, "0x4dc840(1)");
        if (!sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        const std::array<Crt::SprintfArgument, 1> cost{Crt::SprintfArgument(LetterCost)};
        return SystemMessage(context, Crt::FormatLikeSprintf(sent.Value(), cost), "0x4dc840(1)");
    }
    case 2:
        text = NoSuchReceiverText;
        break;
    case 3:
        text = ReceiverFullText;
        break;
    case 4:
        text = NoMoneyText;
        break;
    case 6:
        text = OtherLevelText;
        break;
    case 7:
        text = SeventhSendText;
        break;
    default:
        return {};
    }
    // +0x778 of 0x490a00's window, without checking that it exists (Q-C1001): MuRet does not write into one that does
    // not exist.
    if (window != 0)
    {
        if (Ui::OldWindow* write = Ui::FindTheOldWindow(manager, window); write != nullptr)
            write->letter.sending = 0;
    }
    return Notice(context, text, "0x4dc840");
}

Core::Expected<void, std::string> ApplyLetterRow(const Protocol::Messenger::LetterRow& row,
                                                 const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OldWindowManager& manager = runtime.oldWindows;
    if (row.type < 2)
    {
        Messenger::AddALetter(manager.letterModel, row.index, row.name, row.subject, row.date, row.time, row.type);
        Messenger::SortTheLetters(manager.letterModel, Messenger::ByDateLetters);
    }
    else if (row.type == 2)
    {
        // (CL10) The sound stays requested.
        runtime.RequestSound(NewLetterSound);
        manager.windowList.list.newLetter = 1;
        auto text = TextOf(context, NewLetterText, "0x4dcaa4");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto added = SystemMessage(context, text.Value(), "0x4dcaa4"); !added.HasValue())
            return added;
        Messenger::AddALetter(manager.letterModel, row.index, row.name, row.subject, row.date, row.time, 0);
        Messenger::SortTheLetters(manager.letterModel, Messenger::SameLetters);
    }
    RefreshTheTab(manager);
    if (static_cast<std::size_t>(manager.letterLimit) <= manager.letterModel.letters.size())
    {
        auto full = TextOf(context, LettersFullText, "0x4dcb35");
        if (!full.HasValue())
            return Core::Unexpected(std::move(full).Error());
        return SystemMessage(context, full.Value(), "0x4dcb35");
    }
    return {};
}

Core::Expected<void, std::string> ApplyLetterRead(const Protocol::Messenger::LetterRead& letter,
                                                  const Chat::ChatReceiveContext& context)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    Messenger::KeepTheLetter(manager.letterModel, letter.index, letter.copy);
    Messenger::LetterEntry* entry = Messenger::FindALetter(manager.letterModel, letter.index);
    if (entry == nullptr)
        return {};
    entry->type = ReadType;
    RefreshTheTab(manager);
    auto format = TextOf(context, ReadTitleText, "0x4dcb70");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    const std::array<Crt::SprintfArgument, 1> subject{Crt::SprintfArgument(std::string_view(entry->subject))};
    std::string title = Crt::FormatLikeSprintf(format.Value(), subject);
    const Messenger::LetterEntry shown = *entry;
    const OldWindowsFromAPacket windows(context);
    Core::Expected<std::int32_t, std::string> created = 0;
    if (manager.nextReadX == -1)
        created = windows.Create(manager, Ui::LetterReadCase, ReadPlace, ReadPlace, std::move(title), 0, "0x4dcb70");
    else
    {
        created = windows.Create(manager, Ui::LetterReadCase, manager.nextReadX, manager.nextReadY, std::move(title),
                                 Ui::KeepPlaceFlag, "0x4dcb70");
        manager.nextReadX = -1;
    }
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    const std::int32_t id = created.Value();
    Ui::OldWindow* read = Ui::FindTheOldWindow(manager, id);
    if (read == nullptr)
        return Core::Unexpected(std::string("0x4dcb70: la ventana de leer no se armó y el original sigue con NULL"));
    if (auto set = Messenger::SetTheLetter(manager, *read, shown, letter.body, windows.Measure()); !set.HasValue())
        return set;
    // 0x491720: the letter's window, if 0x491570 had noted it.
    if (const auto found = manager.letterWindows.find(shown.index); found != manager.letterWindows.end())
        found->second = id;
    if (LetterFromWebzen(shown.name))
    {
        // 0x4dccda: +0x770 of the read window, which is also +0x6c0 of its portrait (+0xb0): blank.
        read->letter.fromWebzen = 1;
        read->letter.portrait.blank = 1;
    }
    else
    {
        // The class (bits 4..6 and the 4 of pkt[8], 0x4977d0), the gear (0x497810), the action (0x4978f0), the angle
        // (0x497890, with the character built) and the zoom (0x4978d0).
        Ui::OldPortraitPart& portrait = read->letter.portrait;
        const std::uint8_t packed = letter.portraitClass;
        portrait.portraitClass = static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(packed & 0x10U) | static_cast<std::uint8_t>(packed >> 4U)) >> 1U);
        const Ui::OldWindowStepView& view = windows.View();
        if (!view.portrait)
            return Core::Unexpected(std::string("0x4dcd0e: el personaje del retrato de las cartas es de Scenes "
                                                "(P-C212)"));
        if (auto set = view.portrait(portrait, Ui::PortraitCallKind::SetClass, 0); !set.HasValue())
            return set;
        portrait.equipment = letter.equipment;
        if (auto set = view.portrait(portrait, Ui::PortraitCallKind::SetEquipment, 0); !set.HasValue())
            return set;
        portrait.action = letter.action + ActionBase;
        if (auto set = view.portrait(portrait, Ui::PortraitCallKind::SetAction, 0); !set.HasValue())
            return set;
        if (auto turned = Messenger::SetThePortraitAngle(
                &view, portrait, static_cast<float>((letter.direction & 0x3f) * AngleUnit), "0x4dcd4c");
            !turned.HasValue())
            return turned;
        Messenger::SetThePortraitZoom(portrait,
                                      static_cast<float>(((letter.direction >> 6) * 5 + 0x28) * 2) * ZoomUnit);
    }
    return Ui::Steps::PostAndStep(manager, *read, Ui::ToTheEndMessage, windows.Globals(), windows.View());
}

Core::Expected<void, std::string> ApplyLetterDeleteResult(const Protocol::Messenger::LetterDeleteResult& result,
                                                          const Chat::ChatReceiveContext& context)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (result.result == 0)
    {
        if (auto shown = Notice(context, DeleteFailedText, "0x4dcd90"); !shown.HasValue())
            return shown;
    }
    else if (result.result == 1)
    {
        Messenger::RemoveALetter(manager.letterModel, result.index);
        Messenger::ForgetTheLetter(manager.letterModel, result.index);
    }
    RefreshTheTab(manager);
    return {};
}

Core::Expected<void, std::string> ApplyRoomAnswer(const Protocol::Messenger::RoomAnswer& answer,
                                                  const Chat::ChatReceiveContext& context)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (answer.result == 0 || answer.result == 2)
    {
        Messenger::ForgetAPendingChat(manager, answer.name);
        return Notice(context, answer.result == 0 ? NotThereText : OfflineText, "0x4dce10");
    }
    if (answer.result != 1)
        return {};
    const OldWindowsFromAPacket windows(context);
    auto title = TextOf(context, Messenger::ChatTitleText, "0x4dce10");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    if (answer.type == 0)
    {
        Messenger::ForgetAPendingChat(manager, answer.name);
        auto created = windows.Create(manager, Ui::ChatCase, ChatPlace, ChatPlace, title.Value(), 0, "0x4dce10(1, 0)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        Ui::OldWindow* chat = ChatOf(manager, created.Value());
        if (chat == nullptr)
            return Core::Unexpected(std::string("0x4dcfcf: el chat no se armó y el original sigue con NULL"));
        return JoinTheRoom(context, *chat, answer);
    }
    if (answer.type == 1)
    {
        // 0x4a6430: the chat already open with the friend leaves its room and joins the new one.
        const std::int32_t open = Messenger::FindTheChatWith(manager, answer.name, windows.View().heroName);
        if (open == -1)
            return {};
        if (open != 0)
        {
            Ui::OldWindow* chat = ChatOf(manager, open);
            if (chat == nullptr)
                return Core::Unexpected(std::string("0x4dcf58: el chat de 0x4a6430 no está"));
            if (auto left = Messenger::LeaveTheChatRoom(manager, *chat); !left.HasValue())
                return left;
            // 0x493a70 sends the C1 03 01 and closes the old connection (0x49dfd0) at once, before 0x493530 opens the
            // new one: with the same room number, the new one cannot be the old one.
            FlushTheChatRoomOrders(manager.chatRooms, context.rooms);
            return JoinTheRoom(context, *chat, answer);
        }
    }
    else if (answer.type != 2)
        return {};
    // 0x4dcf7e: the 2 (minimized), the room, state 5, to the back (the 5 to the manager) and out of the windows tab.
    auto created =
        windows.Create(manager, Ui::CalledChatCase, ChatPlace, ChatPlace, title.Value(), 0, "0x4dce10(1, 2)");
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    const std::int32_t id = created.Value();
    Ui::OldWindow* chat = ChatOf(manager, id);
    if (chat == nullptr)
        return Core::Unexpected(std::string("0x4dcf9c: el chat no se armó y el original sigue con NULL"));
    if (auto joined = JoinTheRoom(context, *chat, answer); !joined.HasValue())
        return joined;
    chat->base.state = MinimizedState;
    Ui::PostToTheManager(manager, Ui::SendBackMessage, id, 0);
    Ui::Steps::ForgetTheWindowRow(manager, id);
    return {};
}

Core::Expected<void, std::string> ApplyInviteResult(const Protocol::Messenger::InviteResult& result,
                                                    const Chat::ChatReceiveContext& context)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    Ui::OldWindow* chat = ChatOf(manager, static_cast<std::int32_t>(result.window));
    if (chat == nullptr)
        return {};
    std::size_t index = 0;
    std::string text;
    switch (result.result)
    {
    case 0:
        index = InviteFailedText;
        break;
    case 1:
    {
        // 0x4dd04f: the invite list's chosen one (the vtable + 0x64), if any, with the 1057 after it.
        Ui::OldChatPart& part = chat->chat;
        const std::optional<std::size_t> at = Ui::SelectedInTheList(part.invite, part.invitable.size());
        if (!at || *at >= part.invitable.size())
            return {};
        text = Messenger::FriendName(part.invitable[*at].name);
        index = InvitedText;
        break;
    }
    case 3:
        index = InviteFullText;
        break;
    default:
        return {};
    }
    auto line = TextOf(context, index, "0x4dd020");
    if (!line.HasValue())
        return Core::Unexpected(std::move(line).Error());
    const std::optional<Ui::OldWindowMeasure> measure = OldWindowMeasureOf(context);
    return Messenger::AddAChatLine(manager, *chat, NoSpeaker, text + line.Value(), SystemLine, 0,
                                   measure ? &*measure : nullptr);
}

Core::Expected<void, std::string> ApplyRoomPacket(std::int32_t window, const Network::Packet& packet,
                                                  const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OldWindowManager& manager = runtime.oldWindows;
    namespace Chat = Protocol::Messenger;
    const std::optional<Network::Opcode> opcode = packet.Opcode();
    if (!opcode)
        return {};
    const OldWindowsFromAPacket windows(context);
    // One shorter than what its handler reads is not applied (Q-C1011): ServerSession ignores it earlier, with its
    // warning (Protocol::Messenger::CheckRoomPacket), and here nothing closes either.
    switch (Network::ByteOf(*opcode))
    {
    case Network::ByteOf(Protocol::Messenger::RoomJoinOpcode):
    {
        auto join = Chat::ReadJoinResult(packet);
        if (!join.HasValue())
            return {};
        if (join.Value().result != 0)
            return {};
        return Notice(context, JoinFailedText, "0x4dd1b0");
    }
    case Network::ByteOf(Protocol::Messenger::RoomMemberOpcode):
    {
        // 0x4dd1e0.
        auto member = Chat::ReadMember(packet);
        if (!member.HasValue())
            return {};
        Ui::OldWindow* chat = ChatOf(manager, window);
        if (chat == nullptr)
            return {};
        const Chat::Member& got = member.Value();
        const std::string name = Messenger::FriendName(got.name);
        if (got.kind == 0)
        {
            const auto count = Messenger::AddAChatMember(manager, *chat, name, got.index, 0, windows.Texts(),
                                                         windows.View().heroName, windows.Measure());
            if (!count.HasValue())
                return Core::Unexpected(std::move(count).Error());
            if (count.Value() > 2)
            {
                auto text = TextOf(context, CameInText, "0x4dd1e0");
                if (!text.HasValue())
                    return Core::Unexpected(std::move(text).Error());
                if (auto added = Messenger::AddAChatLine(manager, *chat, NoSpeaker, name + text.Value(), SystemLine, 0,
                                                         windows.Measure());
                    !added.HasValue())
                    return added;
            }
        }
        else if (got.kind == 1)
        {
            if (Ui::ListCount(chat->chat.users, chat->chat.members.size()) > 2)
            {
                auto text = TextOf(context, WentOutText, "0x4dd1e0");
                if (!text.HasValue())
                    return Core::Unexpected(std::move(text).Error());
                if (auto added = Messenger::AddAChatLine(manager, *chat, NoSpeaker, name + text.Value(), SystemLine, 0,
                                                         windows.Measure());
                    !added.HasValue())
                    return added;
            }
            if (auto removed = Messenger::RemoveAChatMember(manager, *chat, name, windows.Texts(),
                                                            windows.View().heroName, windows.Measure());
                !removed.HasValue())
                return removed;
        }
        else
            return {};
        if (chat->chat.mode == 2)
            Messenger::FillTheInvites(manager, *chat, windows.View().heroName);
        return {};
    }
    case Network::ByteOf(Protocol::Messenger::RoomMembersOpcode):
    {
        // 0x4dd350: 0x490a00 and 0x493c90 without checking that the window exists.
        auto members = Chat::ReadMembers(packet);
        if (!members.HasValue())
            return {};
        for (const Chat::MemberRow& row : members.Value().members)
        {
            Ui::OldWindow* chat = ChatOf(manager, window);
            if (chat == nullptr)
                return Core::Unexpected(std::string("0x4dd3a1: el chat no está y el original sigue con NULL"));
            if (auto added = Messenger::AddAChatMember(manager, *chat, row.name, row.index, 0, windows.Texts(),
                                                       windows.View().heroName, windows.Measure());
                !added.HasValue())
                return Core::Unexpected(std::move(added).Error());
        }
        return {};
    }
    case Network::ByteOf(Protocol::Messenger::RoomMessageOpcode):
    {
        // 0x4dd3d0.
        auto message = Chat::ReadMessage(packet);
        if (!message.HasValue())
            return {};
        Ui::OldWindow* chat = ChatOf(manager, window);
        if (chat == nullptr || message.Value().length > Chat::MessageLimit)
            return {};
        if (chat->base.state == MinimizedState)
        {
            Messenger::MarkTheChatWaiting(manager, window, runtime.lastSoundId,
                                          [&runtime](Audio::SoundId sound) { runtime.RequestSound(sound); });
            auto text = TextOf(context, ChatWaitingText, "0x4dd47c");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            if (auto added = SystemMessage(context, text.Value(), "0x4dd47c"); !added.HasValue())
                return added;
            chat->base.state = HiddenState;
            Ui::Steps::AddTheWindowRow(manager, window, chat->frame.title.value_or(std::string()));
        }
        else if (chat->base.state == HiddenState || Ui::LastOldWindow(manager) != window)
            Messenger::MarkTheChatWaiting(manager, window, runtime.lastSoundId,
                                          [&runtime](Audio::SoundId sound) { runtime.RequestSound(sound); });
        return Messenger::AddAChatLine(manager, *chat, message.Value().index, message.Value().text, MemberLine, 0,
                                       windows.Measure());
    }
    case Network::ByteOf(Protocol::Messenger::RoomNoticeOpcode):
    {
        // 0x4dd549: pkt[0x68] = 0 and, if there is text, to the chat.
        auto notice = Chat::ReadNotice(packet);
        if (!notice.HasValue())
            return {};
        if (notice.Value().text.empty())
            return {};
        return SystemMessage(context, notice.Value().text, "0x4dd540");
    }
    default:
        return {};
    }
}

Core::Expected<void, std::string> ApplyChatRoomClosed(const App::ChatRoomClosed& closed,
                                                      const Chat::ChatReceiveContext& context)
{
    Ui::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (!closed.opened)
    {
        Messenger::ForgetTheChatRoom(manager.chatRooms, closed.room);
        return {};
    }
    // 0x49e480 with FD_CLOSE: the room's chat, if it exists, with nobody's 402 (0x493f80 with 0xff and 1).
    Ui::OldWindow* chat = ChatOf(manager, closed.window);
    if (chat == nullptr)
        return {};
    auto text = TextOf(context, RoomClosedText, "0x49e480");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    const std::optional<Ui::OldWindowMeasure> measure = OldWindowMeasureOf(context);
    return Messenger::AddAChatLine(manager, *chat, NoSpeaker, text.Value(), SystemLine, 0,
                                   measure ? &*measure : nullptr);
}

void FlushTheChatRoomOrders(Messenger::ChatRooms& rooms, App::IChatRoomLinks* links)
{
    while (!rooms.orders.empty())
    {
        const Messenger::ChatRoomOrder order = std::move(rooms.orders.front());
        rooms.orders.pop_front();
        if (links == nullptr)
            continue;
        switch (order.kind)
        {
        case Messenger::ChatRoomOrder::Kind::Message:
            links->SendToChatRoom(order.room, Protocol::Messenger::WriteMessage(order.text));
            break;
        case Messenger::ChatRoomOrder::Kind::Leave:
            links->SendToChatRoom(order.room, Protocol::Messenger::WriteLeave());
            links->CloseChatRoom(order.room);
            break;
        case Messenger::ChatRoomOrder::Kind::Ping:
            links->SendToChatRoom(order.room, Protocol::Messenger::WritePing());
            break;
        }
    }
}
bool LetterFromWebzen(std::string_view name)
{
    const std::string_view head = name.substr(0, std::min(name.find('\0'), WebzenCompared));
    const auto lower = [](char letter)
    { return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter; };
    return head.size() == WebzenName.size() &&
           std::ranges::equal(head, WebzenName, [&lower](char a, char b) { return lower(a) == b; });
}

} // namespace Client::Messenger
