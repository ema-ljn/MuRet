#include "Scenes/GameFriends.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/Friends.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameMessages.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/FriendModel.h"
#include "UI/FriendWindows.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/OldWindows.h"
#include "UI/SlideBanner.h"
#include "UI/SprintfFormat.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
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
// 0x4dc3d0: los resultados de agregar.
constexpr std::uint8_t NoSuchCharacter = 0;
constexpr std::uint8_t Added = 1;
constexpr std::uint8_t ListFull = 3;
constexpr std::uint8_t AlreadyAFriend = 4;
constexpr std::uint8_t NotYourself = 5;
constexpr std::uint8_t OtherLevelTooLow = 6;
// 0x4dc690.
constexpr std::uint8_t Deleted = 1;
// 0x41ed10(0x2a, 0, 0): el pedido de amistad. Queda en lastSoundId: MuRet no tiene audio hasta la CL10.
constexpr std::int32_t RequestSound = 0x2a;
// 0x491080: 0x46b720(1, 0, 1244, 2, 2,5, 0xffc8dce6).
constexpr std::int32_t HelpRepeats = 1;
constexpr std::uint16_t HelpGap = 0;
constexpr std::int32_t HelpKind = 2;
constexpr float HelpSpeed = 2.5f;
constexpr std::uint32_t HelpColor = 0xffc8dce6;
// Los textos de la caja de mensaje.
constexpr std::size_t NoticeTitle = 228;
constexpr std::size_t QuestionTitle = 991;
constexpr std::size_t YesLabel = 1037;
constexpr std::size_t NoLabel = 1038;

// Hasta el primer '\0', como lo leen strlen y strcpy.
std::string CString(std::string_view text)
{
    return std::string(text.substr(0, text.find('\0')));
}

Core::Expected<std::string, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x555190("", texto, 1): el nombre vacío es la cadena 0x6e57a4 en 0x491080 y los de la .bss 0x5767a94 y 0x5767a98 en
// 0x4dc010 y 0x4dc3d0 (*estático*).
Core::Expected<void, std::string> SystemMessage(WorldRuntime& runtime, const Render::Text::ITextMeasure* measure,
                                                App::PixelSize screen, std::string_view text, std::string_view where)
{
    if (measure == nullptr)
        return Core::Unexpected(std::string(where) + ": no hay con qué medir el mensaje");
    return AddGameMessage(runtime, *measure, screen, "", text, UI::SystemMessage);
}

// 0x48ee60(caso, −1, −1, texto, aux, 0) para una caja.
Core::Expected<std::int32_t, std::string> OpenABox(const ChatReceiveContext& context, std::int32_t kind,
                                                   const std::string& text, std::int32_t aux, std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": la caja sin con qué medir su texto");
    WorldRuntime& runtime = context.world.Runtime();
    const UI::OldWindowTexts texts = OldWindowTextsOf(context.texts);
    return UI::CreateTheOldWindow(
        runtime.oldWindows, UI::OldWindowRequest{.kind = kind, .x = -1, .y = -1, .data = text, .aux = aux, .flags = 0},
        texts,
        UI::OldWindowMeasure{.measure = *context.measure, .dcFont = runtime.hud.dcFont, .screen = context.screen});
}

Core::Expected<void, std::string> Notice(const ChatReceiveContext& context, const std::string& text,
                                         std::string_view where)
{
    auto opened = OpenABox(context, UI::NoticeCase, text, 0, where);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

// "<nombre><texto>" o el texto solo.
Core::Expected<std::string, std::string> NoticeText(const ChatReceiveContext& context, std::size_t index,
                                                    const std::string* name)
{
    auto text = TextOf(context.texts, index);
    if (!text.HasValue())
        return text;
    return name != nullptr ? *name + text.Value() : text.Value();
}
} // namespace

UI::OldWindowTexts OldWindowTextsOf(const Data::Text::TextTable* texts)
{
    const auto at = [texts](std::size_t index)
    {
        const std::optional<std::string_view> text = UI::TextAt(texts, index);
        return text ? CString(*text) : std::string();
    };
    return UI::OldWindowTexts{
        .notice = at(NoticeTitle), .question = at(QuestionTitle), .yes = at(YesLabel), .no = at(NoLabel), .lookup = at};
}

Protocol::Outgoing FriendPacketOf(const UI::FriendSend& send)
{
    switch (send.kind)
    {
    case UI::FriendSendKind::Add:
        return Protocol::Friends::WriteFriendAdd(send.name);
    case UI::FriendSendKind::Answer:
        return Protocol::Friends::WriteFriendAnswer(send.value, send.name);
    case UI::FriendSendKind::Delete:
        return Protocol::Friends::WriteFriendDelete(send.name);
    case UI::FriendSendKind::State:
        return Protocol::Friends::WriteFriendState(send.value);
    case UI::FriendSendKind::ListRequest:
    default:
        return Protocol::Friends::WriteFriendListRequest();
    }
}

Core::Expected<void, std::string> OpenTheFriends(const FriendsOpenContext& context, const UI::OldWindowGlobals& globals,
                                                 const UI::OldWindowStepView& view)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::ForgetTheHiddenWindows(runtime.oldWindows);
    // 0x491096: con 0x6c7b8c en 0, el texto 992 y nada más; está en 1.
    std::uint16_t level = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    if (level < UI::FriendsLevel)
    {
        auto text = TextOf(context.texts, UI::FriendsLevelText);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (UI::SaidRecently(runtime.chat.log, text.Value(), 1))
            return {};
        return SystemMessage(runtime, context.measure, context.screen, text.Value(), "0x4912fd");
    }
    auto opened = UI::OpenTheFriendsWindow(runtime.oldWindows, UI::FriendsWindowX, UI::FriendsWindowY, globals, view);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    if (!opened.Value())
        return {};
    // 0x4914a1: 0x46b7d0, las dos colas sin nada que pasar.
    UI::SlideBanner& slides = runtime.hud.slides;
    if (!UI::SlideQueueIsIdle(slides.bold) || !UI::SlideQueueIsIdle(slides.normal))
        return {};
    auto help = TextOf(context.texts, UI::FriendsHelpText);
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    UI::QueueTheSlide(slides, HelpRepeats, HelpGap, help.Value(), HelpKind, HelpSpeed, HelpColor);
    return {};
}

Core::Expected<void, std::string> ApplyFriendList(const Protocol::Friends::FriendList& list,
                                                  const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::OldWindowManager& manager = runtime.oldWindows;
    UI::ClearTheFriends(manager.friendModel);
    for (const Protocol::Friends::FriendRow& row : list.friends)
        UI::AddAFriend(manager.friendModel, row.name, 0, row.state);
    UI::SortTheFriends(manager.friendModel, UI::ByNameSort);
    UI::SortTheFriends(manager.friendModel, UI::ByStateSort);
    UI::RefreshTheFriendTab(manager);
    UI::MarkTheFriendList(manager, 1, OldWindowTextsOf(context.texts));
    // 0x4dc0d7..0x4dc37a: el C4 02 que manda con 0x6c7b8c en 0 no sale nunca (Q-C886).
    manager.letterLimit = list.maxLetters;
    if (list.letters == 0)
        return {};
    auto format = TextOf(context.texts, LettersText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    // 0x4dc38f..0x4dc395: los dos sin signo (AND con 0xff y MOVZX).
    const std::string text = UI::Sprintf(format.Value(), static_cast<std::int32_t>(list.letters),
                                         static_cast<std::int32_t>(list.maxLetters));
    return SystemMessage(runtime, context.measure, context.screen, text, "0x4dc3b5");
}

Core::Expected<void, std::string> ApplyFriendAddResult(const Protocol::Friends::FriendAddResult& result,
                                                       const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::OldWindowManager& manager = runtime.oldWindows;
    const std::string name = UI::FriendName(result.name);
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
        UI::AddAFriend(manager.friendModel, name, 0, result.state);
        UI::SortTheFriends(manager.friendModel, UI::SameSort);
        UI::RefreshTheFriendTab(manager);
        // 0x4a6840: el estado en las ventanas de chat con un amigo (hito 10): ninguna.
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

Core::Expected<void, std::string> ApplyFriendRequest(const Protocol::Friends::FriendRequest& request,
                                                     const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::string name = UI::FriendName(request.name);
    auto text = NoticeText(context, AsksToBeAFriendText, &name);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    auto opened = OpenABox(context, UI::QuestionCase, text.Value(), -1, "0x4dc658");
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    if (opened.Value() != 0)
        UI::RememberTheFriendRequest(runtime.oldWindows, opened.Value(), name);
    runtime.lastSoundId = RequestSound;
    return {};
}

Core::Expected<void, std::string> ApplyFriendDeleteResult(const Protocol::Friends::FriendDeleteResult& result,
                                                          const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (result.result == 0)
    {
        auto text = TextOf(context.texts, CannotDeleteText);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        return Notice(context, text.Value(), "0x4dc714");
    }
    if (result.result != Deleted)
        return {};
    UI::RemoveAFriend(runtime.oldWindows.friendModel, result.name);
    UI::RefreshTheFriendTab(runtime.oldWindows);
    return {};
}

Core::Expected<void, std::string> ApplyFriendState(const Protocol::Friends::FriendState& state,
                                                   const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (state.state == UI::AllOfflineState)
    {
        UI::SetAllTheFriends(manager.friendModel, 0, UI::AllOfflineState);
        UI::SortTheFriends(manager.friendModel, UI::SameSort);
        UI::RefreshTheFriendTab(manager);
        // 0x4a75c0: los chats con un amigo fuera de línea (hito 10): ninguno.
        UI::MarkTheFriendList(manager, 0, OldWindowTextsOf(context.texts));
        return {};
    }
    UI::SetAFriend(manager.friendModel, state.name, 0, state.state);
    UI::SortTheFriends(manager.friendModel, UI::SameSort);
    UI::RefreshTheFriendTab(manager);
    // 0x4a6840 y 0x4a6430(nombre) con 0x495630: el estado en el chat con ese amigo (hito 10): ninguno.
    return {};
}
} // namespace Client::Scenes
