#include "Messenger/GameFriends.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Messenger/Friends.h"
#include "Protocol/Messenger/Letters.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Chat/ChatReceive.h"
#include "Chat/GameMessages.h"
#include "Hud/GameUi.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Messenger/FriendModel.h"
#include "Messenger/FriendWindows.h"
#include "Ui/GameText.h"
#include "Messenger/LetterWindows.h"
#include "Ui/MessageArea.h"
#include "Ui/OldWindows.h"
#include "Ui/SlideBanner.h"
#include "Crt/SprintfFormat.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"

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
constexpr std::size_t NoSuchCharacterText = 1047;
constexpr std::size_t ListFullText = 1048;
constexpr std::size_t AlreadyAFriendText = 1049;
constexpr std::size_t NotYourselfText = 1050;
constexpr std::size_t AsksToBeAFriendText = 1051;
constexpr std::size_t CannotDeleteText = 1052;
constexpr std::size_t OtherLevelText = 1068;
constexpr std::size_t LettersText = 1072;
constexpr std::size_t OfflineUntilBothText = 1075;
// 0x4dc3d0: the add results.
constexpr std::uint8_t NoSuchCharacter = 0;
constexpr std::uint8_t Added = 1;
constexpr std::uint8_t ListFull = 3;
constexpr std::uint8_t AlreadyAFriend = 4;
constexpr std::uint8_t NotYourself = 5;
constexpr std::uint8_t OtherLevelTooLow = 6;
// 0x4dc690.
constexpr std::uint8_t Deleted = 1;
// 0x41ed10(0x2a, 0, 0): the friend request.
constexpr Audio::SoundId RequestSound{0x2a};
// 0x491080: 0x46b720(1, 0, 1244, 2, 2.5, 0xffc8dce6).
constexpr std::int32_t HelpRepeats = 1;
constexpr std::uint16_t HelpGap = 0;
constexpr std::int32_t HelpKind = 2;
constexpr float HelpSpeed = 2.5f;
constexpr std::uint32_t HelpColor = 0xffc8dce6;
// The message box's texts.
constexpr std::size_t NoticeTitle = 228;
constexpr std::size_t QuestionTitle = 991;
constexpr std::size_t YesLabel = 1037;
constexpr std::size_t NoLabel = 1038;

// Up to the first '\0', as strlen and strcpy read it.
std::string CString(std::string_view text)
{
    return std::string(text.substr(0, text.find('\0')));
}

Core::Expected<std::string, std::string> TextOf(const Tables::TextTable* texts, std::size_t index)
{
    const std::optional<std::string_view> text = Ui::TextAt(texts, index);
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x555190("", text, 1): the empty name is the string 0x6e57a4 in 0x491080 and the .bss ones 0x5767a94 and 0x5767a98
// in 0x4dc010 and 0x4dc3d0 (*static*).
Core::Expected<void, std::string> SystemMessage(Kernel::WorldRuntime& runtime,
                                                const Render::Text::ITextMeasure* measure, Core::PixelSize screen,
                                                std::string_view text, std::string_view where)
{
    if (measure == nullptr)
        return Core::Unexpected(std::string(where) + ": no hay con qué medir el mensaje");
    return Chat::AddGameMessage(runtime, *measure, screen, "", text, Ui::SystemMessage);
}

// 0x48ee60(case, −1, −1, text, aux, 0) for a box.
Core::Expected<std::int32_t, std::string> OpenABox(const Chat::ChatReceiveContext& context, std::int32_t kind,
                                                   const std::string& text, std::int32_t aux, std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": la caja sin con qué medir su texto");
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::OldWindowTexts texts = OldWindowTextsOf(context.texts);
    // 0x48f700 destroys the buttons if the cascade aborts: the received boxes also lend the real press.
    const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
    return Ui::CreateTheOldWindow(
        runtime.oldWindows, Ui::OldWindowRequest{.kind = kind, .x = -1, .y = -1, .data = text, .aux = aux, .flags = 0},
        texts,
        Ui::OldWindowMeasure{.measure = *context.measure, .dcFont = runtime.hud.dcFont, .screen = context.screen},
        &globals);
}

Core::Expected<void, std::string> Notice(const Chat::ChatReceiveContext& context, const std::string& text,
                                         std::string_view where)
{
    auto opened = OpenABox(context, Ui::NoticeCase, text, 0, where);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

// "<name><text>" or the text alone.
Core::Expected<std::string, std::string> NoticeText(const Chat::ChatReceiveContext& context, std::size_t index,
                                                    const std::string* name)
{
    auto text = TextOf(context.texts, index);
    if (!text.HasValue())
        return text;
    return name != nullptr ? *name + text.Value() : text.Value();
}
} // namespace

Ui::OldWindowTexts OldWindowTextsOf(const Tables::TextTable* texts)
{
    const auto at = [texts](std::size_t index)
    {
        const std::optional<std::string_view> text = Ui::TextAt(texts, index);
        return text ? CString(*text) : std::string();
    };
    return Ui::OldWindowTexts{
        .notice = at(NoticeTitle), .question = at(QuestionTitle), .yes = at(YesLabel), .no = at(NoLabel), .lookup = at};
}

Protocol::Outgoing FriendPacketOf(const Ui::FriendSend& send)
{
    switch (send.kind)
    {
    case Ui::FriendSendKind::Add:
        return Protocol::Messenger::WriteFriendAdd(send.name);
    case Ui::FriendSendKind::Answer:
        return Protocol::Messenger::WriteFriendAnswer(send.value, send.name);
    case Ui::FriendSendKind::Delete:
        return Protocol::Messenger::WriteFriendDelete(send.name);
    case Ui::FriendSendKind::State:
        return Protocol::Messenger::WriteFriendState(send.value);
    // CL9 hito 10.
    case Ui::FriendSendKind::RoomRequest:
        return Protocol::Messenger::WriteRoomRequest(send.name);
    case Ui::FriendSendKind::Invite:
        return Protocol::Messenger::WriteInvite(send.name, send.room, static_cast<std::uint32_t>(send.window));
    case Ui::FriendSendKind::LetterSend:
        return Protocol::Messenger::WriteLetterSend(
            Protocol::Messenger::LetterToSend{.window = static_cast<std::uint32_t>(send.window),
                                              .name = send.name,
                                              .subject = send.subject,
                                              .direction = send.value,
                                              .action = send.action,
                                              .body = send.body});
    case Ui::FriendSendKind::LetterRead:
        return Protocol::Messenger::WriteLetterReadRequest(send.index);
    case Ui::FriendSendKind::LetterDelete:
        return Protocol::Messenger::WriteLetterDelete(send.index);
    case Ui::FriendSendKind::LetterList:
        return Protocol::Messenger::WriteLetterListRequest();
    case Ui::FriendSendKind::ListRequest:
    default:
        return Protocol::Messenger::WriteFriendListRequest();
    }
}

void SendAFriendPacket(Ui::IGameUiRequests& requests, Crt::CrtRandom& random, const Ui::FriendSend& send)
{
    if (send.kind == Ui::FriendSendKind::LetterSend)
    {
        static_cast<void>(random.Next());
        std::uint8_t serialDepth = 0;
        {
            const Models::LogicalAccess serial(serialDepth, random, Models::SerialCloseRandoms);
        }
    }
    requests.SendFriendPacket(FriendPacketOf(send));
}

Core::Expected<void, std::string> OpenTheFriends(const FriendsOpenContext& context, const Ui::OldWindowGlobals& globals,
                                                 const Ui::OldWindowStepView& view)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::ForgetTheHiddenWindows(runtime.oldWindows);
    // 0x491096: with 0x6c7b8c at 0, text 992 and nothing else; it is at 1.
    std::uint16_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    if (level < Messenger::FriendsLevel)
    {
        auto text = TextOf(context.texts, Messenger::FriendsLevelText);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (Ui::SaidRecently(runtime.chat.log, text.Value(), 1))
            return {};
        return SystemMessage(runtime, context.measure, context.screen, text.Value(), "0x4912fd");
    }
    auto opened = Messenger::OpenTheFriendsWindow(runtime.oldWindows, Messenger::FriendsWindowX,
                                                  Messenger::FriendsWindowY, globals, view);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    if (!opened.Value())
        return {};
    // 0x4914a1: 0x46b7d0, both queues with nothing to pass.
    Ui::SlideBanner& slides = runtime.hud.slides;
    if (!Ui::SlideQueueIsIdle(slides.bold) || !Ui::SlideQueueIsIdle(slides.normal))
        return {};
    auto help = TextOf(context.texts, Messenger::FriendsHelpText);
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    Ui::QueueTheSlide(slides, HelpRepeats, HelpGap, help.Value(), HelpKind, HelpSpeed, HelpColor);
    return {};
}

Core::Expected<void, std::string> ApplyFriendList(const Protocol::Messenger::FriendList& list,
                                                  const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OldWindowManager& manager = runtime.oldWindows;
    Messenger::ClearTheFriends(manager.friendModel);
    for (const Protocol::Messenger::FriendRow& row : list.friends)
        Messenger::AddAFriend(manager.friendModel, row.name, 0, row.state);
    Messenger::SortTheFriends(manager.friendModel, Messenger::ByNameSort);
    Messenger::SortTheFriends(manager.friendModel, Messenger::ByStateSort);
    Messenger::RefreshTheFriendTab(manager);
    Messenger::MarkTheFriendList(manager, 1, OldWindowTextsOf(context.texts));
    // 0x4dc0d7..0x4dc37a: the C4 02 that it sends with 0x6c7b8c at 0 never goes out (Q-C886).
    manager.letterLimit = list.maxLetters;
    if (list.letters == 0)
        return {};
    auto format = TextOf(context.texts, LettersText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    // 0x4dc38f..0x4dc395: both unsigned (AND with 0xff and MOVZX).
    const std::string text = Crt::Sprintf(format.Value(), static_cast<std::int32_t>(list.letters),
                                          static_cast<std::int32_t>(list.maxLetters));
    return SystemMessage(runtime, context.measure, context.screen, text, "0x4dc3b5");
}

Core::Expected<void, std::string> ApplyFriendAddResult(const Protocol::Messenger::FriendAddResult& result,
                                                       const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OldWindowManager& manager = runtime.oldWindows;
    const std::string name = Messenger::FriendName(result.name);
    std::size_t index = 0;
    bool withName = false;
    switch (result.result)
    {
    case Added:
    {
        auto text = TextOf(context.texts, OfflineUntilBothText);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto said = SystemMessage(runtime, context.measure, context.screen, text.Value(), "0x4dc493");
            !said.HasValue())
            return said;
        Messenger::AddAFriend(manager.friendModel, name, 0, result.state);
        Messenger::SortTheFriends(manager.friendModel, Messenger::SameSort);
        Messenger::RefreshTheFriendTab(manager);
        // 0x4a6840: the chats with the invite list open rebuild it.
        Messenger::RefreshTheInvites(manager, Actors::HeroNameOf(context.world.Hero()));
        return {};
    }
    case NoSuchCharacter:
        index = NoSuchCharacterText;
        withName = true;
        break;
    case ListFull:
        index = ListFullText;
        break;
    case AlreadyAFriend:
        index = AlreadyAFriendText;
        withName = true;
        break;
    case NotYourself:
        index = NotYourselfText;
        break;
    case OtherLevelTooLow:
        index = OtherLevelText;
        break;
    default:
        return {};
    }
    auto text = NoticeText(context, index, withName ? &name : nullptr);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    return Notice(context, text.Value(), "0x4dc581");
}

Core::Expected<void, std::string> ApplyFriendRequest(const Protocol::Messenger::FriendRequest& request,
                                                     const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::string name = Messenger::FriendName(request.name);
    auto text = NoticeText(context, AsksToBeAFriendText, &name);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    auto opened = OpenABox(context, Ui::QuestionCase, text.Value(), -1, "0x4dc658");
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    if (opened.Value() != 0)
        Messenger::RememberTheFriendRequest(runtime.oldWindows, opened.Value(), name);
    runtime.RequestSound(RequestSound);
    return {};
}

Core::Expected<void, std::string> ApplyFriendDeleteResult(const Protocol::Messenger::FriendDeleteResult& result,
                                                          const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (result.result == 0)
    {
        auto text = TextOf(context.texts, CannotDeleteText);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        return Notice(context, text.Value(), "0x4dc714");
    }
    if (result.result != Deleted)
        return {};
    Messenger::RemoveAFriend(runtime.oldWindows.friendModel, result.name);
    Messenger::RefreshTheFriendTab(runtime.oldWindows);
    return {};
}

Core::Expected<void, std::string> ApplyFriendState(const Protocol::Messenger::FriendState& state,
                                                   const Chat::ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OldWindowManager& manager = runtime.oldWindows;
    const Ui::OldWindowTexts texts = OldWindowTextsOf(context.texts);
    if (state.state == Messenger::AllOfflineState)
    {
        Messenger::SetAllTheFriends(manager.friendModel, 0, Messenger::AllOfflineState);
        Messenger::SortTheFriends(manager.friendModel, Messenger::SameSort);
        Messenger::RefreshTheFriendTab(manager);
        // 0x4a75c0: every chat with a friend receives the 402 and closes.
        std::optional<Ui::OldWindowMeasure> measure;
        if (context.measure != nullptr)
            measure.emplace(Ui::OldWindowMeasure{
                .measure = *context.measure, .dcFont = runtime.hud.dcFont, .screen = context.screen});
        if (auto closed = Messenger::CloseTheChats(manager, texts, measure ? &*measure : nullptr); !closed.HasValue())
            return closed;
        Messenger::MarkTheFriendList(manager, 0, texts);
        return {};
    }
    Messenger::SetAFriend(manager.friendModel, state.name, 0, state.state);
    Messenger::SortTheFriends(manager.friendModel, Messenger::SameSort);
    Messenger::RefreshTheFriendTab(manager);
    // 0x4dc7f4: 0x4a6840 and the chat with the friend (0x4a6430; the −1 passes the JBE and 0x490a00 does not find it):
    // with the state from 0xfd (offline), 0x495630(1); otherwise, 0x495630(0).
    const std::string_view hero = Actors::HeroNameOf(context.world.Hero());
    Messenger::RefreshTheInvites(manager, hero);
    const std::int32_t id = Messenger::FindTheChatWith(manager, Messenger::FriendName(state.name), hero);
    if (id == 0)
        return {};
    if (Ui::OldWindow* chat = Ui::FindTheOldWindow(manager, id); chat != nullptr)
        Messenger::CloseTheChat(*chat, state.state >= Messenger::FirstOfflineFriendState ? 1 : 0, texts);
    return {};
}
} // namespace Client::Messenger
