#include "Scenes/GameCommandsWindow.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Protocol/Duel.h"
#include "Protocol/PersonalShop.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialState.h"
#include "Scenes/SocialRequests.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatCommands.h"
#include "UI/ChatInput.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Color;
using Render::Text::FontKind;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
// glColor3f(0,5; 0,5; 0,5): el botón del elegido (0x40a726) y el nombre con el clic (0x40a967).
constexpr Color Gray{0.5f, 0.5f, 0.5f, 1.0f};
// 0x40ab9a: el nombre junto al mouse sin el comando listo para él.
constexpr Color NotReady{1.0f, 0.2f, 0.2f, 1.0f};
// 0x40aad6..0x40ab19: Interface\CursorID_Wnd.jpg (0x65b) de 128 × 32 en el mouse + 5 (0x6b87f8).
constexpr std::uint16_t NameCardSlot = 0x65b;
constexpr double NameCardOffset = 5.0;
constexpr float NameCardWidth = 128.0f;
constexpr float NameCardHeight = 32.0f;
// 0x40ab2e..0x40ab6b y 0x40abab..0x40abd3: los colores y los lugares de los dos nombres.
constexpr std::uint32_t ChosenNameColor = 0xff67bfdfu;
constexpr std::uint32_t NameCardBack = 0x00141414u;
constexpr std::int32_t ChosenNameTop = 0x2d;
constexpr std::int32_t NameNextToMouseX = 0x45;
constexpr std::int32_t NameNextToMouseY = 0xc;
// glColor3f(1; 0,8; 0,2): el nombre del elegido o con el mouse encima, sin el clic (0x40a978).
constexpr Color Highlight{1.0f, 0.8f, 0.2f, 1.0f};
// 0x491960(0, 1): glColor4ub(0x92, 0x86, 0x79, 0xff), el marco de la casilla (0x4630a5).
constexpr Color CheckboxFrame{static_cast<float>(0x92) / 255.0f, static_cast<float>(0x86) / 255.0f,
                              static_cast<float>(0x79) / 255.0f, 1.0f};

// 0x40a668..0x40a676: la esquina que pone el dibujo.
constexpr std::int32_t WindowLeft = 0x1c2;
constexpr std::int32_t WindowTop = 0;
// 0x40a463..0x40a48f: el mouse sobre la ventana, de 0xbe × 0x1b1 desde la esquina.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x40a51f..0x40a548: el botón de cerrar, de 0x18 × 0x18 en (0x19, 0x18b) de la esquina.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x40a573..0x40a57d.
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x40a553..0x40a56b: los dos sonidos del botón de cerrar.
constexpr std::int32_t FirstCloseSound = 0x19;
constexpr std::int32_t SecondCloseSound = 0x1c;

// Los botones: x + 95 (0x6b87e4) − 50 (0x6b86b0) e y + 53 (0x6b87e0), de 28 en 28 (0x6b880c). El paso los toca entre
// los bordes, con 108 (0x6c) × 20 (0x14); el dibujo los pinta de 100 × 25 y les mira el mouse con 108 (0x6b87fc) ×
// 20 (0x6b86d0).
constexpr double ButtonsRight = 95.0;
constexpr double ButtonsBack = 50.0;
constexpr double ButtonsTop = 53.0;
constexpr std::int32_t ButtonStep = 0x1c;
constexpr float ButtonStepF = 28.0f;
constexpr std::int32_t ButtonHitWidth = 0x6c;
constexpr std::int32_t ButtonHitHeight = 0x14;
constexpr float ButtonHoverWidth = 108.0f;
constexpr float ButtonHoverHeight = 20.0f;
// 0x40a74f..0x40a774: Interface\Message_box.jpg (0xf0) de 100 × 25 con du = 0,83203125.
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr float ButtonWidth = 100.0f;
constexpr float ButtonHeight = 25.0f;
constexpr float ButtonDu = 0.83203125f;
// 0x40aa0f y 0x40aa06: el nombre, centrado en _ftol(x) + 0x32 y en _ftol(y) + 8; 0x40aa3a: la casilla en _ftol(x) −
// 0x14.
constexpr std::int32_t NameCenter = 0x32;
constexpr std::int32_t NameTop = 8;
constexpr std::int32_t CheckboxBack = 0x14;
// 0x463090: la casilla de 9 × 9 y la marca, Interface\win_check.tga (0x64a), de 5 × 5 en (x + 2, y + 2).
constexpr float CheckboxSize = 9.0f;
constexpr std::int32_t CheckboxFar = 8;
constexpr std::uint16_t CheckSlot = 0x64a;
constexpr std::int32_t CheckInset = 2;
constexpr float CheckSize = 5.0f;
constexpr float CheckUv = 0.625f;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
constexpr std::size_t TitleText = 938;
constexpr std::size_t CloseHelpText = 927;
// 0x40a998: la tabla 0x40aca4, el nombre de los comandos del 1 al 11.
constexpr std::array<std::size_t, UI::CommandCount - 1> CommandTexts{943,  1124, 944, 945, 946, 1352,
                                                                     1321, 1322, 947, 948, 949};
// 0x40a853: el "D" (0x6c6358) que sprintf pone en el texto 927, en un buffer de 100 bytes de la pila (EBP − 0x6c).
constexpr std::string_view CloseKey = "D";
constexpr std::size_t HelpBufferSize = 100;
// 0x40a6c9..0x40a6f2: el título en (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
// 0x40ac60..0x40ac7b: el aviso centrado en (x + 0x5f, y + 0x44).
constexpr std::int32_t NoticeCenter = 0x5f;
constexpr std::int32_t NoticeTop = 0x44;

// 0x6c8e5c y 0x6c8e64 del título (0x40a6b5) y de los nombres (0x40a89f).
constexpr Render::Text::TextColors TitleColors{.text = 0xffffffffu, .background = 0xff141414u};
constexpr Render::Text::TextColors NameColors{.text = 0xffffffffu, .background = 0x00141414u};

// El aviso de 0x40abdb..0x40ac2e: con el estado del elegido en 0, el texto del comando 5, 6, 10 o 3; con 0xff o 0xfe,
// los de esos estados.
constexpr std::uint8_t RefusedState = 0;
constexpr std::uint8_t NotMasterState = 0xff;
constexpr std::uint8_t NotMemberState = 0xfe;

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

// FSTP float: la suma de la x87 guardada en la pila como float.
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x40a4ad y 0x40a705: fild de la esquina, fadd de 95 y fsub de 50.
double ButtonsLeftOf(std::int32_t x)
{
    return static_cast<double>(x) + ButtonsRight - ButtonsBack;
}

// 0x4061b7 y 0x4061c3: los estados que pone 0x406050 por comando antes del clic.
constexpr std::size_t PartyCommand = 3;
constexpr std::size_t WhisperCommand = 4;
constexpr std::size_t GuildCommand = 5;
constexpr std::size_t AllianceCommand = 6;
constexpr std::size_t HostilityCommand = 7;
constexpr std::size_t CancelHostilityCommand = 8;
constexpr std::size_t FriendCommand = 9;
constexpr std::size_t FollowCommand = 10;
constexpr std::size_t TradeCommand = 1;
constexpr std::size_t ShopCommand = 2;
constexpr std::size_t DuelCommand = 11;
// 0x4062bb: +0x368 en 0x80, el maestro del gremio.
constexpr std::uint8_t GuildMaster = 0x80;
// 0x406195 y 0x40619a: a lo sumo dos celdas en cada eje.
constexpr std::int32_t CommandReach = 2;
// Los textos 0x76e1398 y 0x76cff08 (el 478 y el 915, UI::ChatCommands).
constexpr std::size_t PersonalShopText = 493;
constexpr std::size_t NotTheLeaderText = 257;

void ResetTheStates(UI::CommandsWindowState& window)
{
    window.states.fill(UI::CommandReady);
}

// 0x406af0..0x406afa y 0x4072ae..0x4072bb: la ventana se cierra (+4), sin destino (+8) ni comando (+0x20); después
// 0x4072be pone los doce estados en 1.
void CloseAfterTheCommand(UI::DelegatedPanels& delegated)
{
    delegated.commands = 0;
    delegated.commandsWindow.target = -1;
    delegated.commandsWindow.chosen = -1;
    ResetTheStates(delegated.commandsWindow);
}

// 0x4061b7..0x406344: lo que puede el comando elegido con el personaje elegido, en su estado (+0x14 + comando): 1 se
// puede, 0 no, 0xff el otro no es el maestro de un gremio, 0xfe el héroe tampoco. El 9 queda siempre en 0.
Core::Expected<void, std::string> SetTheStates(const WorldState& world, UI::CommandsWindowState& window,
                                               std::size_t index)
{
    const ActorState& target = world.Actors()[index];
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    // 0x4061b7: el 9 (texto 947, amigos) en 0; su caso (0x40a0e1) no se alcanza.
    window.states[FriendCommand] = 0;
    auto& states = window.states;
    switch (static_cast<std::size_t>(window.chosen))
    {
    case PartyCommand:
    case FollowCommand:
        // 0x4061d3..0x406275: invitarlo si no está en la party; seguirlo si está.
        states[PartyCommand] = UI::CommandReady;
        states[FollowCommand] = 0;
        if (IsInTheParty(world, index))
        {
            states[PartyCommand] = 0;
            states[FollowCommand] = UI::CommandReady;
        }
        return {};
    case GuildCommand:
        // 0x40627a..0x4062b1: el héroe sin gremio (+0x1f2 negativo) y el otro maestro del suyo.
        states[GuildCommand] = UI::CommandReady;
        if (static_cast<std::int16_t>(hero.word1f2) >= 0)
            states[GuildCommand] = 0;
        else if (static_cast<std::int16_t>(target.word1f2) < 0 || target.guildStatus != GuildMaster)
            states[GuildCommand] = NotMasterState;
        return {};
    case AllianceCommand:
        // 0x4062b6..0x4062f9: los dos maestros y, en la tabla de los gremios, el +4 del del héroe.
        if (hero.guildStatus != GuildMaster)
            states[AllianceCommand] = NotMemberState;
        else if (target.guildStatus != GuildMaster)
            states[AllianceCommand] = NotMasterState;
        else
            return Core::Unexpected(std::string(
                "0x4062e4: el comando 6 con dos maestros lee la tabla de los gremios (0x787aa4c), que es del hito 8"));
        return {};
    case HostilityCommand:
    case CancelHostilityCommand:
    {
        // 0x4062fb..0x406344: los dos maestros.
        const std::size_t command = static_cast<std::size_t>(window.chosen);
        if (hero.guildStatus != GuildMaster)
            states[command] = NotMemberState;
        else if (target.guildStatus != GuildMaster)
            states[command] = NotMasterState;
        else
            states[command] = UI::CommandReady;
        return {};
    }
    default:
        // 0x4061c6: el 1, el 2, el 4, el 9 y el 11 no tocan nada.
        return {};
    }
}

// 0x555190("", texto, tipo) o el texto ya armado, en los mensajes, con la entrada del chat.
Core::Expected<void, std::string> Notify(const GameChatContext& chat, const UI::ChatCommandNotice& notice,
                                         std::string_view where)
{
    if (chat.measure == nullptr)
        return Core::Unexpected(std::format("{}: 0x555190 sin el medidor de los textos", where));
    std::string text;
    if (notice.formatted)
    {
        text = *notice.formatted;
    }
    else
    {
        const auto found = UI::TextAt(chat.texts, notice.text);
        if (!found)
            return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, notice.text));
        text = std::string(*found);
    }
    return AddGameMessage(chat.world.Runtime(), *chat.measure, chat.screen, "", text, notice.type);
}

Core::Expected<void, std::string> NotifyAll(const GameChatContext& chat, const UI::ChatCommandOutcome& outcome,
                                            std::string_view where)
{
    for (const UI::ChatCommandNotice& notice : outcome.notices)
    {
        if (auto added = Notify(chat, notice, where); !added.HasValue())
            return added;
    }
    return {};
}

// Un aviso de Text.bmd y el comando en −1 (+0x20): el nivel (0x40646a), la tienda (0x4064aa), la party (0x407326) y el
// duelo (0x4085a5).
Core::Expected<void, std::string> Refuse(const GameChatContext& chat, UI::CommandsWindowState& window, std::size_t text,
                                         std::int32_t type, std::string_view where)
{
    if (auto added = Notify(chat, UI::ChatCommandNotice{.text = text, .type = type}, where); !added.HasValue())
        return added;
    window.chosen = -1;
    return {};
}

// 0x40638d..0x406afd, el comando 1 (texto 943): el nivel del registro propio (+0xe, con su acceso) contra 0x6c8dc8;
// el otro con tienda personal (0x451810); el 36 con su aviso; la ventana se cierra.
Core::Expected<void, std::string> CommandATrade(const GameUiContext& context, const GameChatContext& chat,
                                                std::size_t index)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    UI::DelegatedPanels& delegated = runtime.panels.delegated;
    std::uint16_t level = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    if (level < UI::DefaultTradeLevel)
        return Refuse(chat, delegated.commandsWindow, UI::TradeLevelText, UI::SystemMessage, "0x406476");
    if (HasShopTitle(runtime.social.personalShop, index))
        return Refuse(chat, delegated.commandsWindow, PersonalShopText, UI::ErrorMessage, "0x4064b6");
    const SocialRequestContext requests{
        .world = world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    auto sent = SendATradeRequest(requests, index, "0x4064f0");
    if (!sent.HasValue())
        return Core::Unexpected(sent.Error());
    if (auto added = NotifyAll(chat, sent.Value(), "0x406ae5"); !added.HasValue())
        return added;
    CloseAfterTheCommand(delegated);
    return {};
}

// 0x406b02..0x4072be, el comando 2 (texto 1124): el C1 10 3F 05 con la clave y el nombre del otro, en C3, y la ventana
// se cierra. Lo que contesta el servidor (el 3F 05 que llega) no tiene handler (08 P-C208 b).
Core::Expected<void, std::string> CommandAShopView(const GameUiContext& context, const GameChatContext& chat,
                                                   std::size_t index)
{
    const ActorState& target = context.world.Actors()[index];
    const SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    if (auto sent =
            SendInC3(requests, Protocol::PersonalShop::WriteShopItemsRequest(target.key, target.name), "0x406b02");
        !sent.HasValue())
        return sent;
    CloseAfterTheCommand(context.world.Runtime().panels.delegated);
    return {};
}

// 0x4072df..0x4079a5, el comando 3 (texto 944): con otro líder, el aviso 257; si no, el 40 con su aviso. La ventana
// queda.
Core::Expected<void, std::string> CommandAParty(const GameUiContext& context, const GameChatContext& chat,
                                                std::size_t index)
{
    UI::CommandsWindowState& window = context.world.Runtime().panels.delegated.commandsWindow;
    if (AnotherLeadsTheParty(context.world))
        return Refuse(chat, window, NotTheLeaderText, UI::SystemMessage, "0x407332");
    const SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    auto sent = SendAPartyRequest(requests, index, "0x40736b");
    if (!sent.HasValue())
        return Core::Unexpected(sent.Error());
    return NotifyAll(chat, sent.Value(), "0x40798d");
}

// 0x40a04e..0x40a0e0, el comando 4 (texto 945): con 0x6c7b8c en 1 (.data), el nombre al EDIT del susurro
// (0x4694c0(0, 0, 1, nombre)), el campo elegido en 0 (0x77f3b70), el foco al del mensaje (0x469370(0)) y
// SendMessage(WM_CHAR, Enter) a la ventana del juego, que corre el Enter del WndProc en el acto. La ventana queda.
Core::Expected<void, std::string> CommandAWhisper(const GameUiContext& context, const GameChatContext& chat,
                                                  std::size_t index)
{
    UI::ChatInputState& input = context.world.Runtime().chat.input;
    UI::SetTheChatFields(input, std::nullopt, ChatNameOf(context.world.Actors()[index]));
    input.field = UI::MessageField;
    UI::FocusTheChatField(input, UI::MessageField);
    return PressEnterInTheGame(chat);
}

// 0x4079a6..0x4085d0, el comando 11 (texto 949): sin duelo, el AA con la clave y el nombre del otro; con el duelo (1) y
// el otro de rival (strcmp con 0x78abae0), el AB; si no, el aviso 915 con el 2. La ventana queda.
Core::Expected<void, std::string> CommandADuel(const GameUiContext& context, const GameChatContext& chat,
                                               std::size_t index)
{
    WorldRuntime& runtime = context.world.Runtime();
    const ActorState& target = context.world.Actors()[index];
    const SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    const std::uint8_t active = runtime.social.duel.active;
    if (active == 0)
        return SendInC3(requests, Protocol::Duel::WriteDuelStart(target.key, target.name), "0x4079b3");
    if (active == 1 && IsTheDuelRival(context.world, index))
        return SendInC3(requests, Protocol::Duel::WriteDuelEnd(), "0x408141");
    return Refuse(chat, runtime.panels.delegated.commandsWindow, UI::DuelBusyText, UI::ErrorMessage, "0x4085b1");
}

// 0x406050 (desde 0x40a499, con ECX = [0x6e592c]), en cada paso con la ventana abierta: los doce estados en 1 y, con un
// comando elegido y un personaje elegido (0x6c8e30), la distancia al héroe (sus cuatro accesos), el destino (+8) si es
// un jugador al que se le puede pedir algo a dos celdas o menos, los estados del comando y, con el clic derecho
// (0x7d36880) y un destino, el comando si su estado es 1.
Core::Expected<void, std::string> RunTheCommand(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::CommandsWindowState& window = runtime.panels.delegated.commandsWindow;
    ResetTheStates(window);
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (window.chosen == -1 || chosen < 0)
        return {};
    if (static_cast<std::size_t>(chosen) >= context.world.Actors().size())
        return Core::Unexpected(std::format("0x4060a4: el elegido {} lee fuera de los personajes", chosen));
    const auto index = static_cast<std::size_t>(chosen);
    window.target = -1;
    const CellDistance distance = CellDistanceToTheHero(context.world, context.random, index);
    if (CanBeAsked(context.world, index) && distance.x <= CommandReach && distance.y <= CommandReach)
        window.target = chosen;
    if (auto states = SetTheStates(context.world, window, index); !states.HasValue())
        return states;
    // 0x406348..0x406386: el clic derecho con un destino se gasta (con 0x7d3685c); el estado del comando, con signo.
    if (!runtime.ui.rightClick || window.target < 0)
        return {};
    runtime.ui.rightClick = false;
    runtime.ui.rightDown = false;
    if (window.chosen < 0 || static_cast<std::size_t>(window.chosen) >= window.states.size())
        return Core::Unexpected(std::format("0x406371: el comando {} lee fuera de los estados", window.chosen));
    const auto command = static_cast<std::size_t>(window.chosen);
    if (static_cast<std::int8_t>(window.states[command]) < 1)
        return {};
    const auto target = static_cast<std::size_t>(window.target);
    if (command == FollowCommand)
    {
        // 0x40a3e0: 0x6c8e50 = el destino. La ventana queda.
        runtime.social.party.followTarget = window.target;
        return {};
    }
    if (command == GuildCommand || command == AllianceCommand || command == HostilityCommand ||
        command == CancelHostilityCommand)
        return Core::Unexpected(std::format("{}: el comando {} sobre el elegido es de los gremios, del hito 8",
                                            command == GuildCommand       ? "0x4085d1"
                                            : command == AllianceCommand  ? "0x408a24"
                                            : command == HostilityCommand ? "0x409066"
                                                                          : "0x409663",
                                            command));
    if (context.chat == nullptr || context.chat->texts == nullptr)
        return Core::Unexpected(std::format("0x406386: el comando {} sin la entrada del chat ni Text.bmd", command));
    const GameChatContext& chat = *context.chat;
    switch (command)
    {
    case TradeCommand:
        return CommandATrade(context, chat, target);
    case ShopCommand:
        return CommandAShopView(context, chat, target);
    case PartyCommand:
        return CommandAParty(context, chat, target);
    case WhisperCommand:
        return CommandAWhisper(context, chat, target);
    case DuelCommand:
        return CommandADuel(context, chat, target);
    default:
        // 0x406380: el 0 no se elige; el 9 tiene el estado en 0.
        return {};
    }
}

// 0x463090(x, y, marcada): el marco de la casilla con cuatro rectángulos sin textura, sin mezcla, y la marca con la
// mezcla normal. Deja el color en blanco, la textura prendida y la mezcla normal.
void DrawTheCheckbox(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool checked)
{
    sprites.SetBlend(BlendMode::Opaque);
    sprites.SetColor(CheckboxFrame);
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    sprites.ColorQuad(left, top, CheckboxSize, 1.0f);
    sprites.ColorQuad(left, static_cast<float>(y + CheckboxFar), CheckboxSize, 1.0f);
    sprites.ColorQuad(left, top, 1.0f, CheckboxSize);
    sprites.ColorQuad(static_cast<float>(x + CheckboxFar), top, 1.0f, CheckboxSize);
    // 0x459ec0: glColor4f(1, 1, 1, 1) y glEnable(GL_TEXTURE_2D).
    sprites.SetColor(White);
    sprites.EnableTexture();
    sprites.SetBlend(BlendMode::Alpha);
    if (checked)
        WindowDrawing::Quad(sprites, CheckSlot, static_cast<float>(x + CheckInset), static_cast<float>(y + CheckInset),
                            CheckSize, CheckSize, CheckUv, CheckUv);
}

// 0x40a840..0x40a875: sprintf(buffer, texto 927, "D"). MuRet no usa el texto como formato: sin '%' y con menos de 100
// bytes, sprintf lo copia tal cual; con un '%' o más largo, corta (Q-C650).
Core::Expected<std::string_view, std::string> CloseHelpOf(const GameHudContext& context)
{
    auto format = TextOf(context, CloseHelpText, "0x40a85b");
    if (!format.HasValue())
        return format;
    const std::string_view text = format.Value();
    if (text.find('%') != std::string_view::npos)
        return Core::Unexpected(std::format("0x40a875: el texto {} es el formato de sprintf con \"{}\" y tiene un '%'",
                                            CloseHelpText, CloseKey));
    if (text.size() >= HelpBufferSize)
        return Core::Unexpected(std::format("0x40a875: el texto {} no entra en los {} bytes de la pila de sprintf",
                                            CloseHelpText, HelpBufferSize));
    return text;
}

// 0x40abdb..0x40ac2e: el texto del aviso del comando elegido, o 0.
std::size_t NoticeOf(const UI::CommandsWindowState& window)
{
    const auto chosen = static_cast<std::size_t>(window.chosen);
    const std::uint8_t state = window.states[chosen];
    if (state == RefusedState)
    {
        switch (window.chosen)
        {
        case 5:
            return 255;
        case 6:
            return 1313;
        case 10:
            return 937;
        case 3:
            return 257;
        default:
            return 0;
        }
    }
    if (state == NotMasterState)
        return 507;
    if (state == NotMemberState)
        return 1320;
    return 0;
}
} // namespace

Core::Expected<void, std::string> StepTheCommandsWindow(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DelegatedPanels& delegated = runtime.panels.delegated;
    if (delegated.commands == 0)
        return {};
    GameMovementState& movement = runtime.movement;
    const std::int32_t x = runtime.hud.commandsX;
    const std::int32_t y = runtime.hud.commandsY;
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    if (mouseX >= x && mouseX < x + WindowWidth && mouseY >= y && mouseY < y + WindowHeight)
        runtime.ui.mouseOnUi = 1;
    if (auto commanded = RunTheCommand(context); !commanded.HasValue())
        return commanded;
    // 0x40a49e..0x40a51d: el 0 no se elige; el clic se gasta en el primero que lo toma.
    const std::int32_t left = Truncate(ButtonsLeftOf(x));
    std::int32_t top = Truncate(static_cast<double>(y) + ButtonsTop);
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(UI::CommandCount); ++command)
    {
        if (command != 0 && mouseX > left && mouseX < left + ButtonHitWidth && mouseY > top &&
            mouseY < top + ButtonHitHeight && movement.pendingClick)
        {
            movement.pendingClick = false;
            movement.leftHeld = false;
            delegated.commandsWindow.chosen = command;
        }
        top += ButtonStep;
    }
    if (mouseX < x + CloseLeft || mouseX >= x + CloseLeft + CloseSize || mouseY < y + CloseTop ||
        mouseY >= y + CloseTop + CloseSize || !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    runtime.lastSoundId = FirstCloseSound;
    runtime.lastSoundId = SecondCloseSound;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
    UI::ResetTheCommands(delegated);
    return {};
}

Core::Expected<void, std::string> DrawTheCommandsWindow(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::DelegatedPanels& delegated = runtime.panels.delegated;
    if (delegated.commands == 0)
        return {};
    const UI::CommandsWindowState& window = delegated.commandsWindow;
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    hud.commandsX = WindowLeft;
    hud.commandsY = WindowTop;
    const std::int32_t x = hud.commandsX;
    const std::int32_t y = hud.commandsY;
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    auto title = TextOf(context, TitleText, "0x40a6ed");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    WindowDrawing::SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());

    // 0x40a6f9..0x40a78c: los once botones, la y sumada en float también por el 0.
    const auto buttonsLeft = static_cast<float>(ButtonsLeftOf(x));
    auto top = static_cast<float>(static_cast<double>(y) + ButtonsTop);
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(UI::CommandCount); ++command)
    {
        if (command != 0)
        {
            sprites.SetColor(window.chosen == command ? Gray : White);
            WindowDrawing::Quad(sprites, ButtonSlot, buttonsLeft, top, ButtonWidth, ButtonHeight, ButtonDu, 1.0f);
        }
        top = static_cast<float>(static_cast<double>(top) + static_cast<double>(ButtonStepF));
    }

    if (WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto help = CloseHelpOf(context);
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    }

    // 0x40a89f..0x40aa56: los nombres, con la fuente y el color del elegido o del que tiene el mouse encima, y la
    // casilla.
    hud.textColors = NameColors;
    top = static_cast<float>(static_cast<double>(y) + ButtonsTop);
    const UI::Point mouse = context.mouse;
    const bool click = runtime.movement.pendingClick;
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(UI::CommandCount); ++command)
    {
        if (command != 0)
        {
            const auto mouseX = static_cast<double>(mouse.x);
            const auto mouseY = static_cast<double>(mouse.y);
            const bool hovered = mouseX > static_cast<double>(buttonsLeft) &&
                                 mouseX < static_cast<double>(buttonsLeft) + static_cast<double>(ButtonHoverWidth) &&
                                 mouseY > static_cast<double>(top) &&
                                 mouseY < static_cast<double>(top) + static_cast<double>(ButtonHoverHeight);
            if (window.chosen != command && !hovered)
            {
                WindowDrawing::SelectFont(context, FontKind::Normal);
                sprites.SetColor(White);
            }
            else
            {
                WindowDrawing::SelectFont(context, FontKind::Bold);
                sprites.SetColor(click ? Gray : Highlight);
            }
            auto name = TextOf(context, CommandTexts[static_cast<std::size_t>(command - 1)], "0x40a9f6");
            if (!name.HasValue())
                return Core::Unexpected(std::move(name).Error());
            const std::int32_t left = Truncate(static_cast<double>(buttonsLeft));
            const std::int32_t row = Truncate(static_cast<double>(top));
            WindowDrawing::DrawCenteredText(context, left + NameCenter, row + NameTop, name.Value());
            DrawTheCheckbox(sprites, left - CheckboxBack, row + NameTop, window.chosen == command);
        }
        top = static_cast<float>(static_cast<double>(top) + static_cast<double>(ButtonStepF));
    }

    sprites.SetColor(White);
    WindowDrawing::SelectFont(context, FontKind::BigBold);
    // 0x40aa80..0x40abd8 (CL9 hito 7, fase 2): con un personaje elegido (0x6c8e30) que es un jugador (+0x88 en 1), no
    // el héroe publicado y del tipo 0x20a o con +0x3e3, el cartel (Interface\CursorID_Wnd.jpg, 0x65b) de 128 × 32 en
    // el mouse + 5, la mezcla normal y su nombre (+0x1d9) en la caja del título en (x + 0x23, y + 0x2d), en
    // 0xff67bfdf sobre 0x141414; y otra vez centrado en (mouse x + 0x45, mouse y + 0xc), en blanco sobre 0x141414,
    // con glColor blanco si el comando elegido es para él (+8) y está listo, o (1; 0,2; 0,2).
    if (const std::int32_t chosen = runtime.selectedTargets[2]; chosen >= 0)
    {
        if (static_cast<std::size_t>(chosen) >= context.world.Actors().size())
            return Core::Unexpected(std::format("0x40aa8d: el elegido {} lee fuera de los personajes", chosen));
        if (CanBeAsked(context.world, static_cast<std::size_t>(chosen)))
        {
            WindowDrawing::Quad(sprites, NameCardSlot, F32(static_cast<double>(mouse.x) + NameCardOffset),
                                F32(static_cast<double>(mouse.y) + NameCardOffset), NameCardWidth, NameCardHeight, 1.0f,
                                1.0f);
            sprites.SetBlend(BlendMode::Alpha);
            const std::string name = ChatNameOf(context.world.Actors()[static_cast<std::size_t>(chosen)]);
            hud.textColors = Render::Text::TextColors{.text = ChosenNameColor, .background = NameCardBack};
            WindowDrawing::DrawTitle(context, x + TitleLeft, y + ChosenNameTop, name);
            bool ready = false;
            if (window.target == chosen && window.chosen != -1)
            {
                if (window.chosen < 0 || window.chosen >= static_cast<std::int32_t>(UI::CommandCount))
                    return Core::Unexpected(
                        std::format("0x40ab87: el comando {} lee fuera de los doce estados", window.chosen));
                ready = window.states[static_cast<std::size_t>(window.chosen)] == UI::CommandReady;
            }
            sprites.SetColor(ready ? White : NotReady);
            hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = NameCardBack};
            WindowDrawing::DrawCenteredText(context, mouse.x + NameNextToMouseX, mouse.y + NameNextToMouseY, name);
        }
    }
    std::size_t notice = 0;
    if (window.chosen != -1)
    {
        if (window.chosen < 0 || window.chosen >= static_cast<std::int32_t>(UI::CommandCount))
            return Core::Unexpected(
                std::format("0x40abe5: el comando {} lee fuera de los doce estados", window.chosen));
        notice = NoticeOf(window);
    }
    sprites.SetColor(Black);
    if (notice != 0)
    {
        auto text = TextOf(context, notice, "0x40ac6c");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        WindowDrawing::SelectFont(context, FontKind::Bold);
        hud.textColors.background = 0;
        WindowDrawing::DrawCenteredText(context, x + NoticeCenter, y + NoticeTop, text.Value());
    }
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}
} // namespace Client::Scenes
