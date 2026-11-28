#include "Chat/GameChat.h"

#include "Actors/ActorName.h"
#include "Input/Key.h"
#include "Input/TextInputEvent.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputEvent.h"
#include "Maps/MapId.h"
#include "Protocol/Chat/ChatSend.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Movement/Movement.h"
#include "Models/IModelLibrary.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Actors/Emote.h"
#include "Assets/FaunaModels.h"
#include "Hud/GameCommandsWindow.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Social/SocialRequests.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatCommands.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Chat/Emotes.h"
#include "Messenger/FriendWindows.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/OldControls.h"
#include "Ui/OldEditInput.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Chat/WhisperSenders.h"
#include "Chat/ChatRuntime.h"
#include "Events/EventGlobals.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Chat
{
namespace
{
// 0x4b5269..: Chaos Castle, from 18 to 23.
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
// 0x4b529b..0x4b52f0
constexpr std::array<std::int32_t, 5> EnterBlockingDialogs{0x7d, 0x95, 0x8a, 0x8d, 0x8b};
// 0x4b52f6: 0x78abb00 at 3.
constexpr std::int32_t EnterBlockingField = 3;
// 0x657880(0xd).
constexpr std::int32_t EnterVirtualKey = 0xd;
// 0x469450: 0 shows them with the focus on the message one; 3 hides them.
constexpr std::int32_t ShowFocused = 0;
constexpr std::int32_t Hide = 3;
// The Text.bmd texts (the address − 0x76bd1dc, over 300): the filter's "..." (0x76e6dd4), /whisper off and on
// (0x76d073c and 0x76d0868), their notices (0x76d0ac0 and 0x76d0bec) and 0x554a40's (0x76e0330).
constexpr std::size_t FilteredText = 570;
constexpr std::size_t WhisperOffCommandText = 264;
constexpr std::size_t WhisperOnCommandText = 265;
constexpr std::size_t WhisperOffNoticeText = 267;
constexpr std::size_t WhisperOnNoticeText = 268;
constexpr std::size_t WhisperDeniedText = 479;
// 0x5767940: what each said message leaves and the limit.
constexpr std::int32_t BrakeSteps = 0x46;
constexpr std::int32_t BrakeLimit = 0x32;
// 0x4b6f5e: 0x554a40(6, nombre).
constexpr std::uint16_t WhisperLevel = 6;
// 0x6c64ac and, with it in the name, the only thing that is said: what starts with 0x6c78dc or with 0x6c78d8.
constexpr std::string_view WebzenWord = "webzen";
constexpr std::array<std::string_view, 2> WebzenPrefixes{"/", "!"};
// The hero's +0x318: the Uniria, the Dinorant and the dark horse. Mounted and outside the safe zone, no emotes.
constexpr std::uint16_t FirstMountModel = 0x1c14;
constexpr std::uint16_t LastMountModel = 0x1c16;
// 0x4b8874: the vtable + 0x34 of 0x576350c with 100.
constexpr std::int32_t ScrollAfterSaying = 100;
// 0x4b54e1: the zen dialog, with the field in the EDIT of [0x5763504].
constexpr std::int32_t MoneyDialog = 0x73;
// 0x4b5515: the field's length (0x6c8e78) on opening the chat, the same that the dialogs' close leaves.
constexpr std::int32_t ClosedDialogFieldLimit = 0x2a;
// 0x4b55b3: 0x78aba60 at 1, the dialog 0x91 of the personal shop's title (0x5eba70(1)).
constexpr std::int32_t ShopTitleDialogMode = 1;
// 0x4b55fd: the title that 0x5ebbd0 does not allow (0x770fe14).
constexpr std::size_t BadShopTitleText = 1130;

Core::Expected<std::string_view, std::string> TextOf(const GameChatContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x555190(nombre, texto, tipo).
Core::Expected<void, std::string> AddMessage(const GameChatContext& context, std::string_view name,
                                             std::string_view text, std::int32_t type, std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: 0x555190 sin el medidor de los textos", where));
    return AddGameMessage(context.world.Runtime(), *context.measure, context.screen, name, text, type);
}

// 0x555190("", texto `index`, tipo).
Core::Expected<void, std::string> AddNotice(const GameChatContext& context, std::size_t index, std::int32_t type,
                                            std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return AddMessage(context, "", text.Value(), type, where);
}

std::int32_t LengthOf(const Ui::ChatBuffer& buffer)
{
    return static_cast<std::int32_t>(Ui::CStringOf(buffer).size());
}

// The vtable + 0x4c of the EDIT of [0x5763504] with 0 and the + 4 with 3 (0x4b558a..0x4b55ab).
void HideTheDialogEdit(Ui::ChatInputState& input)
{
    input.dialogEdit.text.SetText({});
    input.dialogEdit.base.state = Ui::HiddenWindow;
    if (input.focus == Ui::ChatFocus::Dialog)
        input.focus = Ui::ChatFocus::Game;
}

// 0x4b8886..0x4b88e2: with 0x77f3b6b at 0, the 0x100 bytes of the field in use and its length at 0, and the chat
// closed; with the serial number input (0x77f3b6b), nothing (0x4b888f).
void ForgetWhatWasTyped(const Kernel::WorldRuntime& runtime, Ui::ChatInputState& input, Ui::GameUiState& ui)
{
    if (runtime.events.serialEntry)
        return;
    input.messageBuffer.fill('\0');
    input.messageLength = 0;
    ui.chatOpen = false;
}

std::string LegacyControlText(Input::Key key)
{
    switch (key)
    {
    case Input::Key::Backspace:
        return std::string(1, Input::BackspaceChar);
    case Input::Key::Tab:
        return std::string(1, Input::TabChar);
    case Input::Key::Enter:
        return std::string(1, Input::EnterChar);
    default:
        return {};
    }
}

std::optional<Ui::OldEditKey> EditKeyOf(Input::Key key)
{
    using Edit = Ui::OldEditKey;
    switch (key)
    {
    case Input::Key::Backspace:
        return Edit::Backspace;
    case Input::Key::Delete:
        return Edit::Delete;
    case Input::Key::Left:
        return Edit::Left;
    case Input::Key::Right:
        return Edit::Right;
    case Input::Key::Home:
        return Edit::Home;
    case Input::Key::End:
        return Edit::End;
    case Input::Key::Up:
        return Edit::Up;
    case Input::Key::Down:
        return Edit::Down;
    case Input::Key::PageUp:
        return Edit::PageUp;
    case Input::Key::PageDown:
        return Edit::PageDown;
    case Input::Key::Tab:
        return Edit::Tab;
    case Input::Key::C:
        return Edit::Copy;
    case Input::Key::X:
        return Edit::Cut;
    case Input::Key::V:
        return Edit::Paste;
    case Input::Key::A:
        return Edit::CtrlA;
    default:
        return std::nullopt;
    }
}

// strncmp(text, command, strlen(command)) with the command not empty.
bool StartsWithTheCommand(std::string_view said, std::string_view command)
{
    return !command.empty() && said.starts_with(command);
}

// 0x4b56cc..0x4b571c and 0x5a11db..0x5a1204: the emotes (0x576eb0), except mounted outside the safe zone. The C1 18
// go out through `send`, like the chat.
Core::Expected<void, std::string> DoTheEmotes(const GameChatContext& context, std::string_view said)
{
    Kernel::ActorState& hero = context.world.Hero();
    if (hero.pet.model >= FirstMountModel && hero.pet.model <= LastMountModel && hero.safeZone == 0)
        return {};
    if (context.texts == nullptr)
        return Core::Unexpected(std::string("0x576eb0: los emotes sin Text.bmd"));
    const std::optional<Actors::EmoteCase> emote = Chat::FindTheEmote(*context.texts, said);
    if (!emote)
        return {};
    // 0x576abf→0x4ea030 reads the global array: the models loaded by the viewport are also available. Fauna keeps those
    // resources separately and can exist before the first draw's models are bound.
    const Models::IModelLibrary* models =
        context.faunaModels != nullptr && context.faunaModels->SkeletonOf(Models::ModelId{hero.type}) != nullptr
            ? context.faunaModels
            : context.models;
    if (models == nullptr)
        return Core::Unexpected(std::string("0x576a60: el emote sin los modelos (0x4ea030)"));
    const Actors::ActionSender send = [&context](std::uint8_t direction, std::uint8_t action)
    {
        if (context.send)
            context.send(Protocol::Movement::WriteAction(direction, action));
    };
    return Actors::DoTheEmote(*emote, Actors::EmoteContext{.hero = hero, .models = *models, .send = send});
}

// The requests to another of 0x56c440 (RunTheCommand already looked at Text.bmd).
Social::SocialRequestContext SocialRequestsOf(const GameChatContext& context)
{
    return Social::SocialRequestContext{
        .world = context.world, .random = context.random, .texts = *context.texts, .send = context.send};
}

// What 0x56c440 reads and writes.
Core::Expected<Chat::ChatCommandOutcome, std::string> RunTheCommand(const GameChatContext& context,
                                                                    std::string_view text, bool fromMacro)
{
    if (context.texts == nullptr)
        return Core::Unexpected(std::string("0x56c440: los comandos sin Text.bmd"));
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Hud::GamePanels panels(context.world, context.random);
    const Chat::ChatCommandContext commands{
        .texts = *context.texts,
        .items = context.items,
        .setOptions = context.setOptions,
        .map = runtime.map,
        .logoutRequested = runtime.ui.logoutRequested,
        .warehouseOpen = panels.Registry().IsOpen(Ui::WarehousePanel),
        .log = runtime.chat.log,
        .macros = runtime.chat.input.macros,
        .overlayMode = runtime.panels.overlayMode,
        .helpItem = runtime.panels.helpItem,
        .setLabel = &context.world.ItemSets().byte9305,
        .signsShown = &runtime.social.personalShop.signsShown,
        .ownLevel = [&context]() -> std::uint16_t
        {
            Kernel::WorldRuntime& own = context.world.Runtime();
            const Models::LogicalAccess access(own.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
            return context.world.OwnCharacter().level;
        },
        .duelActive = runtime.social.duel.active != 0,
        .endTheDuel = [&context]() -> Core::Expected<void, std::string>
        {
            if (!context.send)
                return Core::Unexpected(std::string("0x56ea6c: /duelend sin el cable"));
            // The C3's padding and serial.
            context.random.Discard(2);
            context.send(Protocol::Social::WriteDuelEnd());
            return {};
        },
        .requestATrade = [&context]() { return Social::RequestATrade(SocialRequestsOf(context)); },
        .requestAParty = [&context]() { return Social::RequestAParty(SocialRequestsOf(context)); },
        .requestADuel = [&context]() { return Social::RequestADuel(SocialRequestsOf(context)); },
        .trainerOpen = panels.Registry().IsOpen(Ui::TrainerPanel),
        .openThePersonalShop = [&context]() -> Core::Expected<void, std::string>
        {
            // 0x65ac3f: opening one's own closes the rest except the inventory, with the 3D and the 31.
            Hud::GamePanels shops(context.world, context.random);
            shops.LendTheSession(context.world, context.random, context.send, context.sendWindowClose);
            auto opened = shops.Registry().Open(Ui::PersonalShopPanel);
            if (!opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
            return {};
        },
        .requestShopItems = [&context](std::optional<std::string_view> name)
        { return Social::RequestTheShopItems(SocialRequestsOf(context), name); },
        .requestAGuildJoin = [&context]() { return Hud::RequestAGuildJoin(context); },
        .requestAGuildRelation = [&context](Chat::GuildRelationCommand command)
        { return Hud::RequestAGuildRelation(context, command); }};
    auto outcome = Chat::RunTheChatCommand(commands, text, fromMacro);
    if (!outcome.HasValue())
        return outcome;
    for (const Chat::ChatCommandNotice& notice : outcome.Value().notices)
    {
        auto added = notice.formatted ? AddMessage(context, "", *notice.formatted, notice.type, "0x56c440")
                                      : AddNotice(context, notice.text, notice.type, "0x56c440");
        if (!added.HasValue())
            return Core::Unexpected(added.Error());
    }
    if (outcome.Value().sound)
        runtime.RequestSound(Ui::ChatButtonSound);
    return outcome;
}

// 0x622b40(text, 1): with any word of Filter.bmd, the text 570.
Core::Expected<std::string, std::string> FilteredOf(const GameChatContext& context, std::string_view said)
{
    if (!Ui::ContainsAFilteredWord(said, context.filter, true))
        return std::string(said);
    auto filtered = TextOf(context, FilteredText, "0x4b566c");
    if (!filtered.HasValue())
        return Core::Unexpected(filtered.Error());
    return std::string(filtered.Value());
}

} // namespace

// 0x6a0c01 (atoi) → 0x6a0b76 (the CRT's atol): the isspace blanks, a '-' or a '+', and the isdigit digits, with the
// count in 32 bits that wraps around silently; with '-', negated. With the "C" locale (without setlocale, like the
// sscanf of 0x574c8c): the blanks are the space and 0x09 to 0x0d, and the digits '0' to '9'.
std::int32_t AtoiLikeTheCrt(std::string_view text)
{
    std::size_t at = 0;
    while (at < text.size() && (text[at] == ' ' || (text[at] >= '\t' && text[at] <= '\r')))
        ++at;
    const char sign = at < text.size() ? text[at] : '\0';
    if (sign == '-' || sign == '+')
        ++at;
    std::uint32_t total = 0;
    constexpr std::uint32_t Ten = 10;
    for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at)
        total = total * Ten + static_cast<std::uint32_t>(text[at] - '0');
    if (sign == '-')
        total = 0u - total;
    return static_cast<std::int32_t>(total);
}

Core::Expected<void, std::string> SayInTheChat(const GameChatContext& context, std::string_view said)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::ChatInputState& input = runtime.chat.input;
    const Kernel::ActorState& hero = context.world.Hero();
    if (input.brake > BrakeLimit)
        return {};
    if (input.brake > 0 && input.lastSaid == said)
        return {};
    input.lastSaid = std::string(said);
    input.brake = BrakeSteps;
    if (Ui::ContainsLikeTheOriginal(Actors::HeroNameOf(hero), WebzenWord) &&
        std::ranges::none_of(WebzenPrefixes, [said](std::string_view prefix) { return said.starts_with(prefix); }))
        return {};
    if (hero.dying == 0)
    {
        if (!said.empty() && said.front() == '/')
        {
            auto off = TextOf(context, WhisperOffCommandText, "0x4b5a4f");
            if (!off.HasValue())
                return Core::Unexpected(off.Error());
            if (StartsWithTheCommand(said, off.Value()))
            {
                runtime.chat.whisperBlocked = true;
                return AddNotice(context, WhisperOffNoticeText, Ui::SystemMessage, "0x4b5aa1");
            }
            auto on = TextOf(context, WhisperOnCommandText, "0x4b5aae");
            if (!on.HasValue())
                return Core::Unexpected(on.Error());
            if (StartsWithTheCommand(said, on.Value()))
            {
                runtime.chat.whisperBlocked = false;
                return AddNotice(context, WhisperOnNoticeText, Ui::SystemMessage, "0x4b5b00");
            }
        }
    }
    else
    {
        auto move = TextOf(context, Chat::MoveCommandText, "0x4b59f4");
        if (!move.HasValue())
            return Core::Unexpected(move.Error());
        if (StartsWithTheCommand(said, move.Value()))
            return {};
    }
    if (context.send)
        context.send(Protocol::Chat::WriteChat(hero.name, said));
    return {};
}

namespace
{
// 0x554a40(6, 0x7798cf8): the own record's level (+0xe), with its access, or the name in one of the ten slots of
// 0x77997f8 (strcmp, which runs on into the next slot if the name fills its own); if not, the notice 479.
Core::Expected<bool, std::string> MayWhisper(const GameChatContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::ChatInputState& input = runtime.chat.input;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        if (context.world.OwnCharacter().level >= WhisperLevel)
            return true;
    }
    const std::string_view target = Ui::CStringOf(input.whisperBuffer);
    for (std::size_t slot = 0; slot < Chat::WhisperSenderSlots; ++slot)
    {
        if (Chat::WhisperSenderAt(runtime.chat.whisperSenders, slot) == target)
            return true;
    }
    auto added = AddNotice(context, WhisperDeniedText, Ui::SystemMessage, "0x554d0b");
    if (!added.HasValue())
        return Core::Unexpected(added.Error());
    return false;
}

// 0x4b6f59..0x4b883e: the whisper. Without "webzen" in the hero's name, the C1 02 to the buffer's name (its ten bytes)
// and the last recipient (0x57678c4); with or without it, what was said in the log with the hero's name and type 0.
Core::Expected<void, std::string> WhisperIt(const GameChatContext& context, std::string_view said)
{
    auto allowed = MayWhisper(context);
    if (!allowed.HasValue())
        return Core::Unexpected(allowed.Error());
    if (!allowed.Value())
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::ChatInputState& input = runtime.chat.input;
    const Kernel::ActorState& hero = context.world.Hero();
    if (!Ui::ContainsLikeTheOriginal(Actors::HeroNameOf(hero), WebzenWord))
    {
        std::array<std::byte, Protocol::Chat::ChatNameSize> target{};
        std::transform(input.whisperBuffer.begin(), input.whisperBuffer.begin() + Protocol::Chat::ChatNameSize,
                       target.begin(), [](char letter) { return static_cast<std::byte>(letter); });
        if (context.send)
            context.send(Protocol::Chat::WriteWhisper(target, said));
        // 0x4b87e9: the ten bytes and the '\0' of 0x57678ce, as the 0C reads them.
        const std::string_view sent(input.whisperBuffer.data(), Protocol::Chat::ChatNameSize);
        runtime.chat.lastWhisperTarget = std::string(sent.substr(0, sent.find('\0')));
    }
    return AddMessage(context, Actors::HeroNameOf(hero), said, Ui::WhisperMessage, "0x4b8839");
}

// 0x4b5624..0x4b883e: what was typed, with the field already read. It returns whether it goes on at 0x4b8841
// (everything except a command that handled it).
Core::Expected<bool, std::string> SayWhatWasTyped(const GameChatContext& context)
{
    Ui::ChatInputState& input = context.world.Runtime().chat.input;
    auto command = RunTheCommand(context, Ui::CStringOf(input.messageBuffer), false);
    if (!command.HasValue())
        return Core::Unexpected(command.Error());
    if (command.Value().handled)
        return false;
    auto said = FilteredOf(context, Ui::CStringOf(input.messageBuffer));
    if (!said.HasValue())
        return Core::Unexpected(said.Error());
    // 0x4b566c: strcpy to the buffer, which leaves what is behind.
    Ui::CopyIntoTheBuffer(input.messageBuffer, said.Value());
    const std::string& text = said.Value();
    if ((!text.empty() && text.front() == '/') || input.whisperLength == 0 || !input.whisperMode)
    {
        if (auto emotes = DoTheEmotes(context, text); !emotes.HasValue())
            return Core::Unexpected(emotes.Error());
        if (auto chat = SayInTheChat(context, text); !chat.HasValue())
            return Core::Unexpected(chat.Error());
        return true;
    }
    if (auto whisper = WhisperIt(context, text); !whisper.HasValue())
        return Core::Unexpected(whisper.Error());
    return true;
}
} // namespace

Core::Expected<void, std::string> TypeInTheGameChat(const GameChatContext& context, std::string_view typed)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::ChatInputState& input = runtime.chat.input;
    constexpr std::string_view Keys{"\t\r\b", 3};
    std::string_view rest = typed;
    while (!rest.empty())
    {
        const std::size_t key = rest.find_first_of(Keys);
        // An old window's EDIT with the Windows focus (CL9 milestone 9) receives the WM_CHAR, and its subclass
        // (0x467b10) passes the Enter to its parent and, with a parent, the Tab to the EDIT of +0x98
        // (0x467cdf..0x467d28; only the write-a-letter one links them, CL9 milestone 10). Without the one of +0x98, the
        // Tab does nothing (P-C181).
        const bool oldEdit = Messenger::HasAnOldEditFocused(runtime.oldWindows, input.focus);
        // The dialogs' EDIT with the focus: the one of [0x5763504] (the serial number input, milestone 11; the guild
        // name of window 2, CL9 milestone 8) or the one of [0x5763508] (the number of the 0x7d, CL9 milestone 8). The
        // same subclass, with the filter of +0x4c.
        Ui::OldEdit* dialogEdit = nullptr;
        if (input.focus == Ui::ChatFocus::Dialog)
            dialogEdit = &input.dialogEdit;
        else if (input.focus == Ui::ChatFocus::NumberField)
            dialogEdit = &input.numberField;
        // 0x467b9d: with the WM_CHAR, the subclass sets the EDIT's draws to 0 (+0x80, the cursor on). MuRet does it
        // with the dialogs' two EDITs (CL9 milestone 8, P-C286), the scene's ones that are drawn with 0x468320 and are
        // not an old window's.
        if (dialogEdit != nullptr)
            dialogEdit->draws = 0;
        if (const std::string_view run = rest.substr(0, key); !run.empty())
        {
            if (oldEdit)
                Messenger::TypeInTheOldEdit(runtime.oldWindows, run);
            else
                Ui::TypeInTheChat(input, run);
        }
        if (key == std::string_view::npos)
            break;
        const char pressed = rest[key];
        rest.remove_prefix(key + 1);
        if (oldEdit)
        {
            if (pressed == Input::EnterChar)
                Messenger::PressEnterInTheOldEdit(runtime.oldWindows);
            else if (pressed == Input::BackspaceChar)
                Messenger::BackspaceInTheOldEdit(runtime.oldWindows);
            else if (pressed == Input::TabChar)
                Messenger::ApplyKeyToTheOldEdit(runtime.oldWindows, Ui::OldEditKey::Tab, false, false,
                                                context.clipboard, context.measure, &runtime.ui.focusedList);
            continue;
        }
        if (Ui::OldEdit* edit = Ui::ChatEditWithFocus(input); edit != nullptr && !edit->compositionText.empty())
        {
            edit->draws = 0;
            continue;
        }
        if (dialogEdit != nullptr)
        {
            // 0x467b5d..0x467c19: without a parent (+0x44 at 0), the Tab and the Enter go as WM_CHAR to the game window
            // (+0x84); the Enter not with the window list built (+0x150 of [0x5763524]) nor with +0xb4 (the vtable +
            // 0x5c, 0x4676d0) or +0xc4 at 1, which nobody sets in this EDIT. The erasing is the EDIT's.
            if (pressed == Input::BackspaceChar)
            {
                Ui::BackspaceInTheChat(input);
                continue;
            }
            if (pressed == Input::EnterChar && runtime.oldWindows.windowList.list.armed == 1)
                continue;
        }
        if (pressed == Input::TabChar)
        {
            if (Ui::PressTabInTheChat(input, runtime.ui.chatOpen, &runtime.ui.focusedList))
                runtime.RequestSound(Ui::ChatButtonSound);
        }
        else if (pressed == Input::EnterChar)
        {
            if (auto entered = PressEnterInTheGame(context); !entered.HasValue())
                return entered;
        }
        else
            Ui::BackspaceInTheChat(input);
    }
    return {};
}

Core::Expected<void, std::string> TypeTheGameInput(const GameChatContext& context,
                                                   std::span<const Input::TextInputEvent> events)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::ChatInputState& input = runtime.chat.input;
    for (const Input::TextInputEvent& event : events)
    {
        if (event.kind == Input::TextInputEvent::Kind::Text)
        {
            if (auto typed = TypeInTheGameChat(context, event.text); !typed.HasValue())
                return typed;
            continue;
        }
        const bool oldEdit = Messenger::HasAnOldEditFocused(runtime.oldWindows, input.focus);
        Ui::OldEdit* chatEdit = Ui::ChatEditWithFocus(input);
        if (event.kind == Input::TextInputEvent::Kind::Composition)
        {
            if (oldEdit)
                Messenger::SetTheOldEditComposition(runtime.oldWindows, event.text, event.compositionStart,
                                                    event.compositionLength);
            else if (chatEdit != nullptr)
                Ui::SetTheEditComposition(*chatEdit, event.text, event.compositionStart, event.compositionLength);
            continue;
        }
        const auto editKey = EditKeyOf(event.key);
        if (oldEdit)
        {
            if (event.key == Input::Key::Enter)
                Messenger::PressEnterInTheOldEdit(runtime.oldWindows);
            else if (editKey)
                Messenger::ApplyKeyToTheOldEdit(runtime.oldWindows, *editKey, event.shift, event.ctrl,
                                                context.clipboard, context.measure, &runtime.ui.focusedList);
            continue;
        }
        // SDL delivers keys during the preedit. The EDIT's same guard avoids editing or sending before the commit; this
        // does not replace the message translation of a Windows XP IME.
        if (chatEdit != nullptr && !chatEdit->compositionText.empty())
        {
            if (editKey)
                Ui::ApplyKeyToTheEdit(*chatEdit, *editKey, event.shift, event.ctrl, context.clipboard, context.measure);
            continue;
        }
        if (chatEdit != nullptr && editKey && event.key != Input::Key::Tab)
        {
            Ui::ApplyKeyToTheEdit(*chatEdit, *editKey, event.shift, event.ctrl, context.clipboard, context.measure);
            continue;
        }
        // The EDITs without a parent forward Tab and Enter to the game window (0x467b5d..0x467c19).
        if (const std::string control = LegacyControlText(event.key); !control.empty())
        {
            if (auto typed = TypeInTheGameChat(context, control); !typed.HasValue())
                return typed;
        }
    }
    return {};
}

Core::Expected<void, std::string> PressEnterInTheGame(const GameChatContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    Ui::ChatInputState& input = runtime.chat.input;
    const Kernel::ActorState& hero = context.world.Hero();
    // 0x4b50c0..0x4b5106: in Chaos Castle outside the safe zone, nothing; if not, 0x5763588 at 1, the Enter the dialogs
    // read (0x6263df and 0x62da58). The scene is 5; with 0x77f3b68, the guild sign-up field (CL9 milestone 8), nothing
    // more (0x4b5115).
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap && hero.safeZone == 0)
        return {};
    ui.enterPressed = true;
    if (input.dialogFieldOn)
        return {};
    if (std::ranges::find(EnterBlockingDialogs, runtime.dialogs.current) != EnterBlockingDialogs.end())
        return {};
    if (runtime.panels.field78abb00 == EnterBlockingField)
        return {};
    {
        Hud::GamePanels panels(context.world, context.random);
        panels.LendTheSession(context.world, context.random, context.send, context.sendWindowClose);
        auto taken = panels.Registry().PanelKey(EnterVirtualKey);
        if (!taken.HasValue())
            return Core::Unexpected(taken.Error());
        if (taken.Value())
            return {};
    }
    // 0x4b5198..0x4b5334: the Enter belongs to the old windows with the window list built (+0x150 of [0x5763524]), with
    // the modal box (0x5762ebc) above all (the last of +0x84) or with a non-modal box (+0xb8 of [0x5763514], 0x4918a0)
    // above all. 0x4b533d..0x4b5360: with the focus outside the game window, 0x467360 saves the IME's mode, which MuRet
    // does not have.
    const Ui::OldWindowManager& old = runtime.oldWindows;
    if (old.windowList.list.armed == 1)
        return {};
    const std::int32_t top = Ui::LastOldWindow(old);
    if (old.modal != 0 && top == old.modal)
        return {};
    if (!old.boxes.empty() && std::ranges::find(old.boxes, top) != old.boxes.end())
        return {};
    // 0x4b5397..0x4b5435: with the field closed, the text of the EDIT of [0x5763504] (the serial number input's,
    // milestone 11, and the dialogs 0x73 and 0x91's, milestone 7); with it open, the two EDITs and what was typed to
    // the history (the vtable + 8).
    if (!ui.chatOpen)
        Ui::CopyIntoTheBuffer(input.messageBuffer, input.dialogEdit.text.Value());
    else
    {
        Ui::ReadTheChatFields(input);
        Ui::RememberTheMessage(input, Ui::CStringOf(input.messageBuffer));
        input.whisperLength = LengthOf(input.whisperBuffer);
    }
    input.messageLength = LengthOf(input.messageBuffer);
    // 0x4b5468..0x4b5554: the empty field closes it or, closed, opens it, without emptying anything; with the dialog
    // 0x73, instead of opening it, it empties the dialog's EDIT and gives it the focus.
    if (input.messageLength == 0 && !input.otherInput)
    {
        if (ui.chatOpen)
        {
            ui.chatOpen = false;
            Ui::ShowTheChatFields(input, Hide, &runtime.ui.focusedList);
        }
        else if (runtime.dialogs.current == MoneyDialog)
        {
            // 0x4b54e1..0x4b5513: the vtable + 0x4c of the EDIT of [0x5763504] with 0 (empty) and SetFocus of the game
            // window ([0x5763544]). The 0x73's draw gives the focus back to the EDIT in the next frame (0x6308b8).
            input.dialogEdit.text.SetText({});
            input.focus = Ui::ChatFocus::Game;
        }
        else
        {
            // 0x4b5515..0x4b554d: 0x77f31c0 at 0 belongs to the input without an EDIT (0x6c7b8c at 0).
            runtime.social.dialogFieldLimit = ClosedDialogFieldLimit;
            input.fieldCount = Ui::ChatFieldCount;
            runtime.social.moneyField = 0;
            Ui::ShowTheChatFields(input, ShowFocused, &runtime.ui.focusedList);
            ui.chatOpen = true;
        }
        return {};
    }
    input.field = Ui::MessageField;
    // 0x4b5563..0x4b55ae: with the zen mode (0x77f3b6a, the dialog 0x73 or the 0x91's price), what was typed is a
    // number (atoi to 0x77f3b6c) and is not said; then the vtable + 0x4c of the EDIT of [0x5763504] with 0 (empty), the
    // + 4 with 3 (hidden, ShowWindow(SW_HIDE): with the focus on it, the focus goes back to the game window, *inferred*
    // as in Ui::ShowTheChatFields) and the jump to 0x4b8886.
    if (runtime.social.moneyField != 0)
    {
        runtime.social.typedMoney = AtoiLikeTheCrt(Ui::CStringOf(input.messageBuffer));
        HideTheDialogEdit(input);
        ForgetWhatWasTyped(runtime, input, ui);
        return {};
    }
    // 0x4b55b3..0x4b560f: with 0x78aba60 at 1, what was typed is the personal shop's title and is not said: with
    // 0x5ebbd0 the strcpy to 0x78aba64; if not, the text 1130 of type 1 without a name (0x6e57a4, ""). Then, the jump
    // to 0x4b8886.
    if (runtime.social.personalShop.dialogMode == ShopTitleDialogMode)
    {
        const std::string_view typed = Ui::CStringOf(input.messageBuffer);
        if (Ui::IsAShopTitle(typed, context.filter))
            runtime.social.personalShop.ownTitle = std::string(typed);
        else if (auto said = AddNotice(context, BadShopTitleText, Ui::SystemMessage, "0x4b5607"); !said.HasValue())
            return said;
        ForgetWhatWasTyped(runtime, input, ui);
        return {};
    }
    // 0x4b5614..0x4b561e and 0x4b8888: with the serial number input (0x77f3b6b), nothing more: it is not said, the
    // buffer is not emptied and the field is not closed.
    if (runtime.events.serialEntry)
        return {};
    auto said = SayWhatWasTyped(context);
    if (!said.HasValue())
        return Core::Unexpected(said.Error());
    if (said.Value())
        runtime.chat.area.firstShown = Ui::FirstShownRow;
    // 0x4b884b..0x4b8883.
    Ui::SetTheChatFields(input, std::string_view{}, std::nullopt);
    Ui::ShowTheChatFields(input, Hide, &runtime.ui.focusedList);
    Ui::ScrollTheLog(runtime.chat.log, ui.chatModes, ScrollAfterSaying);
    ForgetWhatWasTyped(runtime, input, ui);
    return {};
}

Core::Expected<void, std::string> SayTheMacros(const GameChatContext& context,
                                               const std::array<bool, Tables::MacroCount>& pressed)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    for (std::size_t macro = 0; macro < Tables::MacroCount; ++macro)
    {
        if (!pressed[macro])
            continue;
        const std::string text = runtime.chat.input.macros[macro];
        auto command = RunTheCommand(context, text, true);
        if (!command.HasValue())
            return Core::Unexpected(command.Error());
        if (command.Value().handled)
            continue;
        if (auto emotes = DoTheEmotes(context, text); !emotes.HasValue())
            return emotes;
        auto said = FilteredOf(context, text);
        if (!said.HasValue())
            return Core::Unexpected(said.Error());
        if (auto chat = SayInTheChat(context, said.Value()); !chat.HasValue())
            return chat;
        // 0x5a1ea1.
        runtime.hud.macroTime = Ui::MacroLockSteps;
    }
    return {};
}
} // namespace Client::Chat
