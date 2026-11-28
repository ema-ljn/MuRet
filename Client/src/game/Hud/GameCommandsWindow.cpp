#include "Hud/GameCommandsWindow.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Math/X87.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelId.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActionSend.h"
#include "Kernel/MouseState.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Chat/ChatReceive.h"
#include "Chat/GameChat.h"
#include "Hud/GameHud.h"
#include "Chat/GameMessages.h"
#include "Movement/GameMovementState.h"
#include "Hud/GameUi.h"
#include "Hud/GameWindowDrawing.h"
#include "Guild/GuildState.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "Social/SocialState.h"
#include "Social/SocialRequests.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatCommands.h"
#include "Chat/ChatInput.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Hud
{
namespace
{
using Render::BlendMode;
using Render::Color;
using Render::Text::FontKind;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
// The chosen one's button (0x40a726) and the name with the click (0x40a967).
constexpr Color Gray{0.5f, 0.5f, 0.5f, 1.0f};
// 0x40ab9a: the name next to the mouse without the command ready for it.
constexpr Color NotReady{1.0f, 0.2f, 0.2f, 1.0f};
// 0x40aad6..0x40ab19: Interface\CursorID_Wnd.jpg (0x65b), 128 × 32 at the mouse + 5 (0x6b87f8).
constexpr std::uint16_t NameCardSlot = 0x65b;
constexpr double NameCardOffset = 5.0;
constexpr float NameCardWidth = 128.0f;
constexpr float NameCardHeight = 32.0f;
// 0x40ab2e..0x40ab6b and 0x40abab..0x40abd3: the colors and the places of the two names.
constexpr std::uint32_t ChosenNameColor = 0xff67bfdfu;
constexpr std::uint32_t NameCardBack = 0x00141414u;
constexpr std::int32_t ChosenNameTop = 0x2d;
constexpr std::int32_t NameNextToMouseX = 0x45;
constexpr std::int32_t NameNextToMouseY = 0xc;
// The chosen or hovered name, without the click (0x40a978).
constexpr Color Highlight{1.0f, 0.8f, 0.2f, 1.0f};
// 0x491960(0, 1): the checkbox frame (0x4630a5).
constexpr Color CheckboxFrame{static_cast<float>(0x92) / 255.0f, static_cast<float>(0x86) / 255.0f,
                              static_cast<float>(0x79) / 255.0f, 1.0f};

// 0x40a668..0x40a676: the corner the drawing sets.
constexpr std::int32_t WindowLeft = 0x1c2;
constexpr std::int32_t WindowTop = 0;
// 0x40a463..0x40a48f: the mouse over the window, 0xbe × 0x1b1 from the corner.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x40a51f..0x40a548: the close button, 0x18 × 0x18 at (0x19, 0x18b) from the corner.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x40a573..0x40a57d.
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x40a553..0x40a56b
constexpr Audio::SoundId FirstCloseSound = Sound::ButtonClickSound;
constexpr Audio::SoundId SecondCloseSound = Sound::CreateWindowSound;

// The buttons: x + 95 (0x6b87e4) − 50 (0x6b86b0) and y + 53 (0x6b87e0), every 28 (0x6b880c). The step hits them
// between the edges, with 108 (0x6c) × 20 (0x14); the drawing paints them 100 × 25 and checks their mouse with 108
// (0x6b87fc) × 20 (0x6b86d0).
constexpr double ButtonsRight = 95.0;
constexpr double ButtonsBack = 50.0;
constexpr double ButtonsTop = 53.0;
constexpr std::int32_t ButtonStep = 0x1c;
constexpr float ButtonStepF = 28.0f;
constexpr std::int32_t ButtonHitWidth = 0x6c;
constexpr std::int32_t ButtonHitHeight = 0x14;
constexpr float ButtonHoverWidth = 108.0f;
constexpr float ButtonHoverHeight = 20.0f;
// 0x40a74f..0x40a774: Interface\Message_box.jpg (0xf0), 100 × 25 with du = 0.83203125.
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr float ButtonWidth = 100.0f;
constexpr float ButtonHeight = 25.0f;
constexpr float ButtonDu = 0.83203125f;
// 0x40aa0f and 0x40aa06: the name, centered at _ftol(x) + 0x32 and at _ftol(y) + 8; 0x40aa3a: the checkbox at
// _ftol(x) − 0x14.
constexpr std::int32_t NameCenter = 0x32;
constexpr std::int32_t NameTop = 8;
constexpr std::int32_t CheckboxBack = 0x14;
// 0x463090: the 9 × 9 checkbox and the mark, Interface\win_check.tga (0x64a), 5 × 5 at (x + 2, y + 2).
constexpr float CheckboxSize = 9.0f;
constexpr std::int32_t CheckboxFar = 8;
constexpr std::uint16_t CheckSlot = 0x64a;
constexpr std::int32_t CheckInset = 2;
constexpr float CheckSize = 5.0f;
constexpr float CheckUv = 0.625f;

// Text.bmd texts: (the address − 0x76bd1dc) / 300.
constexpr std::size_t TitleText = 938;
constexpr std::size_t CloseHelpText = 927;
// 0x40a998: the table 0x40aca4, the names of commands 1 to 11.
constexpr std::array<std::size_t, Ui::CommandCount - 1> CommandTexts{943,  1124, 944, 945, 946, 1352,
                                                                     1321, 1322, 947, 948, 949};
// 0x40a853: the "D" (0x6c6358) that sprintf puts in text 927, in a 100-byte stack buffer (EBP − 0x6c).
constexpr std::string_view CloseKey = "D";
constexpr std::size_t HelpBufferSize = 100;
// 0x40a6c9..0x40a6f2: the title at (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
// 0x40ac60..0x40ac7b: the notice centered at (x + 0x5f, y + 0x44).
constexpr std::int32_t NoticeCenter = 0x5f;
constexpr std::int32_t NoticeTop = 0x44;

// 0x6c8e5c and 0x6c8e64 of the title (0x40a6b5) and of the names (0x40a89f).
constexpr Render::Text::TextColors TitleColors{.text = 0xffffffffu, .background = 0xff141414u};
constexpr Render::Text::TextColors NameColors{.text = 0xffffffffu, .background = 0x00141414u};

// 0x40abdb..0x40ac2e's notice: with the chosen one's state at 0, the text of command 5, 6, 10 or 3; with 0xff or
// 0xfe, those of these states.
constexpr std::uint8_t RefusedState = 0;
constexpr std::uint8_t NotMasterState = 0xff;
constexpr std::uint8_t NotMemberState = 0xfe;

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

using Math::F32;
using Math::Truncate;

// 0x40a4ad and 0x40a705: fild of the corner, fadd of 95 and fsub of 50.
double ButtonsLeftOf(std::int32_t x)
{
    return static_cast<double>(x) + ButtonsRight - ButtonsBack;
}

// 0x4061b7 and 0x4061c3: the states 0x406050 sets per command before the click.
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
// 0x4062bb: +0x368 at 0x80.
constexpr std::uint8_t GuildMaster = 0x80;
// +0x368 without a guild.
constexpr std::uint8_t NoGuildStatus = 0xff;
// The guild commands' Text.bmd notices: 0x76cfcb0 (255), 0x76e00d8 (477), 0x7720944 (1358), 0x77228e8 (1385),
// 0x76e2400 (507), 0x7720a70 (1359) and 0x7720b9c (1360).
constexpr std::size_t InAGuildText = 255;
constexpr std::size_t GuildAskedText = 477;
constexpr std::size_t AllianceAskedText = 1358;
constexpr std::size_t NoGuildText = 1385;
constexpr std::size_t NotAMasterText = 507;
constexpr std::size_t HostilityAskedText = 1359;
constexpr std::size_t HostilityEndedText = 1360;
// 0x570d8f: /union, /rival or /rivaloff with the hero without a guild (0x77205c0).
constexpr std::size_t HeroWithoutAGuildText = 1355;
// The E5's relation and request.
constexpr std::uint8_t UnionRelation = 1;
constexpr std::uint8_t RivalRelation = 2;
constexpr std::uint8_t MakeRelation = 1;
constexpr std::uint8_t BreakRelation = 2;
// 0x409669: the hero's animation (0x4ea030) and 0x409817: the action of command 8's C1 18.
constexpr std::uint8_t EndOfHostilityAnimation = 0xb7;
constexpr std::uint8_t EndOfHostilityAction = 0x92;
// 0x406195 and 0x40619a: at most two cells on each axis.
constexpr std::int32_t CommandReach = 2;
// The texts 0x76e1398 and 0x76cff08 (478 and 915, Chat::ChatCommands).
constexpr std::size_t PersonalShopText = 493;
constexpr std::size_t NotTheLeaderText = 257;

void ResetTheStates(Ui::CommandsWindowState& window)
{
    window.states.fill(Ui::CommandReady);
}

// 0x406af0..0x406afa and 0x4072ae..0x4072bb: the window closes (+4), without target (+8) or command (+0x20); then
// 0x4072be sets the twelve states to 1.
void CloseAfterTheCommand(Ui::DelegatedPanels& delegated)
{
    delegated.commands = 0;
    delegated.commandsWindow.target = -1;
    delegated.commandsWindow.chosen = -1;
    ResetTheStates(delegated.commandsWindow);
}

// 0x4061b7..0x406344: what the chosen command can do with the chosen character, in its state (+0x14 + command): 1 it
// can, 0 not, 0xff the other is not a guild master, 0xfe nor is the hero. Command 9 always stays at 0.
Core::Expected<void, std::string> SetTheStates(const Kernel::WorldState& world, Ui::CommandsWindowState& window,
                                               std::size_t index)
{
    const Kernel::ActorState& target = world.Actors()[index];
    const Kernel::ActorState& hero = world.Hero();
    // 0x4061b7: 9 (text 947, friends) at 0; its case (0x40a0e1) is not reached.
    window.states[FriendCommand] = 0;
    auto& states = window.states;
    switch (static_cast<std::size_t>(window.chosen))
    {
    case PartyCommand:
    case FollowCommand:
        // 0x4061d3..0x406275: invite them if they are not in the party; follow them if they are.
        states[PartyCommand] = Ui::CommandReady;
        states[FollowCommand] = 0;
        if (Social::IsInTheParty(world, index))
        {
            states[PartyCommand] = 0;
            states[FollowCommand] = Ui::CommandReady;
        }
        return {};
    case GuildCommand:
        // 0x40627a..0x4062b1: the hero without a guild (+0x1f2 negative) and the other the master of theirs.
        states[GuildCommand] = Ui::CommandReady;
        if (static_cast<std::int16_t>(hero.word1f2) >= 0)
            states[GuildCommand] = 0;
        else if (static_cast<std::int16_t>(target.word1f2) < 0 || target.guildStatus != GuildMaster)
            states[GuildCommand] = NotMasterState;
        return {};
    case AllianceCommand:
        // 0x4062b6..0x4062f9: both masters and, in the guilds table, the +4 of the hero's.
        if (hero.guildStatus != GuildMaster)
            states[AllianceCommand] = NotMemberState;
        else if (target.guildStatus != GuildMaster)
            states[AllianceCommand] = NotMasterState;
        else
        {
            // 0x4062d7..0x4062f5: the +4 of the hero's guild entry (0x787aa4c, without checking −1: Q-C1281): with an
            // alliance already, 0; otherwise, 1.
            const auto guildIndex = static_cast<std::int16_t>(hero.word1f2);
            const std::optional<char> unionByte = Guild::UnionByteAt(world.Runtime().guild, guildIndex);
            if (!unionByte)
                return Core::Unexpected(
                    std::format("0x4062e4: el gremio {} del héroe cae fuera de la tabla", guildIndex));
            states[AllianceCommand] = *unionByte != '\0' ? 0 : Ui::CommandReady;
        }
        return {};
    case HostilityCommand:
    case CancelHostilityCommand:
    {
        // 0x4062fb..0x406344: both masters.
        const std::size_t command = static_cast<std::size_t>(window.chosen);
        if (hero.guildStatus != GuildMaster)
            states[command] = NotMemberState;
        else if (target.guildStatus != GuildMaster)
            states[command] = NotMasterState;
        else
            states[command] = Ui::CommandReady;
        return {};
    }
    default:
        // 0x4061c6: 1, 2, 4, 9 and 11 touch nothing.
        return {};
    }
}

// 0x555190("", text, type) or the text already built, in the messages, with the chat input.
Core::Expected<void, std::string> Notify(const Chat::GameChatContext& chat, const Chat::ChatCommandNotice& notice,
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
        const auto found = Ui::TextAt(chat.texts, notice.text);
        if (!found)
            return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, notice.text));
        text = std::string(*found);
    }
    return Chat::AddGameMessage(chat.world.Runtime(), *chat.measure, chat.screen, "", text, notice.type);
}

Core::Expected<void, std::string> NotifyAll(const Chat::GameChatContext& chat, const Chat::ChatCommandOutcome& outcome,
                                            std::string_view where)
{
    for (const Chat::ChatCommandNotice& notice : outcome.notices)
    {
        if (auto added = Notify(chat, notice, where); !added.HasValue())
            return added;
    }
    return {};
}

Core::Expected<void, std::string> SendTheGuildRequest(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                      Protocol::Outgoing packet, std::uint16_t key, std::size_t text,
                                                      std::string_view where)
{
    if (!chat.send)
        return Core::Unexpected(std::format("{}: el paquete del gremio sin el cable", where));
    if (chat.texts == nullptr)
        return Core::Unexpected(std::format("{}: el aviso {} sin Text.bmd", where, text));
    chat.send(std::move(packet));
    const Social::SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    auto notice = Social::NoticeWithTheName(requests, text, key, where);
    if (!notice.HasValue())
        return Core::Unexpected(std::move(notice).Error());
    return Notify(chat, notice.Value(), where);
}

// A Text.bmd notice and the command at −1 (+0x20): the level (0x40646a), the shop (0x4064aa), the party (0x407326)
// and the duel (0x4085a5).
Core::Expected<void, std::string> Refuse(const Chat::GameChatContext& chat, Ui::CommandsWindowState& window,
                                         std::size_t text, std::int32_t type, std::string_view where)
{
    if (auto added = Notify(chat, Chat::ChatCommandNotice{.text = text, .type = type}, where); !added.HasValue())
        return added;
    window.chosen = -1;
    return {};
}

// 0x40638d..0x406afd, command 1 (text 943): the own record's level (+0xe, with its access) against 0x6c8dc8; the
// other with a personal shop (0x451810); the 36 with its notice; the window closes.
Core::Expected<void, std::string> CommandATrade(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                std::size_t index)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    std::uint16_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    if (level < Chat::DefaultTradeLevel)
        return Refuse(chat, delegated.commandsWindow, Chat::TradeLevelText, Ui::SystemMessage, "0x406476");
    if (PersonalShop::HasShopTitle(runtime.social.personalShop, index))
        return Refuse(chat, delegated.commandsWindow, PersonalShopText, Ui::ErrorMessage, "0x4064b6");
    const Social::SocialRequestContext requests{
        .world = world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    auto sent = Social::SendATradeRequest(requests, index, "0x4064f0");
    if (!sent.HasValue())
        return Core::Unexpected(sent.Error());
    if (auto added = NotifyAll(chat, sent.Value(), "0x406ae5"); !added.HasValue())
        return added;
    CloseAfterTheCommand(delegated);
    return {};
}

// 0x406b02..0x4072be, command 2 (text 1124): the C1 10 3F 05 with the other's key and name, in C3, and the window
// closes. What the server answers (the incoming 3F 05) has no handler (08 P-C208 b).
Core::Expected<void, std::string> CommandAShopView(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                   std::size_t index)
{
    const Kernel::ActorState& target = context.world.Actors()[index];
    const Social::SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    if (auto sent = Social::SendInC3(requests, Protocol::PersonalShop::WriteShopItemsRequest(target.key, target.name),
                                     "0x406b02");
        !sent.HasValue())
        return sent;
    CloseAfterTheCommand(context.world.Runtime().panels.delegated);
    return {};
}

// 0x4072df..0x4079a5, command 3 (text 944): with another leader, notice 257; otherwise, the 40 with its notice. The
// window stays.
Core::Expected<void, std::string> CommandAParty(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                std::size_t index)
{
    Ui::CommandsWindowState& window = context.world.Runtime().panels.delegated.commandsWindow;
    if (Social::AnotherLeadsTheParty(context.world))
        return Refuse(chat, window, NotTheLeaderText, Ui::SystemMessage, "0x407332");
    const Social::SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    auto sent = Social::SendAPartyRequest(requests, index, "0x40736b");
    if (!sent.HasValue())
        return Core::Unexpected(sent.Error());
    return NotifyAll(chat, sent.Value(), "0x40798d");
}

// 0x40a04e..0x40a0e0, command 4 (text 945): with 0x6c7b8c at 1 (.data), the name to the whisper EDIT
// (0x4694c0(0, 0, 1, name)), the chosen field at 0 (0x77f3b70), the focus to the message's (0x469370(0)) and
// SendMessage(WM_CHAR, Enter) to the game window, which runs the WndProc's Enter right away. The window stays.
Core::Expected<void, std::string> CommandAWhisper(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                  std::size_t index)
{
    Chat::ChatInputState& input = context.world.Runtime().chat.input;
    Chat::SetTheChatFields(input, std::nullopt, Actors::ChatNameOf(context.world.Actors()[index]));
    input.field = Chat::MessageField;
    Chat::FocusTheChatField(input, Chat::MessageField, &context.world.Runtime().ui.focusedList);
    return Chat::PressEnterInTheGame(chat);
}

// 0x4079a6..0x4085d0, command 11 (text 949): without a duel, the AA with the other's key and name; with the duel (1)
// and the other as the rival (strcmp with 0x78abae0), the AB; otherwise, notice 915 with 2. The window stays.
Core::Expected<void, std::string> CommandADuel(const GameUiContext& context, const Chat::GameChatContext& chat,
                                               std::size_t index)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ActorState& target = context.world.Actors()[index];
    const Social::SocialRequestContext requests{
        .world = context.world, .random = context.random, .texts = *chat.texts, .send = chat.send};
    const std::uint8_t active = runtime.social.duel.active;
    if (active == 0)
        return Social::SendInC3(requests, Protocol::Social::WriteDuelStart(target.key, target.name), "0x4079b3");
    if (active == 1 && Social::IsTheDuelRival(context.world, index))
        return Social::SendInC3(requests, Protocol::Social::WriteDuelEnd(), "0x408141");
    return Refuse(chat, runtime.panels.delegated.commandsWindow, Chat::DuelBusyText, Ui::ErrorMessage, "0x4085b1");
}

// 0x4085d1..0x408a23, command 5: with the hero already in a guild (+0x368 other than 0xff), notice 255 and the command
// at −1; without the world loaded (0x7d3a101), nothing; otherwise, 0x78aba40 = the other's key (MOVSX), the C1 05 50
// in clear and notice 477 with their name. The window stays.
Core::Expected<void, std::string> CommandAGuildJoin(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                    std::size_t index)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (context.world.Hero().guildStatus != NoGuildStatus)
        return Refuse(chat, runtime.panels.delegated.commandsWindow, InAGuildText, Ui::SystemMessage, "0x4085ec");
    if (!runtime.drawingEnabled)
        return {};
    const std::uint16_t key = context.world.Actors()[index].key;
    runtime.guild.requester = static_cast<std::int16_t>(key);
    return SendTheGuildRequest(context, chat, Protocol::Guild::WriteJoinRequest(key), key, GuildAskedText, "0x408626");
}

// 0x408a24..0x409065, command 6: with the other a master (+0x368 at 0x80), the C1 07 E5 01 01 with their key and
// notice 1358 with their name; otherwise, 1385 (0xff, no guild) or 507, and the command at −1.
Core::Expected<void, std::string> CommandAnAlliance(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                    std::size_t index)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ActorState& target = context.world.Actors()[index];
    if (target.guildStatus != GuildMaster)
        return Refuse(chat, runtime.panels.delegated.commandsWindow,
                      target.guildStatus == NoGuildStatus ? NoGuildText : NotAMasterText, Ui::SystemMessage,
                      "0x409046");
    return SendTheGuildRequest(context, chat,
                               Protocol::Guild::WriteRelationshipRequest(UnionRelation, MakeRelation, target.key),
                               target.key, AllianceAskedText, "0x408a32");
}

// 0x409066..0x409662, command 7: the C1 07 E5 02 01 with the other's key and notice 1359 with their name.
Core::Expected<void, std::string> CommandAHostility(const GameUiContext& context, const Chat::GameChatContext& chat,
                                                    std::size_t index)
{
    const std::uint16_t key = context.world.Actors()[index].key;
    return SendTheGuildRequest(context, chat,
                               Protocol::Guild::WriteRelationshipRequest(RivalRelation, MakeRelation, key), key,
                               HostilityAskedText, "0x409066");
}

// The hero's 0xb7 animation (0x4ea030(hero, 0xb7), with its type's model) and the C1 05 18 with the hero's direction
// and action 0x92: command 8 (0x409663) and /rivaloff (0x57106e and 0x5729e1).
Core::Expected<void, std::string> PlayTheEndOfAHostility(const Chat::GameChatContext& chat, std::string_view animation,
                                                         std::string_view action)
{
    Kernel::ActorState& hero = chat.world.Hero();
    if (!chat.send)
        return Core::Unexpected(std::format("{}: el C1 18 sin el cable", action));
    if (chat.models == nullptr)
        return Core::Unexpected(std::format("{}: la animación del héroe sin los modelos (0x4ea030)", animation));
    const auto* skeleton = chat.models->SkeletonOf(Models::ModelId{hero.type});
    if (skeleton == nullptr)
        return Core::Unexpected(
            std::format("{}: sin las acciones del modelo {:#x} del héroe (0x4ea030)", animation, hero.type));
    Models::SetAction(hero.animation, *skeleton, EndOfHostilityAnimation);
    const Actors::ActionSender sendAction = [&chat](std::uint8_t direction, std::uint8_t sent)
    {
        if (chat.send)
            chat.send(Protocol::Movement::WriteAction(direction, sent));
    };
    return Actors::SendTheAction(hero.angles.z, EndOfHostilityAction, sendAction, action);
}

// 0x409663..0x40a04d, command 8: the hero's 0xb7 animation and the C1 05 18 (PlayTheEndOfAHostility), the C1 07 E5 02
// 02 with the other's key and notice 1360 with their name.
Core::Expected<void, std::string> CommandTheEndOfAHostility(const GameUiContext& context,
                                                            const Chat::GameChatContext& chat, std::size_t index)
{
    if (auto played = PlayTheEndOfAHostility(chat, "0x40966f", "0x4096f0"); !played.HasValue())
        return played;
    const std::uint16_t key = context.world.Actors()[index].key;
    return SendTheGuildRequest(context, chat,
                               Protocol::Guild::WriteRelationshipRequest(RivalRelation, BreakRelation, key), key,
                               HostilityEndedText, "0x409a4c");
}

// 0x406050 (from 0x40a499, with ECX = [0x6e592c]), every step with the window open: the twelve states at 1 and, with
// a chosen command and a chosen character (0x6c8e30), the distance to the hero (its four accesses), the target (+8) if
// it is a player who can be asked something within two cells, the command's states and, with the right click
// (0x7d36880) and a target, the command if its state is 1.
Core::Expected<void, std::string> RunTheCommand(const GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::CommandsWindowState& window = runtime.panels.delegated.commandsWindow;
    ResetTheStates(window);
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (window.chosen == -1 || chosen < 0)
        return {};
    if (static_cast<std::size_t>(chosen) >= context.world.Actors().size())
        return Core::Unexpected(std::format("0x4060a4: el elegido {} lee fuera de los personajes", chosen));
    const auto index = static_cast<std::size_t>(chosen);
    window.target = -1;
    const Social::CellDistance distance = Social::CellDistanceToTheHero(context.world, context.random, index);
    if (Social::CanBeAsked(context.world, index) && distance.x <= CommandReach && distance.y <= CommandReach)
        window.target = chosen;
    if (auto states = SetTheStates(context.world, window, index); !states.HasValue())
        return states;
    // 0x406348..0x406386: the right click with a target is spent (with 0x7d3685c); the command's state, signed.
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
        // 0x40a3e0: 0x6c8e50 = the target. The window stays.
        runtime.movement.followTarget = window.target;
        return {};
    }
    if (context.chat == nullptr || context.chat->texts == nullptr)
        return Core::Unexpected(std::format("0x406386: el comando {} sin la entrada del chat ni Text.bmd", command));
    const Chat::GameChatContext& chat = *context.chat;
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
    case GuildCommand:
        return CommandAGuildJoin(context, chat, target);
    case AllianceCommand:
        return CommandAnAlliance(context, chat, target);
    case HostilityCommand:
        return CommandAHostility(context, chat, target);
    case CancelHostilityCommand:
        return CommandTheEndOfAHostility(context, chat, target);
    default:
        // 0x406380: 0 is not chosen; 9 has its state at 0.
        return {};
    }
}

// 0x463090(x, y, checked): the checkbox frame with four untextured rectangles, without blend, and the mark with the
// normal blend. It leaves the color white, the texture on and the normal blend.
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
    // 0x459ec0
    sprites.SetColor(White);
    sprites.EnableTexture();
    sprites.SetBlend(BlendMode::Alpha);
    if (checked)
        WindowDrawing::Quad(sprites, CheckSlot, static_cast<float>(x + CheckInset), static_cast<float>(y + CheckInset),
                            CheckSize, CheckSize, CheckUv, CheckUv);
}

// 0x40a840..0x40a875: sprintf(buffer, text 927, "D"). MuRet does not use the text as the format: without '%' and
// under 100 bytes, sprintf copies it as is; with a '%' or longer, it stops (Q-C650).
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

// 0x40abdb..0x40ac2e: the chosen command's notice text, or 0.
std::size_t NoticeOf(const Ui::CommandsWindowState& window)
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
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    if (delegated.commands == 0)
        return {};
    Kernel::MouseState& mouse = runtime.mouse;
    const std::int32_t x = runtime.hud.commandsX;
    const std::int32_t y = runtime.hud.commandsY;
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    if (mouseX >= x && mouseX < x + WindowWidth && mouseY >= y && mouseY < y + WindowHeight)
        runtime.ui.mouseOnUi = 1;
    if (auto commanded = RunTheCommand(context); !commanded.HasValue())
        return commanded;
    // 0x40a49e..0x40a51d: 0 is not chosen; the click is spent on the first that takes it.
    const std::int32_t left = Truncate(ButtonsLeftOf(x));
    std::int32_t top = Truncate(static_cast<double>(y) + ButtonsTop);
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(Ui::CommandCount); ++command)
    {
        if (command != 0 && mouseX > left && mouseX < left + ButtonHitWidth && mouseY > top &&
            mouseY < top + ButtonHitHeight && mouse.pendingClick)
        {
            mouse.pendingClick = false;
            mouse.leftHeld = false;
            delegated.commandsWindow.chosen = command;
        }
        top += ButtonStep;
    }
    if (mouseX < x + CloseLeft || mouseX >= x + CloseLeft + CloseSize || mouseY < y + CloseTop ||
        mouseY >= y + CloseTop + CloseSize || !mouse.pendingClick)
        return {};
    mouse.pendingClick = false;
    runtime.RequestSound(FirstCloseSound);
    runtime.RequestSound(SecondCloseSound);
    mouse.updateTime = 0;
    mouse.updateTimeMax = CloseMouseUpdateTimeMax;
    Ui::ResetTheCommands(delegated);
    return {};
}

Core::Expected<void, std::string> DrawTheCommandsWindow(GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    if (delegated.commands == 0)
        return {};
    const Ui::CommandsWindowState& window = delegated.commandsWindow;
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
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

    // 0x40a6f9..0x40a78c: the eleven buttons, the y added in float also for 0.
    const auto buttonsLeft = static_cast<float>(ButtonsLeftOf(x));
    auto top = static_cast<float>(static_cast<double>(y) + ButtonsTop);
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(Ui::CommandCount); ++command)
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

    // 0x40a89f..0x40aa56: the names, with the font and color of the chosen or hovered one, and the checkbox.
    hud.textColors = NameColors;
    top = static_cast<float>(static_cast<double>(y) + ButtonsTop);
    const Ui::Point mouse = context.mouse;
    const bool click = runtime.mouse.pendingClick;
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(Ui::CommandCount); ++command)
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
    // 0x40aa80..0x40abd8 (CL9 milestone 7, phase 2): with a chosen character (0x6c8e30) that is a player (+0x88 at 1),
    // not the published hero and of type 0x20a or with +0x3e3, the card (Interface\CursorID_Wnd.jpg, 0x65b) of 128 × 32
    // at the mouse + 5, the normal blend and their name (+0x1d9) in the title box at (x + 0x23, y + 0x2d), in
    // 0xff67bfdf over 0x141414; and again centered at (mouse x + 0x45, mouse y + 0xc), in white over 0x141414, with
    // white glColor if the chosen command is for them (+8) and ready, or (1; 0.2; 0.2).
    if (const std::int32_t chosen = runtime.selectedTargets[2]; chosen >= 0)
    {
        if (static_cast<std::size_t>(chosen) >= context.world.Actors().size())
            return Core::Unexpected(std::format("0x40aa8d: el elegido {} lee fuera de los personajes", chosen));
        if (Social::CanBeAsked(context.world, static_cast<std::size_t>(chosen)))
        {
            WindowDrawing::Quad(sprites, NameCardSlot, F32(static_cast<double>(mouse.x) + NameCardOffset),
                                F32(static_cast<double>(mouse.y) + NameCardOffset), NameCardWidth, NameCardHeight, 1.0f,
                                1.0f);
            sprites.SetBlend(BlendMode::Alpha);
            const std::string name = Actors::ChatNameOf(context.world.Actors()[static_cast<std::size_t>(chosen)]);
            hud.textColors = Render::Text::TextColors{.text = ChosenNameColor, .background = NameCardBack};
            WindowDrawing::DrawTitle(context, x + TitleLeft, y + ChosenNameTop, name);
            bool ready = false;
            if (window.target == chosen && window.chosen != -1)
            {
                if (window.chosen < 0 || window.chosen >= static_cast<std::int32_t>(Ui::CommandCount))
                    return Core::Unexpected(
                        std::format("0x40ab87: el comando {} lee fuera de los doce estados", window.chosen));
                ready = window.states[static_cast<std::size_t>(window.chosen)] == Ui::CommandReady;
            }
            sprites.SetColor(ready ? White : NotReady);
            hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = NameCardBack};
            WindowDrawing::DrawCenteredText(context, mouse.x + NameNextToMouseX, mouse.y + NameNextToMouseY, name);
        }
    }
    std::size_t notice = 0;
    if (window.chosen != -1)
    {
        if (window.chosen < 0 || window.chosen >= static_cast<std::int32_t>(Ui::CommandCount))
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
// 0x573f85..0x574bd5: with a guild (the hero's +0x368 other than 0xff), notice 255 with 1; otherwise, the chosen one
// or the one in front (TheOneToAsk, 0x573fbb and 0x574587); without them or without the world loaded (0x7d3a101),
// nothing. With them, 0x78aba40 = their key (MOVSX), the C1 05 50 in clear and notice 477 with their name, with 1. It
// returns 1.
Core::Expected<Chat::ChatCommandOutcome, std::string> RequestAGuildJoin(const Chat::GameChatContext& chat)
{
    Kernel::WorldState& world = chat.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    if (world.Hero().guildStatus != NoGuildStatus)
        return Chat::ChatCommandOutcome{.handled = true, .notices = {{InAGuildText, Ui::SystemMessage}}};
    if (chat.texts == nullptr)
        return Core::Unexpected(std::string("0x573fbb: /guild sin Text.bmd"));
    const Social::SocialRequestContext requests{
        .world = world, .random = chat.random, .texts = *chat.texts, .send = chat.send};
    auto found = Social::TheOneToAsk(requests, "0x573fbb");
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    if (!found.Value() || !runtime.drawingEnabled)
        return Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    const std::uint16_t key = world.Actors()[*found.Value()].key;
    runtime.guild.requester = static_cast<std::int16_t>(key);
    if (!chat.send)
        return Core::Unexpected(std::string("0x574179: el C1 05 50 de /guild sin el cable"));
    chat.send(Protocol::Guild::WriteJoinRequest(key));
    auto notice = Social::NoticeWithTheName(requests, GuildAskedText, key, "0x574511");
    if (!notice.HasValue())
        return Core::Unexpected(std::move(notice).Error());
    return Chat::ChatCommandOutcome{.handled = true, .notices = {std::move(notice).Value()}};
}

// 0x570d7e..0x573f48: without a guild (the hero's +0x368 at 0xff), notice 1355 with 1; otherwise, the chosen one or
// the one in front (TheOneToAsk, 0x570db4 and 0x572679), without looking at their guild or the world loaded; without
// them, nothing. With them, the alliance (C1 07 E5 01 01 and notice 1358), the rivalry (E5 02 01 and 1359) or its end
// (the 0xb7 animation and command 8's C1 05 18, E5 02 02 and 1360), with their key and the name of the first live one
// with it, with 1. It returns 1.
Core::Expected<Chat::ChatCommandOutcome, std::string> RequestAGuildRelation(const Chat::GameChatContext& chat,
                                                                            Chat::GuildRelationCommand command)
{
    Kernel::WorldState& world = chat.world;
    if (world.Hero().guildStatus == NoGuildStatus)
        return Chat::ChatCommandOutcome{.handled = true, .notices = {{HeroWithoutAGuildText, Ui::SystemMessage}}};
    if (chat.texts == nullptr)
        return Core::Unexpected(std::string("0x570db4: /union, /rival o /rivaloff sin Text.bmd"));
    const Social::SocialRequestContext requests{
        .world = world, .random = chat.random, .texts = *chat.texts, .send = chat.send};
    auto found = Social::TheOneToAsk(requests, "0x570db4");
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    if (!found.Value())
        return Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    const std::uint16_t key = world.Actors()[*found.Value()].key;
    std::uint8_t relation = RivalRelation;
    std::uint8_t change = MakeRelation;
    std::size_t text = HostilityAskedText;
    std::string_view where = "0x571a4e";
    if (command == Chat::GuildRelationCommand::Union)
    {
        relation = UnionRelation;
        text = AllianceAskedText;
        where = "0x572063";
    }
    else if (command == Chat::GuildRelationCommand::RivalOff)
    {
        if (auto played = PlayTheEndOfAHostility(chat, "0x571079", "0x5710fa"); !played.HasValue())
            return Core::Unexpected(std::move(played).Error());
        change = BreakRelation;
        text = HostilityEndedText;
        where = "0x571432";
    }
    if (!chat.send)
        return Core::Unexpected(std::format("{}: el E5 sin el cable", where));
    chat.send(Protocol::Guild::WriteRelationshipRequest(relation, change, key));
    auto notice = Social::NoticeWithTheName(requests, text, key, where);
    if (!notice.HasValue())
        return Core::Unexpected(std::move(notice).Error());
    return Chat::ChatCommandOutcome{.handled = true, .notices = {std::move(notice).Value()}};
}
} // namespace Client::Hud
