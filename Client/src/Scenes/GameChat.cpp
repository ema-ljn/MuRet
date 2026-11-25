#include "Scenes/GameChat.h"

#include "Input/Key.h"
#include "Input/TextInputEvent.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputEvent.h"
#include "Maps/MapId.h"
#include "Protocol/Chat/ChatSend.h"
#include "Protocol/Duel.h"
#include "Protocol/Movement.h"
#include "Render/Models/IModelLibrary.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/Emote.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameCommandsWindow.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SocialRequests.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatCommands.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/Emotes.h"
#include "UI/FriendWindows.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/NameChecks.h"
#include "UI/OldControls.h"
#include "UI/OldEditInput.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "UI/WhisperSenders.h"

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

namespace Client::Scenes
{
namespace
{
// 0x4b5269..: Chaos Castle, del 18 al 23.
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
// 0x4b529b..0x4b52f0: los diálogos con los que Enter no hace nada.
constexpr std::array<std::int32_t, 5> EnterBlockingDialogs{0x7d, 0x95, 0x8a, 0x8d, 0x8b};
// 0x4b52f6: 0x78abb00 en 3.
constexpr std::int32_t EnterBlockingField = 3;
// 0x657880(0xd).
constexpr std::int32_t EnterVirtualKey = 0xd;
// 0x469450: 0 los muestra con el foco en el del mensaje; 3 los oculta.
constexpr std::int32_t ShowFocused = 0;
constexpr std::int32_t Hide = 3;
// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300): el "..." del filtro (0x76e6dd4), /whisper off y on
// (0x76d073c y 0x76d0868), sus avisos (0x76d0ac0 y 0x76d0bec) y el de 0x554a40 (0x76e0330).
constexpr std::size_t FilteredText = 570;
constexpr std::size_t WhisperOffCommandText = 264;
constexpr std::size_t WhisperOnCommandText = 265;
constexpr std::size_t WhisperOffNoticeText = 267;
constexpr std::size_t WhisperOnNoticeText = 268;
constexpr std::size_t WhisperDeniedText = 479;
// 0x5767940: lo que deja cada mensaje dicho y el tope.
constexpr std::int32_t BrakeSteps = 0x46;
constexpr std::int32_t BrakeLimit = 0x32;
// 0x4b6f5e: 0x554a40(6, nombre).
constexpr std::uint16_t WhisperLevel = 6;
// 0x6c64ac y, con él en el nombre, lo único que se dice: lo que empieza con 0x6c78dc o con 0x6c78d8.
constexpr std::string_view WebzenWord = "webzen";
constexpr std::array<std::string_view, 2> WebzenPrefixes{"/", "!"};
// +0x318 del héroe: la Uniria, el Dinorant y el caballo oscuro. Montado y fuera de la zona segura, sin emotes.
constexpr std::uint16_t FirstMountModel = 0x1c14;
constexpr std::uint16_t LastMountModel = 0x1c16;
// 0x4b8874: la vtable + 0x34 de 0x576350c con 100.
constexpr std::int32_t ScrollAfterSaying = 100;
// 0x4b54e1: el diálogo del zen, con el campo en el EDIT de [0x5763504].
constexpr std::int32_t MoneyDialog = 0x73;
// 0x4b5515: el largo del campo (0x6c8e78) al abrir el chat, el mismo que deja el cierre de los diálogos.
constexpr std::int32_t ClosedDialogFieldLimit = 0x2a;
// 0x4b55b3: 0x78aba60 en 1, el diálogo 0x91 del título de la tienda personal (0x5eba70(1)).
constexpr std::int32_t ShopTitleDialogMode = 1;
// 0x4b55fd: el título que 0x5ebbd0 no deja (0x770fe14).
constexpr std::size_t BadShopTitleText = 1130;

Core::Expected<std::string_view, std::string> TextOf(const GameChatContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
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

std::int32_t LengthOf(const UI::ChatBuffer& buffer)
{
    return static_cast<std::int32_t>(UI::CStringOf(buffer).size());
}

// La vtable + 0x4c del EDIT de [0x5763504] con 0 y la + 4 con 3 (0x4b558a..0x4b55ab).
void HideTheDialogEdit(UI::ChatInputState& input)
{
    input.dialogEdit.text.SetText({});
    input.dialogEdit.base.state = UI::HiddenWindow;
    if (input.focus == UI::ChatFocus::Dialog)
        input.focus = UI::ChatFocus::Game;
}

// 0x4b8886..0x4b88e2: con 0x77f3b6b en 0, los 0x100 bytes del campo en uso y su largo en 0, y el chat cerrado; con la
// entrada del número de serie (0x77f3b6b), nada (0x4b888f).
void ForgetWhatWasTyped(const WorldRuntime& runtime, UI::ChatInputState& input, UI::GameUiState& ui)
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

std::optional<UI::OldEditKey> EditKeyOf(Input::Key key)
{
    using Edit = UI::OldEditKey;
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

// strncmp(texto, comando, strlen(comando)) con el comando no vacío.
bool StartsWithTheCommand(std::string_view said, std::string_view command)
{
    return !command.empty() && said.starts_with(command);
}

// 0x4b56cc..0x4b571c y 0x5a11db..0x5a1204: los emotes (0x576eb0), salvo montado fuera de la zona segura. Los C1 18
// salen por `send`, como el chat.
Core::Expected<void, std::string> DoTheEmotes(const GameChatContext& context, std::string_view said)
{
    ActorState& hero = context.world.Actors()[context.world.Runtime().hero];
    if (hero.pet.model >= FirstMountModel && hero.pet.model <= LastMountModel && hero.safeZone == 0)
        return {};
    if (context.texts == nullptr)
        return Core::Unexpected(std::string("0x576eb0: los emotes sin Text.bmd"));
    const std::optional<UI::EmoteCase> emote = UI::FindTheEmote(*context.texts, said);
    if (!emote)
        return {};
    // 0x576abf→0x4ea030 lee el arreglo global: los modelos cargados por el viewport también están disponibles.
    // Fauna conserva esos recursos por separado y puede existir antes de ligar los modelos del primer dibujo.
    const Render::Models::IModelLibrary* models = context.world.FaunaResources().SkeletonOf(hero.type) != nullptr
                                                      ? &context.world.FaunaResources()
                                                      : context.models;
    if (models == nullptr)
        return Core::Unexpected(std::string("0x576a60: el emote sin los modelos (0x4ea030)"));
    const ActionSender send = [&context](std::uint8_t direction, std::uint8_t action)
    {
        if (context.send)
            context.send(Protocol::Movement::WriteAction(direction, action));
    };
    return DoTheEmote(*emote, EmoteContext{.hero = hero, .models = *models, .send = send});
}

// Los pedidos a otro de 0x56c440 (RunTheCommand ya miró Text.bmd).
SocialRequestContext SocialRequestsOf(const GameChatContext& context)
{
    return SocialRequestContext{
        .world = context.world, .random = context.random, .texts = *context.texts, .send = context.send};
}

// Lo que 0x56c440 lee y escribe.
Core::Expected<UI::ChatCommandOutcome, std::string> RunTheCommand(const GameChatContext& context, std::string_view text,
                                                                  bool fromMacro)
{
    if (context.texts == nullptr)
        return Core::Unexpected(std::string("0x56c440: los comandos sin Text.bmd"));
    WorldRuntime& runtime = context.world.Runtime();
    GamePanels panels(context.world, context.random);
    const UI::ChatCommandContext commands{
        .texts = *context.texts,
        .items = context.items,
        .setOptions = context.setOptions,
        .map = runtime.map,
        .logoutRequested = runtime.ui.logoutRequested,
        .warehouseOpen = panels.Registry().IsOpen(UI::WarehousePanel),
        .log = runtime.chat.log,
        .macros = runtime.chat.input.macros,
        .overlayMode = runtime.panels.overlayMode,
        .helpItem = runtime.panels.helpItem,
        .setLabel = &context.world.ItemSets().byte9305,
        .signsShown = &runtime.social.personalShop.signsShown,
        .ownLevel = [&context]() -> std::uint16_t
        {
            WorldRuntime& own = context.world.Runtime();
            const Render::Models::LogicalAccess access(own.ownRecordDepth, context.random,
                                                       Render::Models::OwnRecordCloseRandoms);
            return context.world.OwnCharacter().level;
        },
        .duelActive = runtime.social.duel.active != 0,
        .endTheDuel = [&context]() -> Core::Expected<void, std::string>
        {
            if (!context.send)
                return Core::Unexpected(std::string("0x56ea6c: /duelend sin el cable"));
            // El relleno y el serial del C3.
            context.random.Discard(2);
            context.send(Protocol::Duel::WriteDuelEnd());
            return {};
        },
        .requestATrade = [&context]() { return RequestATrade(SocialRequestsOf(context)); },
        .requestAParty = [&context]() { return RequestAParty(SocialRequestsOf(context)); },
        .requestADuel = [&context]() { return RequestADuel(SocialRequestsOf(context)); },
        .trainerOpen = panels.Registry().IsOpen(UI::TrainerPanel),
        .openThePersonalShop = [&context]() -> Core::Expected<void, std::string>
        {
            // 0x65ac3f: abrir la propia cierra lo demás menos el inventario, con el 3D y el 31.
            GamePanels shops(context.world, context.random);
            shops.LendTheSession(context.world, context.random, context.send, context.sendWindowClose);
            auto opened = shops.Registry().Open(UI::PersonalShopPanel);
            if (!opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
            return {};
        },
        .requestShopItems = [&context](std::optional<std::string_view> name)
        { return RequestTheShopItems(SocialRequestsOf(context), name); },
        .requestAGuildJoin = [&context]() { return RequestAGuildJoin(context); },
        .requestAGuildRelation = [&context](UI::GuildRelationCommand command)
        { return RequestAGuildRelation(context, command); }};
    auto outcome = UI::RunTheChatCommand(commands, text, fromMacro);
    if (!outcome.HasValue())
        return outcome;
    for (const UI::ChatCommandNotice& notice : outcome.Value().notices)
    {
        auto added = notice.formatted ? AddMessage(context, "", *notice.formatted, notice.type, "0x56c440")
                                      : AddNotice(context, notice.text, notice.type, "0x56c440");
        if (!added.HasValue())
            return Core::Unexpected(added.Error());
    }
    if (outcome.Value().sound)
        runtime.RequestSound(UI::ChatButtonSound);
    return outcome;
}

// 0x622b40(texto, 1): con alguna palabra de Filter.bmd, el texto 570.
Core::Expected<std::string, std::string> FilteredOf(const GameChatContext& context, std::string_view said)
{
    if (!UI::ContainsAFilteredWord(said, context.filter, true))
        return std::string(said);
    auto filtered = TextOf(context, FilteredText, "0x4b566c");
    if (!filtered.HasValue())
        return Core::Unexpected(filtered.Error());
    return std::string(filtered.Value());
}

} // namespace

// 0x6a0c01 (atoi) → 0x6a0b76 (atol de la CRT): los blancos de isspace, un '-' o un '+', y las cifras de isdigit, con la
// cuenta en 32 bits que da la vuelta sin aviso; con '-', negada. Con el locale "C" (sin setlocale, como el sscanf de
// 0x574c8c): los blancos son el espacio y de 0x09 a 0x0d, y las cifras de '0' a '9'.
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
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    const ActorState& hero = context.world.Actors()[runtime.hero];
    if (input.brake > BrakeLimit)
        return {};
    if (input.brake > 0 && input.lastSaid == said)
        return {};
    input.lastSaid = std::string(said);
    input.brake = BrakeSteps;
    if (UI::ContainsLikeTheOriginal(HeroNameOf(hero), WebzenWord) &&
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
                return AddNotice(context, WhisperOffNoticeText, UI::SystemMessage, "0x4b5aa1");
            }
            auto on = TextOf(context, WhisperOnCommandText, "0x4b5aae");
            if (!on.HasValue())
                return Core::Unexpected(on.Error());
            if (StartsWithTheCommand(said, on.Value()))
            {
                runtime.chat.whisperBlocked = false;
                return AddNotice(context, WhisperOnNoticeText, UI::SystemMessage, "0x4b5b00");
            }
        }
    }
    else
    {
        auto move = TextOf(context, UI::MoveCommandText, "0x4b59f4");
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
// 0x554a40(6, 0x7798cf8): el nivel del registro propio (+0xe), con su acceso, o el nombre en uno de los diez lugares
// de 0x77997f8 (strcmp, que sigue en el lugar de al lado si el nombre llena el suyo); si no, el aviso 479.
Core::Expected<bool, std::string> MayWhisper(const GameChatContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::ChatInputState& input = runtime.chat.input;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        if (context.world.OwnCharacter().level >= WhisperLevel)
            return true;
    }
    const std::string_view target = UI::CStringOf(input.whisperBuffer);
    for (std::size_t slot = 0; slot < UI::WhisperSenderSlots; ++slot)
    {
        if (UI::WhisperSenderAt(runtime.chat.whisperSenders, slot) == target)
            return true;
    }
    auto added = AddNotice(context, WhisperDeniedText, UI::SystemMessage, "0x554d0b");
    if (!added.HasValue())
        return Core::Unexpected(added.Error());
    return false;
}

// 0x4b6f59..0x4b883e: el susurro. Sin "webzen" en el nombre del héroe, el C1 02 al nombre del buffer (sus diez bytes)
// y el último destinatario (0x57678c4); con o sin él, lo dicho en el registro con el nombre del héroe y el tipo 0.
Core::Expected<void, std::string> WhisperIt(const GameChatContext& context, std::string_view said)
{
    auto allowed = MayWhisper(context);
    if (!allowed.HasValue())
        return Core::Unexpected(allowed.Error());
    if (!allowed.Value())
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    const ActorState& hero = context.world.Actors()[runtime.hero];
    if (!UI::ContainsLikeTheOriginal(HeroNameOf(hero), WebzenWord))
    {
        std::array<std::byte, Protocol::Chat::ChatNameSize> target{};
        std::transform(input.whisperBuffer.begin(), input.whisperBuffer.begin() + Protocol::Chat::ChatNameSize,
                       target.begin(), [](char letter) { return static_cast<std::byte>(letter); });
        if (context.send)
            context.send(Protocol::Chat::WriteWhisper(target, said));
        // 0x4b87e9: los diez bytes y el '\0' de 0x57678ce, como los lee el 0C.
        const std::string_view sent(input.whisperBuffer.data(), Protocol::Chat::ChatNameSize);
        runtime.chat.lastWhisperTarget = std::string(sent.substr(0, sent.find('\0')));
    }
    return AddMessage(context, HeroNameOf(hero), said, UI::WhisperMessage, "0x4b8839");
}

// 0x4b5624..0x4b883e: lo escrito, con el campo ya leído. Devuelve si sigue en 0x4b8841 (todo menos un comando que
// lo atendió).
Core::Expected<bool, std::string> SayWhatWasTyped(const GameChatContext& context)
{
    UI::ChatInputState& input = context.world.Runtime().chat.input;
    auto command = RunTheCommand(context, UI::CStringOf(input.messageBuffer), false);
    if (!command.HasValue())
        return Core::Unexpected(command.Error());
    if (command.Value().handled)
        return false;
    auto said = FilteredOf(context, UI::CStringOf(input.messageBuffer));
    if (!said.HasValue())
        return Core::Unexpected(said.Error());
    // 0x4b566c: strcpy al buffer, que deja lo de detrás.
    UI::CopyIntoTheBuffer(input.messageBuffer, said.Value());
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
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    constexpr std::string_view Keys{"\t\r\b", 3};
    std::string_view rest = typed;
    while (!rest.empty())
    {
        const std::size_t key = rest.find_first_of(Keys);
        // El EDIT de una ventana vieja con el foco de Windows (CL9 hito 9) recibe el WM_CHAR, y su subclase (0x467b10)
        // le pasa el Enter a su padre y, con padre, el Tab al EDIT de +0x98 (0x467cdf..0x467d28; solo los enlaza la de
        // escribir una carta, CL9 hito 10). Sin el de +0x98, el Tab no hace nada (P-C181).
        const bool oldEdit = UI::HasAnOldEditFocused(runtime.oldWindows, input.focus);
        // El EDIT de los diálogos con el foco: el de [0x5763504] (la entrada del número de serie, hito 11; el nombre
        // del gremio de la ventana 2, CL9 hito 8) o el de [0x5763508] (el número del 0x7d, CL9 hito 8). La misma
        // subclase, con el filtro de +0x4c.
        UI::OldEdit* dialogEdit = nullptr;
        if (input.focus == UI::ChatFocus::Dialog)
            dialogEdit = &input.dialogEdit;
        else if (input.focus == UI::ChatFocus::NumberField)
            dialogEdit = &input.numberField;
        // 0x467b9d: con el WM_CHAR, la subclase pone en 0 los dibujos del EDIT (+0x80, el cursor prendido). MuRet lo
        // hace con los dos EDIT de los diálogos (CL9 hito 8, P-C286), los de la escena que se dibujan con 0x468320 y no
        // son de una ventana vieja.
        if (dialogEdit != nullptr)
            dialogEdit->draws = 0;
        if (const std::string_view run = rest.substr(0, key); !run.empty())
        {
            if (oldEdit)
                UI::TypeInTheOldEdit(runtime.oldWindows, run);
            else
                UI::TypeInTheChat(input, run);
        }
        if (key == std::string_view::npos)
            break;
        const char pressed = rest[key];
        rest.remove_prefix(key + 1);
        if (oldEdit)
        {
            if (pressed == Input::EnterChar)
                UI::PressEnterInTheOldEdit(runtime.oldWindows);
            else if (pressed == Input::BackspaceChar)
                UI::BackspaceInTheOldEdit(runtime.oldWindows);
            else if (pressed == Input::TabChar)
                UI::ApplyKeyToTheOldEdit(runtime.oldWindows, UI::OldEditKey::Tab, false, false, context.clipboard,
                                         context.measure, &runtime.ui.focusedList);
            continue;
        }
        if (UI::OldEdit* edit = UI::ChatEditWithFocus(input); edit != nullptr && !edit->compositionText.empty())
        {
            edit->draws = 0;
            continue;
        }
        if (dialogEdit != nullptr)
        {
            // 0x467b5d..0x467c19: sin padre (+0x44 en 0), el Tab y el Enter van como WM_CHAR a la ventana del juego
            // (+0x84); el Enter no con la lista de ventanas armada (+0x150 de [0x5763524]) ni con +0xb4 (la vtable +
            // 0x5c, 0x4676d0) o +0xc4 en 1, que nadie prende en este EDIT. El borrar es del EDIT.
            if (pressed == Input::BackspaceChar)
            {
                UI::BackspaceInTheChat(input);
                continue;
            }
            if (pressed == Input::EnterChar && runtime.oldWindows.windowList.list.armed == 1)
                continue;
        }
        if (pressed == Input::TabChar)
        {
            if (UI::PressTabInTheChat(input, runtime.ui.chatOpen, &runtime.ui.focusedList))
                runtime.RequestSound(UI::ChatButtonSound);
        }
        else if (pressed == Input::EnterChar)
        {
            if (auto entered = PressEnterInTheGame(context); !entered.HasValue())
                return entered;
        }
        else
            UI::BackspaceInTheChat(input);
    }
    return {};
}

Core::Expected<void, std::string> TypeTheGameInput(const GameChatContext& context,
                                                   std::span<const Input::TextInputEvent> events)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    for (const Input::TextInputEvent& event : events)
    {
        if (event.kind == Input::TextInputEvent::Kind::Text)
        {
            if (auto typed = TypeInTheGameChat(context, event.text); !typed.HasValue())
                return typed;
            continue;
        }
        const bool oldEdit = UI::HasAnOldEditFocused(runtime.oldWindows, input.focus);
        UI::OldEdit* chatEdit = UI::ChatEditWithFocus(input);
        if (event.kind == Input::TextInputEvent::Kind::Composition)
        {
            if (oldEdit)
                UI::SetTheOldEditComposition(runtime.oldWindows, event.text, event.compositionStart,
                                             event.compositionLength);
            else if (chatEdit != nullptr)
                UI::SetTheEditComposition(*chatEdit, event.text, event.compositionStart, event.compositionLength);
            continue;
        }
        const auto editKey = EditKeyOf(event.key);
        if (oldEdit)
        {
            if (event.key == Input::Key::Enter)
                UI::PressEnterInTheOldEdit(runtime.oldWindows);
            else if (editKey)
                UI::ApplyKeyToTheOldEdit(runtime.oldWindows, *editKey, event.shift, event.ctrl, context.clipboard,
                                         context.measure, &runtime.ui.focusedList);
            continue;
        }
        // SDL entrega teclas durante el preedit. El mismo guard del EDIT evita editar o enviar antes del commit;
        // esto no sustituye la traducción de mensajes de un IME de Windows XP.
        if (chatEdit != nullptr && !chatEdit->compositionText.empty())
        {
            if (editKey)
                UI::ApplyKeyToTheEdit(*chatEdit, *editKey, event.shift, event.ctrl, context.clipboard, context.measure);
            continue;
        }
        if (chatEdit != nullptr && editKey && event.key != Input::Key::Tab)
        {
            UI::ApplyKeyToTheEdit(*chatEdit, *editKey, event.shift, event.ctrl, context.clipboard, context.measure);
            continue;
        }
        // Los EDIT sin padre reenvían Tab y Enter a la ventana del juego (0x467b5d..0x467c19).
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
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    UI::ChatInputState& input = runtime.chat.input;
    const ActorState& hero = context.world.Actors()[runtime.hero];
    // 0x4b50c0..0x4b5106: en Chaos Castle fuera de la zona segura, nada; si no, 0x5763588 en 1, el Enter que leen los
    // diálogos (0x6263df y 0x62da58). La escena es la 5; con 0x77f3b68, el campo del alta del gremio (CL9 hito 8),
    // nada más (0x4b5115).
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
        GamePanels panels(context.world, context.random);
        panels.LendTheSession(context.world, context.random, context.send, context.sendWindowClose);
        auto taken = panels.Registry().PanelKey(EnterVirtualKey);
        if (!taken.HasValue())
            return Core::Unexpected(taken.Error());
        if (taken.Value())
            return {};
    }
    // 0x4b5198..0x4b5334: el Enter es de las ventanas viejas con la lista de ventanas armada (+0x150 de [0x5763524]),
    // con la caja modal (0x5762ebc) arriba de todas (el último de +0x84) o con una caja no modal (+0xb8 de
    // [0x5763514], 0x4918a0) arriba de todas. 0x4b533d..0x4b5360: con el foco fuera de la ventana del juego, 0x467360
    // guarda el modo del IME, que MuRet no tiene.
    const UI::OldWindowManager& old = runtime.oldWindows;
    if (old.windowList.list.armed == 1)
        return {};
    const std::int32_t top = UI::LastOldWindow(old);
    if (old.modal != 0 && top == old.modal)
        return {};
    if (!old.boxes.empty() && std::ranges::find(old.boxes, top) != old.boxes.end())
        return {};
    // 0x4b5397..0x4b5435: con el campo cerrado, el texto del EDIT de [0x5763504] (el de la entrada del número de serie,
    // hito 11, y el de los diálogos 0x73 y 0x91, hito 7); con él abierto, los dos EDIT y lo escrito al historial (la
    // vtable + 8).
    if (!ui.chatOpen)
        UI::CopyIntoTheBuffer(input.messageBuffer, input.dialogEdit.text.Value());
    else
    {
        UI::ReadTheChatFields(input);
        UI::RememberTheMessage(input, UI::CStringOf(input.messageBuffer));
        input.whisperLength = LengthOf(input.whisperBuffer);
    }
    input.messageLength = LengthOf(input.messageBuffer);
    // 0x4b5468..0x4b5554: el campo vacío lo cierra o, cerrado, lo abre, sin vaciar nada; con el diálogo 0x73, en vez de
    // abrirlo, vacía el EDIT del diálogo y le da el foco.
    if (input.messageLength == 0 && !input.otherInput)
    {
        if (ui.chatOpen)
        {
            ui.chatOpen = false;
            UI::ShowTheChatFields(input, Hide, &runtime.ui.focusedList);
        }
        else if (runtime.dialogs.current == MoneyDialog)
        {
            // 0x4b54e1..0x4b5513: la vtable + 0x4c del EDIT de [0x5763504] con 0 (vacío) y SetFocus de la ventana del
            // juego ([0x5763544]). El dibujo del 0x73 le devuelve el foco al EDIT en el frame siguiente (0x6308b8).
            input.dialogEdit.text.SetText({});
            input.focus = UI::ChatFocus::Game;
        }
        else
        {
            // 0x4b5515..0x4b554d: 0x77f31c0 en 0 es de la entrada sin EDIT (0x6c7b8c en 0).
            runtime.social.dialogFieldLimit = ClosedDialogFieldLimit;
            input.fieldCount = UI::ChatFieldCount;
            runtime.social.moneyField = 0;
            UI::ShowTheChatFields(input, ShowFocused, &runtime.ui.focusedList);
            ui.chatOpen = true;
        }
        return {};
    }
    input.field = UI::MessageField;
    // 0x4b5563..0x4b55ae: con el modo del zen (0x77f3b6a, el diálogo 0x73 o el precio del 0x91), lo escrito es un
    // número (atoi a 0x77f3b6c) y no se dice; después la vtable + 0x4c del EDIT de [0x5763504] con 0 (vacío), la + 4
    // con 3 (oculto, ShowWindow(SW_HIDE): con el foco en él, el foco vuelve a la ventana del juego, *inferido* como en
    // UI::ShowTheChatFields) y el salto a 0x4b8886.
    if (runtime.social.moneyField != 0)
    {
        runtime.social.typedMoney = AtoiLikeTheCrt(UI::CStringOf(input.messageBuffer));
        HideTheDialogEdit(input);
        ForgetWhatWasTyped(runtime, input, ui);
        return {};
    }
    // 0x4b55b3..0x4b560f: con 0x78aba60 en 1, lo escrito es el título de la tienda personal y no se dice: con 0x5ebbd0
    // el strcpy a 0x78aba64; si no, el texto 1130 del tipo 1 sin nombre (0x6e57a4, ""). Después, el salto a 0x4b8886.
    if (runtime.social.personalShop.dialogMode == ShopTitleDialogMode)
    {
        const std::string_view typed = UI::CStringOf(input.messageBuffer);
        if (UI::IsAShopTitle(typed, context.filter))
            runtime.social.personalShop.ownTitle = std::string(typed);
        else if (auto said = AddNotice(context, BadShopTitleText, UI::SystemMessage, "0x4b5607"); !said.HasValue())
            return said;
        ForgetWhatWasTyped(runtime, input, ui);
        return {};
    }
    // 0x4b5614..0x4b561e y 0x4b8888: con la entrada del número de serie (0x77f3b6b), nada más: ni se dice, ni se vacía
    // el buffer, ni se cierra el campo.
    if (runtime.events.serialEntry)
        return {};
    auto said = SayWhatWasTyped(context);
    if (!said.HasValue())
        return Core::Unexpected(said.Error());
    if (said.Value())
        runtime.chat.area.firstShown = UI::FirstShownRow;
    // 0x4b884b..0x4b8883.
    UI::SetTheChatFields(input, std::string_view{}, std::nullopt);
    UI::ShowTheChatFields(input, Hide, &runtime.ui.focusedList);
    UI::ScrollTheLog(runtime.chat.log, ui.chatModes, ScrollAfterSaying);
    ForgetWhatWasTyped(runtime, input, ui);
    return {};
}

Core::Expected<void, std::string> SayTheMacros(const GameChatContext& context,
                                               const std::array<bool, UI::MacroCount>& pressed)
{
    WorldRuntime& runtime = context.world.Runtime();
    for (std::size_t macro = 0; macro < UI::MacroCount; ++macro)
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
        runtime.hud.macroTime = UI::MacroLockSteps;
    }
    return {};
}
} // namespace Client::Scenes
