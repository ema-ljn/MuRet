#include "Hud/GameUi.h"
#include "Actors/ActorName.h"
#include "Maps/MapId.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "CastleSiege/CastleWindows.h"
#include "CastleSiege/CastleSiegeReceive.h"

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Kernel/CharacterClass.h"
#include "Protocol/Player/LevelUpPoint.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Messenger/Letters.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pets/PetInfo.h"
#include "Protocol/Social/Trade.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "ChaosMix/ChaosMix.h"
#include "Chat/ChatReceive.h"
#include "Messenger/LetterPortrait.h"
#include "Actors/CharacterPicking.h"
#include "Events/Crywolf/CameraTour.h"
#include "Events/Crywolf/CrywolfContract.h"
#include "Events/EventChipWindow.h"
#include "Events/EventNpcWindows.h"
#include "Items/FloorItemActions.h"
#include "Chat/GameChat.h"
#include "Hud/GameCommandsWindow.h"
#include "Hud/Dialogs/GameDialogSteps.h"
#include "Messenger/GameFriends.h"
#include "Messenger/GameLetters.h"
#include "Chat/GameMessages.h"
#include "Hud/GameMoveWindow.h"
#include "Movement/GameMovementState.h"
#include "Hud/GamePanels.h"
#include "Guild/GuildPanels.h"
#include "Party/GamePartyWindow.h"
#include "Social/GameSocialSteps.h"
#include "Items/GridItemUse.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Player/ItemSetState.h"
#include "Npc/NpcWindowState.h"
#include "Npc/NpcWindowSteps.h"
#include "Npc/NpcWindows.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Party/PartyActors.h"
#include "Kernel/OwnInventory.h"
#include "PetTrainer/PetTrainer.h"
#include "Warehouse/WarehouseKeypad.h"
#include "PersonalShop/PersonalShopWindow.h"
#include "Quests/QuestWindow.h"
#include "Social/SocialState.h"
#include "Guild/GuildWindow.h"
#include "Guild/SecondGuildWindow.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Ui/SlideBanner.h"
#include "Kernel/ItemState.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"
#include "Chat/ChatRuntime.h"
#include "Events/EventGlobals.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Hud
{
namespace
{
using Input::Key;

// A MuRet key and its Windows virtual key, the one the original passes to GetAsyncKeyState and that indexes the
// latches at 0x77f36d4.
struct GameKey
{
    Key key;
    std::uint8_t virtualKey;
};

constexpr GameKey TabKey{Key::Tab, 0x09};
constexpr GameKey EscapeKey{Key::Escape, 0x1b};
constexpr GameKey PageUpKey{Key::PageUp, 0x21};
constexpr GameKey PageDownKey{Key::PageDown, 0x22};
constexpr GameKey UpKey{Key::Up, 0x26};
constexpr GameKey DownKey{Key::Down, 0x28};
constexpr GameKey SpaceKey{Key::Space, 0x20};
constexpr GameKey F1Key{Key::F1, 0x70};
constexpr GameKey F2Key{Key::F2, 0x71};
constexpr GameKey F3Key{Key::F3, 0x72};
constexpr GameKey F4Key{Key::F4, 0x73};
constexpr GameKey F5Key{Key::F5, 0x74};
constexpr GameKey F6Key{Key::F6, 0x75};
constexpr GameKey EnterKey{Key::Enter, 0x0d};

constexpr GameKey Letter(char letter)
{
    return {static_cast<Key>(static_cast<int>(Key::A) + (letter - 'A')), static_cast<std::uint8_t>(letter)};
}

constexpr GameKey Digit(int digit)
{
    return {static_cast<Key>(static_cast<int>(Key::Digit0) + digit), static_cast<std::uint8_t>('0' + digit)};
}

// 0x41ed10(0x19, 0, 0): the sound of the buttons and of Esc.
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;
// 0x5a10f0: the zen dialog, with the field in the EDIT of [0x5763504].
constexpr std::int32_t MoneyDialog = 0x73;
// 0x5e455a and 0x5e4573: the character window, from 0x78aba14 and 0x78aba18.
constexpr std::int32_t CharacterWindowWidth = 0xbe;
constexpr std::int32_t CharacterWindowHeight = 0x1b1;
// Class 4, the Dark Lord: five point buttons (0x5e4598) and the pet orders with Shift (0x420e30).
// 0x4246ea..0x424709: the pet button, fixed on the screen.
constexpr std::int32_t PetButtonLeft = 0x217;
constexpr std::int32_t PetButtonRight = 0x22f;
constexpr std::int32_t PetButtonTop = 0x18b;
constexpr std::int32_t PetButtonBottom = 0x1a3;
// 0x42474c: 0x723911 at 2, the pet window.
constexpr std::uint8_t PetWindowOpen = 2;

// The guild packets the UI sends (CL9 milestone 8) go through the scene's requests; without them, nothing.
Guild::GuildSender GuildSenderOf(Ui::IGameUiRequests* requests)
{
    if (requests == nullptr)
        return {};
    return [requests](Protocol::Outgoing packet) { requests->SendGuildPacket(std::move(packet)); };
}

// The dialogs step and the windows step have different registries; both need the full closes.
void LendTheUiPanelCloses(GamePanels& panels, const Ui::GameUiContext& context)
{
    CastleSiege::CastleSender send;
    if (context.requests != nullptr)
        send = [requests = context.requests](Protocol::Outgoing packet)
        { requests->SendGamePacket(std::move(packet)); };
    CastleSiege::LendTheCastlePanelServices(
        panels, context.world, context.random, send, context.worldEntry,
        {.measure = context.measure, .texts = context.texts, .screen = context.screen, .now = context.systemTime});
    panels.LendTheNpcWindows(context.world, context.random,
                             context.inventory != nullptr ? *context.inventory : Items::InventoryServices{},
                             context.sendNpcWindow);
    panels.LendTheGuild(context.world, GuildSenderOf(context.requests));
}

// 0x42475f..0x424990: the pet window and its buttons (0x6b8a08, 0x6b8a04, 0x6b87bc, 0x6b8874, 0x6b8a00, 0x6b89f8,
// 0x6b89fc, 0x6b86d0, 0x6b8800, 0x6b8804 and 0x6b89f4).
constexpr std::int32_t PetWindowLeft = 0x1c2;
constexpr std::int32_t PetWindowBesideTheCharacter = 0x104;
constexpr float PetWindowWidth = 190.0f;
constexpr float PetWindowHeight = 433.0f;
constexpr std::uint8_t PetTabCount = 2;
constexpr std::int32_t PetTabStep = 0x2d;
constexpr float PetTabLeft = 15.0f;
constexpr float PetTabWidth = 49.0f;
constexpr float PetOtherTabTop = 67.0f;
constexpr float PetChosenTabTop = 63.0f;
constexpr float PetTabBottom = 83.0f;
constexpr float PetCloseLeft = 20.0f;
constexpr float PetCloseSize = 24.0f;
constexpr float PetCloseTop = 395.0f;
// 0x5aca1b..0x5aca24: F1 cycles through 0, 1 and 2 (idiv by 3).
constexpr std::int32_t FunctionHelpModes = 3;
// CL9 milestone 5, the grid's right click (0x5c58b0..0x5c740c): the town portal scroll and the level 14 box of luck
// (+4 & 0x78 at 0x70). The three that send the C1 .. 91, in GridItemUse.
constexpr std::int32_t TownPortalScroll = 0x1c0a;
constexpr std::int32_t BoxOfLuck = 0x1c0b;
constexpr std::uint32_t UsedLevelBits = 0x78;
constexpr std::uint32_t UsedBoxLevelBits = 0x70;
// 0x636e11: from this y on, the mouse is over the bar.
constexpr std::int32_t HudTop = 0x1b0;
// 0x5a1181..0x5a1189: Chaos Castle.
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
// 0x5a0420 and neighbors: 0x6c8dcc, the MouseUpdateTime maximum the buttons set.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x59ff45..0x5a009b: the old area's six rows, 13 tall from y = 0 (up to 0x4e).
constexpr std::int32_t OldAreaRowHeight = 0xd;
constexpr std::int32_t OldAreaBottom = 0x4e;
// 0x4691e0: the two chat EDITs, at (0xc1, 0x1a6), 0xb4 × 0xe, and at (0x178, 0x1a6), 0x32 × 0xe. Their ids (0x45a210)
// are *inferred*: the first two WinMain builds (0x4bb01b and 0x4bb02c), before the other three EDIT objects and the
// chat log (the 9).
struct ChatEditPlace
{
    std::int32_t field;
    std::int32_t id;
    std::int32_t x, y, width, height;
};
constexpr std::array<ChatEditPlace, 2> ChatEdits{{
    {Chat::MessageField, Chat::ChatMessageEditId, 0xc1, 0x1a6, 0xb4, 0xe},
    {Chat::WhisperField, Chat::ChatWhisperEditId, 0x178, 0x1a6, 0x32, 0xe},
}};
// 0x5a33dc..0x5a343a: the F3 button, next to the whisper EDIT.
constexpr std::int32_t WhisperModeLeft = 0x1b2;
constexpr std::int32_t WhisperModeTop = 0x19f;
constexpr std::int32_t WhisperModeRight = 0x1c6;
constexpr std::int32_t WhisperModeBottom = 0x1bc;

// 0x553520(vk).
bool KeyEdge(Ui::GameUiState& ui, const Input::InputState& input, GameKey key)
{
    return Ui::LatchTheKey(ui.keyLatches[key.virtualKey], input.IsDown(key.key));
}

// 0x6578e0: a text field has the focus (the chat 0x6c8e68, 0x77f3b68, the panels' dialog's) or the game window does
// not have it (GetFocus() other than [0x5763544]). MuRet has the chat's (milestone 3), the guild sign-up's (0x77f3b68,
// CL9 milestone 8) and, with the Windows focus, the old windows' EDIT (CL9 milestone 9) and the dialogs' two: that of
// [0x5763504] (the serial number entry, milestone 11, and the guild sign-up, CL9 milestone 8) and the 0x7d's number one
// (CL9 milestone 8); the guild ones keep the focus when closing (Q-C1291 and Q-C1295). The panels' dialog's is CL9.5
// (P-C285), and its window is the game's.
bool TextInputBusy(const Kernel::WorldRuntime& runtime)
{
    const Chat::ChatFocus focus = runtime.chat.input.focus;
    const auto& panelDialog = runtime.panels.delegated.panelDialog;
    const bool panelField =
        Ui::PanelDialogHandle(panelDialog) != 0 && (panelDialog.flags & Ui::PanelDialogTextField) != 0;
    return runtime.ui.chatOpen || runtime.chat.input.dialogFieldOn || panelField || focus != Chat::ChatFocus::Game;
}

// 0x636e51..0x636e5a: the chat log, 0x45aa80(0) through the vtable + 0x18 of 0x576350c.
Core::Expected<void, std::string> StepTheChat(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    Chat::ChatRuntime& chat = runtime.chat;
    const Kernel::ActorState& hero = context.world.Hero();
    const Chat::ChatLogGlobals globals{.leftClick = runtime.mouse.pendingClick,
                                       .rightDown = ui.rightDown,
                                       .wheel = ui.wheel,
                                       .capture = ui.capture,
                                       .hovered = ui.hovered,
                                       .focusedList = ui.focusedList,
                                       .mouseOnUi = ui.mouseOnUi,
                                       .modes = ui.chatModes};
    // The focus on one of the chat EDITs: while the field is open it has it (0x636e80).
    const Chat::ChatLogStepView view{.mouseX = context.mouseX,
                                     .mouseY = context.mouseY,
                                     .leftDown = runtime.mouse.leftHeld,
                                     .chatOpen = ui.chatOpen,
                                     .heroSafeZone = hero.safeZone != 0,
                                     .heroName = Actors::HeroNameOf(hero),
                                     .chatEditFocused = ui.chatOpen,
                                     .measure = context.measure,
                                     .dcFont = runtime.hud.dcFont};
    const Chat::ChatLogStepResult result = Chat::StepTheChatLog(chat.log, runtime.hud.chatFrame, globals, view);
    for (const Audio::SoundId sound : result.sounds)
        runtime.RequestSound(sound);
    // 0x4694c0(0, 0, 1, name): the name to the whisper EDIT (SetText), without touching its buffer.
    if (result.whisperTo)
        Chat::SetTheChatFields(chat.input, std::nullopt, *result.whisperTo);
    return {};
}

// 0x636e5d..0x636ed1: with the left button down and the mouse outside the windows (or over the chat log), the old
// window manager's focus (+0x44 of 0x5763514) at 0 and, with the chat and the other fields closed (0x636e8c..0x636ea5:
// 0x77f3b69 the panels', 0x77f3b68 the guild sign-up's and 0x77f3b6a the zen mode of the 0x73 and of the 0x91's
// price), the arrows' list at 0, 0x4a5e10 on the window list and the keyboard focus to the game window (SetFocus; the
// IME of 0x467360, MuRet does not have it).
void StepTheUnownedClick(Kernel::WorldRuntime& runtime)
{
    Ui::GameUiState& ui = runtime.ui;
    if (!runtime.mouse.leftHeld || (ui.hovered != 0 && ui.hovered != Chat::ChatLogId))
        return;
    runtime.oldWindows.focus = 0;
    if (ui.chatOpen || runtime.chat.input.otherInput || runtime.chat.input.dialogFieldOn ||
        runtime.social.moneyField != 0)
        return;
    ui.focusedList = 0;
    Ui::HideTheWindowList(runtime.oldWindows.windowList);
    runtime.chat.input.focus = Chat::ChatFocus::Game;
}

// 0x435bd0: the Crywolf interface's step (06 § La interfaz de Crywolf), only on map 34 (0x4303c0). With the results
// (0x723bf9 at 1, from the BD 08) and the left button down (0x7d36874), over the button (300, 354) × (300, 330),
// without the borders, it sets 0x723bdc to 5, which the draw reads to close them with the mouse still over it
// (Q-C1342); anywhere it turns off the button and the edge (0x435c4e): with the results in view, the UI takes all the
// clicks. Then the contract window (0x723a44 at 1 or 2, 0x435c5c: Events/Crywolf/CrywolfContract), with the hero's pet
// (+0x318) and the step's send() for the Yes.
Core::Expected<void, std::string> StepTheCrywolfInterface(const Ui::GameUiContext& context)
{
    using Maps::CrywolfMap;
    constexpr std::int32_t ButtonLeft = 0x12c;
    constexpr std::int32_t ButtonRight = 0x162;
    constexpr std::int32_t ButtonTop = 0x12c;
    constexpr std::int32_t ButtonBottom = 0x14a;
    constexpr std::int32_t ButtonPushed = 5;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.map != CrywolfMap)
        return {};
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    Kernel::CrywolfInterface& crywolf = runtime.crywolf.ui;
    Kernel::MouseState& mouse = runtime.mouse;
    if (crywolf.byte723bf9 == 1 && mouse.leftHeld)
    {
        if (mouseX > ButtonLeft && mouseX < ButtonRight && mouseY > ButtonTop && mouseY < ButtonBottom)
            crywolf.dwords723bdc[0] = ButtonPushed;
        mouse.leftHeld = false;
        mouse.pendingClick = false;
    }
    return Events::Crywolf::StepTheContractWindow(Events::Crywolf::ContractWindowContext{
        .runtime = runtime,
        .texts = context.texts,
        .heroPet = context.world.Hero().pet.model,
        .mouseX = mouseX,
        .mouseY = mouseY,
        .send = context.chat != nullptr ? context.chat->send : Chat::ChatSender{}});
}

// 0x636f04..0x637056: the chat log's corner (0x45a9d0). Outside its window, (−10, 0x65); in it, 0x1a4 tall and x at 0
// under a window on the left, or 0xba. The queries go in the original's order, which stops at the first open one: the
// trade's and the NPC shop's take a rand().
void PlaceTheChatLog(Kernel::WorldRuntime& runtime, Ui::PanelRegistry& panels)
{
    Chat::ChatLogFrame& frame = runtime.hud.chatFrame;
    if (runtime.ui.chatModes.placement != 1)
    {
        frame.x = -10;
        frame.y = 0x65;
        return;
    }
    bool left = false;
    if (panels.IsOpen(Ui::InventoryPanel) &&
        (panels.IsOpen(Ui::CharacterPanel) || panels.IsOpen(Ui::TradePanel) || panels.IsOpen(Ui::WarehousePanel) ||
         panels.IsOpen(Ui::ChaosBoxPanel) || panels.IsOpen(Ui::EventNpcPanel) ||
         panels.IsOpen(Ui::SecondEventNpcPanel) || panels.IsOpen(Ui::NpcShopPanel) ||
         panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel) || panels.IsOpen(Ui::TrainerPanel)))
        left = true;
    // 0x636fff: without the inventory, or with it and none of those, the character with the pet or the quest.
    else if (panels.IsOpen(Ui::CharacterPanel) && (panels.IsOpen(Ui::PetPanel) || panels.IsOpen(Ui::QuestPanel)))
        left = true;
    frame.x = left ? 0 : 0xba;
    frame.y = 0x1a4;
}

bool InRect(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right,
            std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

Core::Expected<void, std::string> Toggle(Ui::PanelRegistry& panels, Ui::PanelId id)
{
    if (panels.IsOpen(id))
    {
        auto closed = panels.Close(id);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    }
    auto opened = panels.Open(id);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

// The five keys the old windows read with 0x553520 (Esc, F5, F6, Enter and the F of the friends one, 0x4a2f70) and
// the two they read down with GetAsyncKeyState (the lists' arrows, 0x47b0f0).
// The siege panel also queries 0x553520(9) at 0x40f6e8; it shares the latch with the rest of the UI.
constexpr std::array<GameKey, 6> OldWindowEdgeKeys{EscapeKey, F5Key, F6Key, EnterKey, Letter('F'), TabKey};
constexpr std::array<GameKey, 2> OldWindowDownKeys{UpKey, DownKey};

// 0x650387 and 0x6503a4: 0x553520 of PgUp and PgDn, which the guild members list reads.
std::function<bool(std::uint8_t)> PageKeyEdges(Ui::GameUiState& ui, const Input::InputState& input)
{
    return [ui = &ui, input = &input](std::uint8_t virtualKey)
    {
        if (virtualKey == PageUpKey.virtualKey)
            return KeyEdge(*ui, *input, PageUpKey);
        if (virtualKey == PageDownKey.virtualKey)
            return KeyEdge(*ui, *input, PageDownKey);
        return false;
    };
}

// GetAsyncKeyState of an old list's arrows (0x47b0f0).
std::function<bool(std::uint8_t)> ArrowKeysDown(const Input::InputState& input)
{
    return [input = &input](std::uint8_t virtualKey)
    {
        for (const GameKey& key : OldWindowDownKeys)
        {
            if (key.virtualKey == virtualKey)
                return input->IsDown(key.key);
        }
        return false;
    };
}

// What the old windows read from the game in the UI step (CL9 milestone 9): 0x490490's view (the mouse, the keys, the
// open chat, Text.bmd, the DC measure, the wire and the whisper EDIT) and 0x491080, which uses it too (panel 1 and
// 0x4906a0). It lives as long as the step.
class OldWindowSight
{
public:
    explicit OldWindowSight(const Ui::GameUiContext& context)
        : m_context(context), m_texts(Messenger::OldWindowTextsOf(context.texts))
    {
        if (context.measure != nullptr)
            m_measure.emplace(Ui::OldWindowMeasure{
                .measure = *context.measure, .dcFont = context.world.Runtime().hud.dcFont, .screen = context.screen});
    }
    OldWindowSight(const OldWindowSight&) = delete;
    OldWindowSight& operator=(const OldWindowSight&) = delete;
    OldWindowSight(OldWindowSight&&) = delete;
    OldWindowSight& operator=(OldWindowSight&&) = delete;
    ~OldWindowSight() = default;

    [[nodiscard]] Ui::OldWindowStepView View() const
    {
        Ui::GameUiState& ui = m_context.world.Runtime().ui;
        const Input::InputState& input = m_context.input;
        Ui::OldWindowStepView view{.mouseX = m_context.mouseX,
                                   .mouseY = m_context.mouseY,
                                   .keyEdge = [&ui, &input](std::uint8_t virtualKey)
                                   { return OldWindowKeyEdge(ui, input, virtualKey); },
                                   .chatOpen = ui.chatOpen,
                                   .texts = &m_texts,
                                   .measure = m_measure ? &*m_measure : nullptr,
                                   .send = {},
                                   .openFriends = [this]() { return OpenTheFriendsWindow(); }};
        if (Ui::IGameUiRequests* requests = m_context.requests; requests != nullptr)
            view.send = [requests, &random = m_context.random](const Ui::FriendSend& send)
            { Messenger::SendAFriendPacket(*requests, random, send); };
        LendTheGameHooks(view, m_context, true);
        return view;
    }

    // 0x491080(0x186, 0x106) with the current globals.
    [[nodiscard]] Core::Expected<void, std::string> OpenTheFriendsWindow() const
    {
        const Ui::OldWindowGlobals globals = OldWindowGlobalsOf(m_context.world.Runtime());
        const Ui::OldWindowStepView view = View();
        return Messenger::OpenTheFriends(Messenger::FriendsOpenContext{.world = m_context.world,
                                                                       .random = m_context.random,
                                                                       .measure = m_context.measure,
                                                                       .screen = m_context.screen,
                                                                       .texts = m_context.texts},
                                         globals, view);
    }

private:
    const Ui::GameUiContext& m_context;
    Ui::OldWindowTexts m_texts;
    std::optional<Ui::OldWindowMeasure> m_measure;
};

// 0x636e3c..0x636e4c: the window list's step ([0x5763524], the vtable + 0x18 with 0) and the old windows' (0x490490).
// Esc reaches 0x490490 already read by 0x623e70, except with the friends one open (Q-C446).
Core::Expected<void, std::string> StepTheOldWindows(const Ui::GameUiContext& context, const OldWindowSight& sight)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::OldWindowStepView view = sight.View();
    const Ui::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    if (auto list = Ui::StepTheWindowList(runtime.oldWindows, globals, view); !list.HasValue())
        return list;
    return Ui::StepTheOldWindows(runtime.oldWindows, globals, view);
}

// ---------------------------------------------------------------------------------------------------------------
// The open inventory's step (0x5ee1ac..0x5ee1ca) and the cursor item's (0x5d8d50), CL8 milestone 4. They read the
// corner their draw left (0x78a5a14 and 0x78a5a10). Picking up, dropping and equipping are in Items/InventoryMoves.

// The siege list in mode 3, which with 0x6689d0 joins the windows with a grid.
constexpr std::int32_t SiegeListWithInventory = 3;
// 0x449c00: the trainer's box, and its fixed grid of 0x44ad10 and 0x44aca0.
constexpr std::int32_t TrainerWithBox = 3;
constexpr std::int32_t TrainerBoxLeft = 0x113;
constexpr std::int32_t TrainerBoxTop = 0x6e;
constexpr std::uint16_t RepairButtonLevel = 0x32;
constexpr std::uint16_t PersonalShopLevel = 6;
// 0x41ed10(0x1d, 0, 0): the sound of picking up from the grid (0x5c587a).
constexpr Audio::SoundId ItemMoveSound = Sound::GetItemSound;
// The rand() of the padding (0x69fd70) and of the serial number (0x40b0a0) of a C3.
constexpr std::size_t C3SendRandoms = 2;
// 0x5c4298..0x5c42a9 and 0x5aed6c..0x5aed7d: the label item's slot is the cell + 12; with a personal shop open,
// + 0x4c (0x5c4353..0x5c4363).
constexpr std::int32_t FirstGridSlot = 12;
constexpr std::int32_t FirstPersonalShopSlot = 0x4c;
// 0x5c5131: 0x78a6758 with no slot.
constexpr std::int32_t NoCursorSlot = -1;
// 0x5c516b..0x5c5177: 0x633300(1, 1100, 0x94), buying in another's shop, which waits for warning 5 with no answer.
constexpr std::int32_t BuyDialogLines = 1;
constexpr std::int32_t BuyDialogText = 1100;
constexpr std::int32_t BuyDialog = 0x94;
constexpr std::int32_t BuyWarningKind = 5;
constexpr std::uint8_t NoWarningAnswer = 0xff;
// 0x5c4b04..0x5c4b22: Ctrl with Q, W or E puts the item under the mouse in that quick slot, with its level.
constexpr std::array<char, 3> CtrlQuickSlotLetters{'Q', 'W', 'E'};
constexpr GameKey CtrlKey{Key::Ctrl, 0x11};
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t CellSize = 20;
// 0x5e58f0..0x5e58fe: the personal shops' 8 × 4 grid (0x78ab0b8), from (x + 15, y + 0x6e).
constexpr std::int32_t ShopGridLeft = 15;
constexpr std::int32_t ShopGridTop = 0x6e;
constexpr std::int32_t ShopGridColumns = 8;
constexpr std::int32_t ShopGridRows = 4;
constexpr std::uint8_t HoveredMark = 2;
// 0x5aed40: 0x78abb28 at 2 with the trade's own grid (0x78a92a8), at 3 with the other's (0x78a5a20) and at 5 with the
// shop's while viewing another's (0x78ab0b8).
constexpr std::int32_t OwnTradeTooltip = 2;
constexpr std::int32_t OtherTradeTooltip = 3;
constexpr std::int32_t OtherShopTooltip = 5;

// 0x5c43f4..0x5c445d: what the repair does not look at (the pets 0x1a00 to 0x1a04, 0x1a0a, the arrows, 0x1c00 onward,
// the wings 0x1807 to 0x1813, 0x1a0e to 0x1a13, 0x1c15 and the Wizard's Ring), signed.
// 0x5c2e4d..0x5c2e5d: the equipment's does not repair the Dark Raven either.
constexpr std::uint16_t RavenItem = 0x1a05;

bool IsNotRepaired(std::uint16_t id)
{
    const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(id));
    const auto in = [type](std::int32_t first, std::int32_t last) { return type >= first && type <= last; };
    return in(0x1a00, 0x1a04) || type == 0x1a0a || type == 0x807 || type == 0x80f || type >= 0x1c00 ||
           in(0x1807, 0x1813) || in(0x1a0e, 0x1a13) || type == 0x1c15 || type == 0x1a14;
}

// The pick up and drop services; without them, the empty ones, and whatever needs them stops.
const Items::InventoryServices& ServicesOf(const Ui::GameUiContext& context)
{
    static const Items::InventoryServices None{};
    return context.inventory != nullptr ? *context.inventory : None;
}

Items::InventoryMoveContext MovesOf(const Ui::GameUiContext& context)
{
    return Items::InventoryMoveContext{
        .world = context.world, .random = context.random, .services = ServicesOf(context)};
}

// The mouse in [left, left + w) × [top, top + h), with the right and bottom edges on the x87:
// FILD mouse; FILD left; FADD w; FCOMPP.
bool MouseInEquipmentRect(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top, float width,
                          float height)
{
    const auto x = static_cast<double>(context.mouseX);
    const auto y = static_cast<double>(context.mouseY);
    return context.mouseX >= left && static_cast<double>(left) + static_cast<double>(width) > x &&
           context.mouseY >= top && static_cast<double>(top) + static_cast<double>(height) > y;
}

// 0x5bec50(x, y, w, h, slot): an equipment slot. With the wait of 0x5767954 or the C3 24 lock it returns without
// opening the own record; otherwise it opens it (3848 rand() when closing it without other accesses) and everything
// that follows runs inside. With an item on the cursor and the mouse on the slot, drop it (0x5bee04); without one,
// with the slot taken and the mouse on it, 0x78abaac at 1 and, with the click, pick it up (0x5c2c65) or, without the
// click, the mark and the help (0x5c35ad).
Core::Expected<void, std::string> StepAnEquipmentSlot(const Ui::GameUiContext& context, float x, float y, float width,
                                                      float height, std::size_t slot)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0)
        return {};
    // _ftol of the constants, which are integers.
    const std::int32_t left = static_cast<std::int32_t>(x) + runtime.hud.inventoryX;
    const std::int32_t top = static_cast<std::int32_t>(y) + runtime.hud.inventoryY;
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const bool inside = MouseInEquipmentRect(context, left, top, width, height);
    if (runtime.cursorItemCount > 0)
    {
        if (!inside)
            return {};
        return Items::DropOnTheEquipment(MovesOf(context), slot);
    }
    if (context.world.OwnCharacter().equipment[slot].id == Kernel::EmptyOwnItemId || !inside)
        return {};
    // 0x5c2c56..0x5c2c5f.
    runtime.ui.field78abaac = 1;
    if (runtime.mouse.pendingClick)
    {
        // 0x5c2c65..0x5c2c79 and 0x5c2dd5..0x5c3594: in repair mode (and without 0x576794c), what gets repaired spends
        // the click and the C1 05 34 goes out with the slot and 0x78abb10, in C3 and with the record open.
        if (runtime.movement.gateRequestBlocked == 0 && runtime.panels.repairMode != 0)
        {
            const std::uint16_t worn = context.world.OwnCharacter().equipment[slot].id;
            if (IsNotRepaired(worn) || worn == RavenItem)
                return {};
            runtime.mouse.pendingClick = false;
            return Npc::SendRepair(context.random, context.sendNpcWindow, static_cast<std::uint8_t>(slot & 0xff),
                                   static_cast<std::uint8_t>(runtime.field78abb10 & 0xff));
        }
        return Items::PickUpFromTheEquipment(MovesOf(context), slot);
    }
    Items::HoverTheEquipment(MovesOf(context), slot, left, top, width);
    return {};
}

// 0x5c3d20: the twelve slots, 8 and 7 first, the helmet with the class read with the record open (its 0x5bec50 stays
// inside and takes nothing when closing) and the other nine.
Core::Expected<void, std::string> StepTheEquipment(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (auto slot = StepAnEquipmentSlot(context, 15.0f, 46.0f, 40.0f, 40.0f, 8); !slot.HasValue())
        return slot;
    if (auto slot = StepAnEquipmentSlot(context, 115.0f, 46.0f, 60.0f, 40.0f, 7); !slot.HasValue())
        return slot;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        constexpr std::uint8_t ClassWithoutHelm = 3;
        if ((context.world.OwnCharacter().characterClass & 7u) != ClassWithoutHelm)
        {
            if (auto slot = StepAnEquipmentSlot(context, 75.0f, 46.0f, 40.0f, 40.0f, 2); !slot.HasValue())
                return slot;
        }
    }
    struct Slot
    {
        float x;
        float y;
        float width;
        float height;
        std::size_t slot;
    };
    constexpr std::array<Slot, 9> Slots{{{75.0f, 89.0f, 40.0f, 60.0f, 3},
                                         {75.0f, 152.0f, 40.0f, 40.0f, 4},
                                         {15.0f, 89.0f, 40.0f, 60.0f, 0},
                                         {134.0f, 89.0f, 40.0f, 60.0f, 1},
                                         {15.0f, 152.0f, 40.0f, 40.0f, 5},
                                         {134.0f, 152.0f, 40.0f, 40.0f, 6},
                                         {55.0f, 89.0f, 20.0f, 20.0f, 9},
                                         {55.0f, 152.0f, 20.0f, 20.0f, 10},
                                         {115.0f, 152.0f, 20.0f, 20.0f, 11}}};
    for (const Slot& place : Slots)
    {
        if (auto slot = StepAnEquipmentSlot(context, place.x, place.y, place.width, place.height, place.slot);
            !slot.HasValue())
            return slot;
    }
    return {};
}

// What the grid item under the mouse did: with picking it up (0x5c5884) or the right click (0x5c5895..0x5c9b0b),
// 0x5c4110 returns.
enum class GridHover : std::uint8_t
{
    Stayed,
    PickedUp,
    RightClicked,
};

// The grid of 0x5c4110(x, y, grid, columns, rows, kind), all of 8 columns: the inventory's (0x78a9bb0, 8 × 8), the
// trade's (0x78a92a8 and 0x78a5a20, 8 × 4), the personal shop's and the NPC windows' (CL9 milestone 6). Its cells, the
// place the item under the mouse records and, in the NPC ones, their rows, the kind (1 the NPC shop, which buys; 0 the
// others) and the grid that what is picked up comes from.
struct HoverGrid
{
    std::span<Kernel::ItemState> cells;
    Ui::HoveredItem::Place place = Ui::HoveredItem::Place::InventoryCell;
    std::int32_t rows = 0;
    std::uint8_t kind = 0;
    Kernel::CursorGrid grid = Kernel::CursorGrid::None;
};

// 0x5c4110 with kind 1 (0x5e57af, the shop): buy.
constexpr std::uint8_t ShopGridKind = 1;
// 0x5aed40: the item's place for its help in the warehouse grid (1) and in the chaos box (4).
constexpr std::int32_t WarehouseHelpPlace = 1;
constexpr std::int32_t ChaosBoxHelpPlace = 4;
// 0x5c4b6e: with the shop, 0x78abb28 at 6.
constexpr std::int32_t ShopHelpPlace = 6;
static_assert(Npc::NpcGridColumns == static_cast<std::int32_t>(Kernel::InventoryColumns));

// 0x5c4b64..0x5c510f: the item under the mouse in the shop grid. 0x78abb28 at 6 and 0x78abb2c at the cell; with the
// click, not in repair mode and with no other purchase waiting (0x5767950), the click spent, 0x54d0f0's price in mode
// 0 of the top-left item, the C1 04 32 with its slot in C3 and the price into 0x5767950, until the 32.
Core::Expected<GridHover, std::string> BuyTheGridItem(const Ui::GameUiContext& context, const HoverGrid& grid,
                                                      std::size_t index)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ItemState& hovered = grid.cells[index];
    runtime.ui.field78abb28 = ShopHelpPlace;
    runtime.ui.field78abb2c = static_cast<std::int32_t>(index);
    Npc::NpcWindowState& npc = runtime.npcWindows;
    if (!runtime.mouse.pendingClick || runtime.panels.repairMode != 0)
        return GridHover::Stayed;
    runtime.mouse.pendingClick = false;
    if (npc.buyLock != 0)
        return GridHover::Stayed;
    const std::int32_t topLeft = hovered.gridRow * Npc::NpcGridColumns + hovered.gridColumn;
    if (topLeft >= static_cast<std::int32_t>(grid.cells.size()))
        return Core::Unexpected(std::format("0x5c4bc4: el ítem de arriba a la izquierda ({}) cae fuera de la grilla de "
                                            "la tienda",
                                            topLeft));
    const Tables::ItemTable* items = ServicesOf(context).items;
    if (items == nullptr)
        return Core::Unexpected(std::string("0x5c4bc4: el precio de 0x54d0f0 sin Item.bmd"));
    const Items::PetPriceLevels pets{.raven = runtime.pets[Kernel::RavenPetStats].level,
                                     .horse = runtime.pets[Kernel::DarkHorsePetStats].level};
    auto price = Items::ItemPrice(grid.cells[static_cast<std::size_t>(topLeft)], *items, Items::ShopGridPrice, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    context.random.Discard(C3SendRandoms);
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::string("0x5c4bdc: el C1 04 32 sin la sesión"));
    if (auto sent = context.sendNpcWindow(Protocol::Npc::WriteBuy(static_cast<std::uint8_t>(topLeft & 0xff)));
        !sent.HasValue())
        return Core::Unexpected(std::move(sent).Error());
    npc.buyLock = static_cast<std::int32_t>(price.Value());
    return GridHover::Stayed;
}

// 0x5c5895..0x5c9b0b (CL9 milestone 5): the right click on the item of cell `index` of the grid, consumed, with the
// item's top-left cell (with the grid's width, without the 12). Those that only send the C1 06 26 (UseInventoryItem):
// 0x1c0a without 0x576794c (0x5c58bd), the level 14 0x1c0b (0x5c608c) and the potions (0x1c00..0x1c06, 0x1c08, 0x1c09,
// the level 0 0x1c14 and 0x1c07, 0x5c73ea). The event entries stop with their address (0x5c6865, after the windows
// the 0x1a1d checks; 0x5c6c36 and 0x5c7010). From 0x5c7bc6, the skill books and orbs, the fruit, 0x1a0b and the rest:
// UseTheLaterGridItem. With the trade's own grid (0x5c4aa5 with 0x78a92a8, CL9 milestone 7, 08 P-C235) the branch is
// the same: what reaches UseInventoryItem gives warning 474, because the trade is open, and the others go on in
// UseTheLaterGridItem with that grid.
Core::Expected<void, std::string> UseTheGridItem(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                                 const HoverGrid& grid, std::size_t index)
{
    const Kernel::ItemState& hovered = grid.cells[index];
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.ui.rightClick = false;
    constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    const std::int32_t type = std::bit_cast<std::int16_t>(hovered.id);
    const std::uint32_t levelBits = hovered.scalar1Low & UsedLevelBits;
    const auto use = [&] { return Items::UseInventoryItem(MovesOf(context), panels, cell); };
    if (type == TownPortalScroll)
    {
        if (runtime.movement.gateRequestBlocked != 0)
            return {};
        return use();
    }
    if (type == BoxOfLuck && levelBits == UsedBoxLevelBits)
        return use();
    if (Items::IsAnEventTicket(type))
        return Items::UseTheEventTicket(MovesOf(context), panels,
                                        Items::UsedGrid{.cells = grid.cells, .grid = grid.grid}, index);
    const bool potion = (type >= 0x1c00 && type <= 0x1c06) || type == 0x1c08 || type == 0x1c09 ||
                        (type == 0x1c14 && levelBits == 0) || type == 0x1c07;
    if (potion)
        return use();
    const Kernel::CursorGrid used = grid.place == Ui::HoveredItem::Place::OwnTradeCell ? Kernel::CursorGrid::OwnTrade
                                    : grid.place == Ui::HoveredItem::Place::InventoryCell
                                        ? Kernel::CursorGrid::Inventory
                                        : grid.grid;
    return Items::UseTheLaterGridItem(MovesOf(context), panels, Items::UsedGrid{.cells = grid.cells, .grid = used},
                                      index);
}

// 0x5c5361..0x5c5876, after picking up: IsOpen(0x20), with its rand(); with the trade open, the own OK set and the
// item from the own grid, the OK at 0 and the C1 04 3C 00 in C3. Then sound 0x1d.
Core::Expected<void, std::string> AfterThePickUp(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                                 const HoverGrid& grid)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::TradeState& trade = runtime.social.trade;
    if (panels.IsOpen(Ui::TradePanel) && trade.ownOk != 0 && grid.place == Ui::HoveredItem::Place::OwnTradeCell)
    {
        trade.ownOk = 0;
        context.random.Discard(2);
        const Items::InventoryServices& services = ServicesOf(context);
        if (!services.sendGamePacket)
            return Core::Unexpected(std::string("0x5c5396: el C1 04 3C sin la sesión"));
        services.sendGamePacket(Protocol::Social::WriteTradeOkButton(0));
    }
    runtime.RequestSound(ItemMoveSound);
    return {};
}

// The grid of `place` as 0x78ab9b8 stores it.
Kernel::CursorGrid CursorGridOf(Ui::HoveredItem::Place place)
{
    switch (place)
    {
    case Ui::HoveredItem::Place::InventoryCell:
        return Kernel::CursorGrid::Inventory;
    case Ui::HoveredItem::Place::OwnTradeCell:
        return Kernel::CursorGrid::OwnTrade;
    case Ui::HoveredItem::Place::OtherTradeCell:
        return Kernel::CursorGrid::OtherTrade;
    case Ui::HoveredItem::Place::ShopCell:
        return Kernel::CursorGrid::PersonalShop;
    default:
        return Kernel::CursorGrid::None;
    }
}

// 0x5c5110..0x5c5241 (CL9 milestone 7, 08 P-C208 b): the click on the item with a personal shop open, not repairing.
// The click consumed, the grid into 0x78ab9b8, the top-left cell and 0x78a6758 at −1. Viewing another's (0x6c931c at
// 2), its grid's item goes to 0x78a59c8 with its slot (+ 0x4c) and dialog 0x94 of texts 1100 (0x633300(1, 1100, 0x94))
// waits for warning 5 (0x78abb14) with no answer; one from the inventory is picked up (+ 12). With the own (1), its
// grid's item (+ 0x4c) or the inventory's (+ 12) is picked up. At the end, 0x78abaa8 at 0; neither the sound nor
// 0x5c5361's trade.
Core::Expected<void, std::string> ClickWithTheShopOpen(const Ui::GameUiContext& context, const HoverGrid& grid,
                                                       const Kernel::ItemState& hovered)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.mouse.pendingClick = false;
    const Kernel::CursorGrid from = CursorGridOf(grid.place);
    runtime.cursorGrid = from;
    constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    runtime.slot78a6758 = NoCursorSlot;
    const std::int32_t mode = runtime.social.personalShop.mode;
    const bool shopGrid = from == Kernel::CursorGrid::PersonalShop;
    const bool inventoryGrid = from == Kernel::CursorGrid::Inventory;
    if (mode == Social::OtherShopView && shopGrid)
    {
        runtime.slot78a6758 = cell + FirstPersonalShopSlot;
        if (static_cast<std::size_t>(cell) >= grid.cells.size())
            return Core::Unexpected(std::format("0x5c515f: la casilla {} está fuera de la grilla de la tienda", cell));
        runtime.dialogItem = grid.cells[static_cast<std::size_t>(cell)];
        if (auto opened = Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, BuyDialogLines,
                                             BuyDialogText, BuyDialog);
            !opened.HasValue())
            return opened;
        runtime.social.trade.warningKind = BuyWarningKind;
        runtime.social.trade.warningByte = NoWarningAnswer;
        runtime.ui.hoveredItem = {};
        return {};
    }
    const bool picks = (mode == Social::OwnShopView && (shopGrid || inventoryGrid)) ||
                       (mode == Social::OtherShopView && inventoryGrid);
    if (picks)
    {
        const std::int32_t slot = cell + (shopGrid ? FirstPersonalShopSlot : FirstGridSlot);
        if (auto picked = Items::PickUpForTheShop(MovesOf(context), slot, from); !picked.HasValue())
            return picked;
    }
    runtime.ui.hoveredItem = {};
    return {};
}

// 0x5c5242..0x5c52b0 (CL9 milestone 7, 08 P-C208 b): the right click with a personal shop open, not repairing. The
// right click consumed, the grid into 0x78ab9b8, the top-left item into 0x78a59c8 and its cell + 0x4c into 0x78a6758
// (with any grid). With the own shop and its grid, the price dialog (0x5eba70(2)). 0x78abaa8 stays.
Core::Expected<void, std::string> RightClickWithTheShopOpen(const Ui::GameUiContext& context, const HoverGrid& grid,
                                                            const Kernel::ItemState& hovered)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.ui.rightClick = false;
    const Kernel::CursorGrid from = CursorGridOf(grid.place);
    runtime.cursorGrid = from;
    constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    if (static_cast<std::size_t>(cell) >= grid.cells.size())
        return Core::Unexpected(std::format("0x5c526d: la casilla {} está fuera de la grilla", cell));
    runtime.dialogItem = grid.cells[static_cast<std::size_t>(cell)];
    runtime.slot78a6758 = cell + FirstPersonalShopSlot;
    if (runtime.social.personalShop.mode == Social::OwnShopView && from == Kernel::CursorGrid::PersonalShop)
        PersonalShop::OpenTheShopDialog(runtime, PersonalShop::ShopPriceMode);
    return {};
}

// 0x5c41f4..0x5c4b3b and 0x5c52b1..0x5c5894: the item of cell `index` of `grid` under the mouse. Mark 2 on its
// footprint (on the other's in the trade, without overwriting the 99), the label item (0x78abaa8, its copy at 0x787a518
// and its slot) and its help's corner. With the other's grid, only 0x5aed40 (0x5c4b2b); with the NPC shop, buy.
// Otherwise, depending on the personal shops, the repair and the clicks, pick it up, use it, 0x5aed40 and Ctrl with Q,
// W or E.
Core::Expected<GridHover, std::string> HoverTheGridItem(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                                        const HoverGrid& grid, std::size_t index, std::int32_t left,
                                                        std::int32_t top)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    const std::span<Kernel::ItemState> cells = grid.cells;
    const Kernel::ItemState& hovered = cells[index];
    const bool ownTrade = grid.place == Ui::HoveredItem::Place::OwnTradeCell;
    const bool otherTrade = grid.place == Ui::HoveredItem::Place::OtherTradeCell;
    // 0x5c41f4..0x5c4284: Item.bmd's width and height, and the mark from the item's column and row, without checking
    // the edges: a placed item fits whole.
    const auto footprint = Items::ItemFootprintOf(ServicesOf(context).items, hovered.id, "0x5c4110 (0x5c420a)");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    const std::int32_t column = hovered.gridColumn;
    const std::int32_t row = hovered.gridRow;
    constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    static_assert(Columns == Social::TradeGridColumns && Columns == Npc::NpcGridColumns);
    for (std::int32_t y = row; y < row + footprint.Value().height; ++y)
    {
        for (std::int32_t x = column; x < column + footprint.Value().width; ++x)
        {
            const std::int32_t marked = y * Columns + x;
            if (marked >= static_cast<std::int32_t>(cells.size()))
                return Core::Unexpected(std::format("0x5c425e: la marca de la casilla {} cae fuera de la grilla "
                                                    "(Q-C200)",
                                                    marked));
            Kernel::ItemState& marking = cells[static_cast<std::size_t>(marked)];
            // 0x5c4243..0x5c4256: in the other's grid, the copy's 99 (0x5bc470) stays.
            if (otherTrade && marking.warning == Social::ChangedFromTheCopy)
                continue;
            marking.warning = HoveredMark;
        }
    }
    // 0x5c4286..0x5c4302.
    const auto cell = static_cast<std::int32_t>(index);
    ui.field78aba27 = grid.kind;
    ui.hoveredItem = Ui::HoveredItem{.place = grid.place, .index = index};
    runtime.hoveredItemCopy = hovered;
    ui.field78abb2c = cell + FirstGridSlot;
    ui.skillTooltipX = left + footprint.Value().width * CellSize / 2 + column * CellSize;
    ui.skillTooltipY = top + row * CellSize;
    // 0x5c42fd..0x5c4308 → 0x5c4b2b: the trade's other grid, 0x5aed40 with 3 and the cell, and the next cell.
    if (otherTrade)
    {
        ui.field78abb28 = OtherTradeTooltip;
        ui.field78abb2c = cell;
        return GridHover::Stayed;
    }
    if (grid.kind != 0)
        return BuyTheGridItem(context, grid, index);
    // 0x5c4319..0x5c4324: the personal shop grid.
    if (grid.place == Ui::HoveredItem::Place::ShopCell)
        ui.shopGridHovered = 1;
    // 0x5c432b..0x5c43ee: the personal shops (0x800 and 0x20000), without rand(), and the repair (0x78abb0c).
    const auto shopOpen = [&panels]
    { return panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel); };
    if (shopOpen())
    {
        ui.field78abb28 = 0;
        ui.field78abb2c = cell + FirstPersonalShopSlot;
    }
    const bool repairing = runtime.panels.repairMode != 0;
    if (shopOpen() && runtime.mouse.pendingClick && !repairing)
    {
        if (auto clicked = ClickWithTheShopOpen(context, grid, hovered); !clicked.HasValue())
            return Core::Unexpected(clicked.Error());
        return GridHover::PickedUp;
    }
    if (shopOpen() && ui.rightClick && !repairing)
    {
        if (auto clicked = RightClickWithTheShopOpen(context, grid, hovered); !clicked.HasValue())
            return Core::Unexpected(clicked.Error());
        return GridHover::RightClicked;
    }
    if (repairing)
    {
        if (IsNotRepaired(hovered.id) || !runtime.mouse.pendingClick)
            return GridHover::Stayed;
        // 0x5c4472..0x5c4a89: the click consumed and the C1 05 34 with the slot of the item's corner (row · columns +
        // column + 12) and 0x78abb10; then it goes on with the next cell (0x5c4b3b).
        runtime.mouse.pendingClick = false;
        const auto position = static_cast<std::uint8_t>((row * Columns + column + FirstGridSlot) & 0xff);
        if (auto sent = Npc::SendRepair(context.random, context.sendNpcWindow, position,
                                        static_cast<std::uint8_t>(runtime.field78abb10 & 0xff));
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return GridHover::Stayed;
    }
    // 0x5c4a8e..0x5c4a9f and 0x5c52b1..0x5c5894: pick it up.
    if (runtime.mouse.pendingClick && runtime.movement.gateRequestBlocked == 0)
    {
        auto picked = ownTrade ? Items::PickUpFromTheTradeGrid(MovesOf(context), index)
                      : grid.place == Ui::HoveredItem::Place::InventoryCell
                          ? Items::PickUpFromTheGrid(MovesOf(context), index)
                          : Items::PickUpFromOtherGrid(MovesOf(context), grid.grid, index);
        if (!picked.HasValue())
            return Core::Unexpected(picked.Error());
        if (auto after = AfterThePickUp(context, panels, grid); !after.HasValue())
            return Core::Unexpected(after.Error());
        return GridHover::PickedUp;
    }
    // 0x5c4aa5..0x5c4aac.
    if (ui.rightClick)
    {
        if (auto used = UseTheGridItem(context, panels, grid, index); !used.HasValue())
            return Core::Unexpected(used.Error());
        return GridHover::RightClicked;
    }
    // 0x5c4ab7: 0x5aed40 with the grid: the inventory's with 0 and the cell + 12, the trade's own with 2 and the cell;
    // the shop's, viewing another's (0x5aedee..0x5aee10), with 5 and the cell + 0x4c, and with the own nothing
    // (0x5c4353's stay); the chaos box and the trainer's box (0x449c30) with 4 and the cell, and the warehouse's (with
    // 0x40 open, no rand()) with 1 and the cell.
    if (grid.place == Ui::HoveredItem::Place::ShopCell)
    {
        if (runtime.social.personalShop.mode == Social::OtherShopView)
        {
            ui.field78abb28 = OtherShopTooltip;
            ui.field78abb2c = cell + FirstPersonalShopSlot;
        }
    }
    else if (grid.grid == Kernel::CursorGrid::ChaosBox || grid.grid == Kernel::CursorGrid::TrainerBox)
    {
        ui.field78abb28 = ChaosBoxHelpPlace;
        ui.field78abb2c = cell;
    }
    else if (grid.grid == Kernel::CursorGrid::NpcGrid)
    {
        if (panels.IsOpen(Ui::WarehousePanel))
        {
            ui.field78abb28 = WarehouseHelpPlace;
            ui.field78abb2c = cell;
        }
    }
    else
    {
        ui.field78abb28 = ownTrade ? OwnTradeTooltip : 0;
        ui.field78abb2c = ownTrade ? cell : cell + FirstGridSlot;
    }
    // 0x5c4abf..0x5c4b29: GetAsyncKeyState(0x11) and 0x553520 of Q, W and E, in that order; the first wins.
    if (!context.input.IsDown(CtrlKey.key))
        return GridHover::Stayed;
    for (std::size_t slot = 0; slot < CtrlQuickSlotLetters.size(); ++slot)
    {
        if (!KeyEdge(ui, context.input, Letter(CtrlQuickSlotLetters[slot])))
            continue;
        runtime.hud.quickSlots.types[slot] = static_cast<std::int16_t>(hovered.id);
        runtime.hud.quickSlots.levels[slot] = static_cast<std::int32_t>((hovered.scalar1Low >> LevelShift) & LevelMask);
        break;
    }
    return GridHover::Stayed;
}

// 0x5c4110(x, y, grid, 8, rows, kind) with the NPC windows' grids. Returns with the wait, the C3 24 lock or an item on
// the cursor; otherwise 0x78abb38 at 0 (0x5c4163) and it walks the cells by rows: with the mouse over one with a type,
// that item's handling. Picking up or the right click end it.
Core::Expected<void, std::string> StepTheGrid(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                              const HoverGrid& grid, std::int32_t left, std::int32_t top)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    runtime.ui.shopGridHovered = 0;
    for (std::int32_t row = 0; row < grid.rows; ++row)
    {
        const std::int32_t cellTop = top + row * CellSize;
        for (std::int32_t column = 0; column < Npc::NpcGridColumns; ++column)
        {
            const std::int32_t cellLeft = left + column * CellSize;
            const auto index = static_cast<std::size_t>(row * Npc::NpcGridColumns + column);
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize || grid.cells[index].id == Kernel::EmptyOwnItemId)
                continue;
            const auto hovered = HoverTheGridItem(context, panels, grid, index, left, top);
            if (!hovered.HasValue())
                return Core::Unexpected(hovered.Error());
            if (hovered.Value() != GridHover::Stayed)
                return {};
        }
    }
    return {};
}

// 0x5c4110(x + 15, y + 200, 0x78a9bb0, 8, 8, 0): the inventory grid. Returns with the wait, the C3 24 lock or an item
// on the cursor; otherwise it walks the cells by rows and, with the mouse over one with a type, that item's handling.
Core::Expected<void, std::string> StepTheInventoryGrid(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    // 0x5c4163.
    runtime.ui.shopGridHovered = 0;
    const std::int32_t left = runtime.hud.inventoryX + 15;
    const std::int32_t top = runtime.hud.inventoryY + 200;
    for (std::size_t row = 0; row < Kernel::InventoryColumns; ++row)
    {
        const std::int32_t cellTop = top + static_cast<std::int32_t>(row) * CellSize;
        for (std::size_t column = 0; column < Kernel::InventoryColumns; ++column)
        {
            const std::int32_t cellLeft = left + static_cast<std::int32_t>(column) * CellSize;
            const std::size_t index = row * Kernel::InventoryColumns + column;
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize ||
                context.world.Inventory().cells[index].id == Kernel::EmptyOwnItemId)
                continue;
            const auto hovered = HoverTheGridItem(
                context, panels,
                HoverGrid{.cells = context.world.Inventory().cells, .place = Ui::HoveredItem::Place::InventoryCell},
                index, left, top);
            if (!hovered.HasValue())
                return Core::Unexpected(hovered.Error());
            if (hovered.Value() != GridHover::Stayed)
                return {};
        }
    }
    return {};
}

// 0x5e545f..0x5e5717: the foot buttons (repair, close and the personal shop) and the S, without the trainer's box
// (0x449c00). With an event NPC's window (0x1000 or 0x4000), only close at x + 0x19; MuRet already stopped with it
// open.
Core::Expected<void, std::string> StepTheFootButtons(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                                     std::uint16_t level)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::PanelState& state = runtime.panels;
    Kernel::MouseState& mouse = runtime.mouse;
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    const auto inFoot = [&context, x, y](std::int32_t from, std::int32_t to)
    { return InRect(context, x + from, y + 0x18b, x + to, y + 0x1a3); };
    const auto toggleThePersonalShop = [&panels]() -> Core::Expected<void, std::string>
    {
        if (panels.IsOpen(Ui::PersonalShopPanel))
        {
            if (auto closed = panels.Close(Ui::PersonalShopPanel); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            return {};
        }
        if (auto opened = panels.Open(Ui::PersonalShopPanel); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return {};
    };
    if (state.delegated.trainer == TrainerWithBox)
        return {};
    if (panels.IsOpen(Ui::EventNpcPanel) || panels.IsOpen(Ui::SecondEventNpcPanel))
        return {};
    if (panels.IsOpen(Ui::TradePanel) || panels.IsOpen(Ui::NpcShopPanel) || panels.IsOpen(Ui::WarehousePanel) ||
        panels.IsOpen(Ui::ChaosBoxPanel) || state.field78abb00 != 0)
        return {};
    if (level >= RepairButtonLevel)
    {
        runtime.field78abb10 = 1;
        if (inFoot(0x30, 0x48) && mouse.pendingClick)
        {
            mouse.pendingClick = false;
            state.repairMode ^= 1;
        }
    }
    if (inFoot(0x14, 0x2c) && mouse.pendingClick)
    {
        mouse.pendingClick = false;
        mouse.updateTime = 0;
        mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
        if (auto closed = panels.Close(Ui::InventoryPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    if (inFoot(0x4c, 100))
    {
        if (level < PersonalShopLevel)
            return {};
        if (mouse.pendingClick)
        {
            mouse.pendingClick = false;
            if (auto toggled = toggleThePersonalShop(); !toggled.HasValue())
                return toggled;
        }
    }
    // 0x5e5643..0x5e5717: the S, with level 6 or more and without the chat field. MuRet does not have 0x77f3b69 (a
    // milestone 6 field), and the 0x73 dialog with 0x6c7b8c cannot be up: 0x5ee140 does not get here with a dialog.
    if (level < PersonalShopLevel || runtime.ui.chatOpen || !KeyEdge(runtime.ui, context.input, Letter('S')))
        return {};
    return toggleThePersonalShop();
}

// Sends a packet over the game socket with send(), without the queue (0x5e494b, 0x424610). With the socket at −1
// (`send` empty) it sends nothing and goes on, like the original; without the session context (the tests that do not
// check it), it stops.
Core::Expected<void, std::string> SendRaw(const Ui::GameUiContext& context, Protocol::Outgoing packet,
                                          std::string_view where)
{
    if (context.chat == nullptr)
        return Core::Unexpected(std::format("{}: el send() sin el contexto de la sesión", where));
    if (context.chat->send)
        context.chat->send(std::move(packet));
    return {};
}

// 0x5e4510: with the character open, the mouse over the window belongs to the UI; with points to spend (+0x68), the
// buttons of the four stats (five with the Dark Lord, 0x6c9330) send F3 06, with the own record open around them
// (3848 rand() when closing it without other accesses); and the close button.
Core::Expected<void, std::string> StepTheCharacterWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::CharacterPanel))
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    const std::int32_t x = runtime.hud.characterX;
    const std::int32_t y = runtime.hud.characterY;
    if (InRect(context, x, y, x + CharacterWindowWidth, y + CharacterWindowHeight))
        runtime.ui.mouseOnUi = 1;
    const Kernel::ActorState& hero = context.world.Hero();
    const std::int32_t buttons = Kernel::BaseClassOf(hero.characterClass) == Kernel::CharacterClass::DarkLord ? 5 : 4;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        if (context.world.OwnCharacter().levelUpPoints != 0)
        {
            for (std::int32_t button = 0; button < buttons; ++button)
            {
                const std::int32_t top = y + 0x73 + button * 0x3c;
                if (!InRect(context, x + 0x7d, top, x + 0x95, top + 0x18) || !mouse.pendingClick)
                    continue;
                mouse.pendingClick = false;
                if (auto sent = SendRaw(context, Protocol::Player::WriteLevelUpPoint(static_cast<std::uint8_t>(button)),
                                        "0x5e494b");
                    !sent.HasValue())
                    return sent;
                runtime.RequestSound(ButtonSound);
            }
        }
    }
    if (!InRect(context, x + 0x19, y + 0x18b, x + 0x31, y + 0x1a3) || !mouse.pendingClick)
        return {};
    mouse.pendingClick = false;
    mouse.updateTime = 0;
    mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    if (auto closed = panels.Close(Ui::CharacterPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

// 0x424759..0x424990, the pet window open (CL9 milestone 4): left of 0x1c2, or of 0x104 with the character open,
// 190 × 433 belongs to the UI (0x775857c). The two tabs (0 the horse and 1 the raven, 45 apart from 15, 49 wide): a
// click on the unchosen one, between 67 and 83, chooses it; on the chosen one, between 63 and 83, it only eats the
// click. The close button (20, 395, 24 × 24) closes panel 0x400. All in float, with the edges included except the
// UI's and the button's. No rand().
Core::Expected<void, std::string> StepThePetWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    const std::int32_t left = panels.IsOpen(Ui::CharacterPanel) ? PetWindowBesideTheCharacter : PetWindowLeft;
    const auto mouseX = static_cast<float>(context.mouseX);
    const auto mouseY = static_cast<float>(context.mouseY);
    const auto base = static_cast<float>(left);
    if (left <= context.mouseX && mouseX < base + PetWindowWidth && context.mouseY >= 0 && mouseY < PetWindowHeight)
        runtime.ui.mouseOnUi = 1;
    const auto clickTab = [&](std::uint8_t tab, float top)
    {
        const float x = static_cast<float>(tab * PetTabStep) + base + PetTabLeft;
        if (x <= mouseX && mouseX <= x + PetTabWidth && top <= mouseY && mouseY <= PetTabBottom && mouse.pendingClick)
        {
            mouse.leftHeld = false;
            mouse.pendingClick = false;
            runtime.petTab = tab;
        }
    };
    for (std::uint8_t tab = 0; tab < PetTabCount; ++tab)
        if (runtime.petTab != tab)
            clickTab(tab, PetOtherTabTop);
    for (std::uint8_t tab = 0; tab < PetTabCount; ++tab)
        if (runtime.petTab == tab)
            clickTab(tab, PetChosenTabTop);
    const float closeLeft = base + PetCloseLeft;
    if (closeLeft <= mouseX && mouseX < closeLeft + PetCloseSize && PetCloseTop <= mouseY &&
        mouseY < PetCloseTop + PetCloseSize && mouse.pendingClick)
    {
        mouse.leftHeld = false;
        mouse.pendingClick = false;
        if (auto closed = panels.Close(Ui::PetPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    return {};
}

// 0x424130 → 0x424230: with the character closed, 0x723911 at 0. Open, the first time the dark horse request
// (C1 06 A9 01 00 08, Q-C16) and 0x723911 at 1; the pet button opens or closes panel 0x400. With 0x723911 at 2, the
// pet window's step. 0x416660, from 0x424130 with 2, does nothing.
Core::Expected<void, std::string> StepThePetButton(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    std::uint8_t& pet = runtime.panels.delegated.pet;
    if (!panels.IsOpen(Ui::CharacterPanel))
    {
        pet = 0;
        return {};
    }
    if (pet == 0)
    {
        const Protocol::Pets::PetInfoRequest horse{Protocol::Pets::DarkHorsePet, Protocol::Pets::InventoryPlace,
                                                   Protocol::Pets::PetSlot};
        if (auto sent = SendRaw(context, Protocol::Pets::WritePetInfoRequest(horse), "0x424610"); !sent.HasValue())
            return sent;
        pet = 1;
    }
    Kernel::MouseState& mouse = runtime.mouse;
    if (InRect(context, PetButtonLeft, PetButtonTop, PetButtonRight, PetButtonBottom) && mouse.pendingClick)
    {
        mouse.leftHeld = false;
        mouse.pendingClick = false;
        auto toggled = panels.IsOpen(Ui::PetPanel) ? panels.Close(Ui::PetPanel) : panels.Open(Ui::PetPanel);
        if (!toggled.HasValue())
            return Core::Unexpected(std::move(toggled).Error());
    }
    if (pet == PetWindowOpen)
        return StepThePetWindow(context, panels);
    return {};
}

// 0x5e5728..0x5e5767: 0x5c4110 with the trade's two grids (0x78a5a20 at y + 0x46 and 0x78a92a8 at y + 0x10e, 8 × 4,
// from x + 15, with the last argument at 0). Each returns as with the inventory (the wait, the C3 24 lock or an item
// on the cursor) and walks its cells by rows: with the mouse over one with a type, that item's handling
// (HoverTheGridItem). Picking up or the right click end the own grid's; the other's goes on.
Core::Expected<void, std::string> StepTheTradeGrids(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::TradeState& trade = runtime.social.trade;
    const auto scan = [&](const HoverGrid& grid, std::int32_t top) -> Core::Expected<void, std::string>
    {
        if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
            return {};
        runtime.ui.shopGridHovered = 0;
        const std::int32_t left = runtime.hud.tradeX + 15;
        for (std::int32_t row = 0; row < Social::TradeGridRows; ++row)
        {
            for (std::int32_t column = 0; column < Social::TradeGridColumns; ++column)
            {
                const std::int32_t cellLeft = left + column * CellSize;
                const std::int32_t cellTop = top + row * CellSize;
                const auto index = static_cast<std::size_t>(row * Social::TradeGridColumns + column);
                if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                    context.mouseY >= cellTop + CellSize || grid.cells[index].id == Kernel::EmptyOwnItemId)
                    continue;
                const auto hovered = HoverTheGridItem(context, panels, grid, index, left, top);
                if (!hovered.HasValue())
                    return Core::Unexpected(hovered.Error());
                if (hovered.Value() != GridHover::Stayed)
                    return {};
            }
        }
        return {};
    };
    if (auto other = scan(HoverGrid{.cells = trade.otherGrid, .place = Ui::HoveredItem::Place::OtherTradeCell},
                          runtime.hud.tradeY + 0x46);
        !other.HasValue())
        return other;
    return scan(HoverGrid{.cells = trade.ownGrid, .place = Ui::HoveredItem::Place::OwnTradeCell},
                runtime.hud.tradeY + 0x10e);
}

// 0x5e58ea..0x5e5904 (CL9 milestone 7, 08 P-C208 b): 0x5c4110(x + 15, y + 0x6e, 0x78ab0b8, 8, 4, 0) with the corner
// of the personal shops window (0x78aba58 and 0x78aba5c, written by its step 0x5ea290 with the own shop and its draw
// 0x5f3030). Returns like the others and walks the cells by rows; picking up or the right click end it.
Core::Expected<void, std::string> StepTheShopGrid(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    runtime.ui.shopGridHovered = 0;
    const Social::PersonalShopState& shops = runtime.social.personalShop;
    const std::int32_t left = shops.windowX + ShopGridLeft;
    const std::int32_t top = shops.windowY + ShopGridTop;
    const std::span<Kernel::ItemState> cells(context.world.Inventory().secondCells);
    for (std::int32_t row = 0; row < ShopGridRows; ++row)
    {
        for (std::int32_t column = 0; column < ShopGridColumns; ++column)
        {
            const std::int32_t cellLeft = left + column * CellSize;
            const std::int32_t cellTop = top + row * CellSize;
            const auto index = static_cast<std::size_t>(row * ShopGridColumns + column);
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize || cells[index].id == Kernel::EmptyOwnItemId)
                continue;
            const auto hovered =
                HoverTheGridItem(context, panels, HoverGrid{.cells = cells, .place = Ui::HoveredItem::Place::ShopCell},
                                 index, left, top);
            if (!hovered.HasValue())
                return Core::Unexpected(hovered.Error());
            if (hovered.Value() != GridHover::Stayed)
                return {};
        }
    }
    return {};
}

// 0x5e571c..0x5e5910: the other windows' grids, the trainer's box (0x44ad10) and the set label (0x40e6c0).
Core::Expected<void, std::string> StepTheOtherGrids(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::PanelState& state = runtime.panels;
    if (panels.IsOpen(Ui::TradePanel))
    {
        if (auto trade = StepTheTradeGrids(context, panels); !trade.HasValue())
            return trade;
    }
    // 0x5e577d..0x5e589c: 0x5c4110 from the NPC window's corner, the shop grid with kind 1 and the warehouse's with 0,
    // 8 × 15 at (x + 0xf, y + 0x32), and the 8 × 4 chaos box at (x + 0xf, y + 0x6e) with the mix other than 1.
    Npc::NpcWindowState& npc = runtime.npcWindows;
    const std::int32_t npcLeft = runtime.ui.npcWindowX + 0xf;
    const HoverGrid npcGrid{.cells = npc.grid,
                            .place = Ui::HoveredItem::Place::NpcGridCell,
                            .rows = static_cast<std::int32_t>(Npc::NpcGridCellCount) / Npc::NpcGridColumns,
                            .kind = 0,
                            .grid = Kernel::CursorGrid::NpcGrid};
    if (panels.IsOpen(Ui::NpcShopPanel))
    {
        HoverGrid shop = npcGrid;
        shop.kind = ShopGridKind;
        if (auto hovered = StepTheGrid(context, panels, shop, npcLeft, runtime.ui.npcWindowY + 0x32);
            !hovered.HasValue())
            return hovered;
    }
    if (panels.IsOpen(Ui::WarehousePanel))
    {
        if (auto hovered = StepTheGrid(context, panels, npcGrid, npcLeft, runtime.ui.npcWindowY + 0x32);
            !hovered.HasValue())
            return hovered;
    }
    if ((panels.IsOpen(Ui::ChaosBoxPanel) && npc.chaosMixState != 1) ||
        (panels.IsOpen(Ui::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         CastleSiege::HeroCanCraftAtTheCastle(context.world)))
    {
        const HoverGrid chaos{.cells = npc.chaosBox,
                              .place = Ui::HoveredItem::Place::ChaosBoxCell,
                              .rows = Npc::ChaosBoxRows,
                              .kind = 0,
                              .grid = Kernel::CursorGrid::ChaosBox};
        if (auto hovered = StepTheGrid(context, panels, chaos, npcLeft, runtime.ui.npcWindowY + 0x6e);
            !hovered.HasValue())
            return hovered;
    }
    if (panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel))
    {
        if (auto shop = StepTheShopGrid(context, panels); !shop.HasValue())
            return shop;
    }
    // 0x5e58f2: 0x44ad10, with 0x724580 at 3, 0x5c4110(0x113, 0x6e, 0x723c80, 8, 4, 0).
    if (state.delegated.trainer == TrainerWithBox)
    {
        const HoverGrid trainer{.cells = npc.trainerBox,
                                .place = Ui::HoveredItem::Place::TrainerBoxCell,
                                .rows = Npc::ChaosBoxRows,
                                .kind = 0,
                                .grid = Kernel::CursorGrid::TrainerBox};
        if (auto hovered = StepTheGrid(context, panels, trainer, TrainerBoxLeft, TrainerBoxTop); !hovered.HasValue())
            return hovered;
    }
    // 0x40e6c0: the mouse over the label, without looking at the click (Q-C472), turns on the set window (+0x9304),
    // which the tooltips draw (0x40e850), and spends the click's edge.
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    if (InRect(context, x + 0xe, y + 0x16, x + 0xb0, y + 0x2a))
    {
        context.world.ItemSets().byte9304 = 1;
        runtime.mouse.pendingClick = false;
        runtime.mouse.updateTime = 0;
        runtime.mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    }
    return {};
}

// 0x5e4ce0, the inventory. The other windows with a grid stop where they are first queried.
Core::Expected<void, std::string> StepTheInventoryWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Ui::PanelState& state = runtime.panels;
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    // 0x5e4ce0..0x5e4f2c: the mouse over the window belongs to the UI, and over the open NPC one (the shop, with its
    // rand(), the warehouse and the chaos box), at 0x78aba28's corner, too.
    if (InRect(context, x, y, x + 0xbe, y + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    const std::int32_t npcX = runtime.ui.npcWindowX;
    const std::int32_t npcY = runtime.ui.npcWindowY;
    const auto overTheNpcWindow = [&context, &runtime, npcX, npcY]
    {
        if (InRect(context, npcX, npcY, npcX + 0xbe, npcY + 0x1b1))
            runtime.ui.mouseOnUi = 1;
    };
    if (panels.IsOpen(Ui::NpcShopPanel))
        overTheNpcWindow();
    if (panels.IsOpen(Ui::WarehousePanel))
        overTheNpcWindow();
    if (panels.IsOpen(Ui::ChaosBoxPanel) ||
        (panels.IsOpen(Ui::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         CastleSiege::HeroCanCraftAtTheCastle(context.world)))
        overTheNpcWindow();
    // 0x5e4e34..0x5e4e7b: the mouse over the trade window (0xbe × 0x1b1 from 0x78a5a1c and 0x78a5a18).
    if (panels.IsOpen(Ui::TradePanel) &&
        InRect(context, runtime.hud.tradeX, runtime.hud.tradeY, runtime.hud.tradeX + 0xbe, runtime.hud.tradeY + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    // 0x5e4e76..0x5e4ed0: with one of the event NPCs' windows, the mouse over its corner (0x78aba28, set by its step
    // and its draw) belongs to the UI.
    if (panels.IsOpen(Ui::EventNpcPanel) || panels.IsOpen(Ui::SecondEventNpcPanel))
        overTheNpcWindow();
    // 0x5e4ed7..0x5e4f31: the mouse over the personal shops window (0xbe × 0x1b1 from 0x78aba58 and 0x78aba5c).
    const Social::PersonalShopState& shops = runtime.social.personalShop;
    if ((panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel)) &&
        InRect(context, shops.windowX, shops.windowY, shops.windowX + 0xbe, shops.windowY + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    // 0x5e4f31..0x5e5128: the twelve equipment slots, each with its access, and +0x44 at 1 with a type.
    for (Kernel::ItemState& item : world.OwnCharacter().equipment)
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        item.warning = item.id == Kernel::EmptyOwnItemId ? std::uint8_t{0} : std::uint8_t{1};
    }
    // 0x5e512f..0x5e5207: the same in the grids: the inventory (0x78a9bb0), the other's in the trade (0x78a5a20,
    // 0x5e515d..0x5e5185, which keeps 0x5bc470's 99), the own (0x78a92a8), the shop's and the warehouse's
    // (0x78a70b4..), the chaos box (0x7d2d3c4..) and the personal shops' (0x78ab0b8, CL9 milestone 7); 0x449c60
    // (0x5e5207) does the same with the trainer's box (0x723c80).
    const auto warn = [](std::span<Kernel::ItemState> cells)
    {
        for (Kernel::ItemState& cell : cells)
            cell.warning = cell.id == Kernel::EmptyOwnItemId ? std::uint8_t{0} : std::uint8_t{1};
    };
    warn(world.Inventory().cells);
    Social::TradeState& trade = runtime.social.trade;
    for (Kernel::ItemState& cell : trade.otherGrid)
    {
        if (cell.id == Kernel::EmptyOwnItemId)
            cell.warning = 0;
        else if (cell.warning != Social::ChangedFromTheCopy)
            cell.warning = 1;
    }
    warn(trade.ownGrid);
    warn(runtime.npcWindows.grid);
    warn(runtime.npcWindows.chaosBox);
    warn(world.Inventory().secondCells);
    warn(runtime.npcWindows.trainerBox);
    runtime.ui.field78abaac = 0;
    // 0x5e520c..0x5e5256: above y + 200 (_ftol of y + 200f), the equipment; otherwise, the grid.
    if (context.mouseY < y + 200)
    {
        if (auto equipment = StepTheEquipment(context); !equipment.HasValue())
            return equipment;
    }
    else if (auto grid = StepTheInventoryGrid(context, panels); !grid.HasValue())
        return grid;
    runtime.field78abb10 = 0;
    std::uint16_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    if (auto buttons = StepTheFootButtons(context, panels, level); !buttons.HasValue())
        return buttons;
    return StepTheOtherGrids(context, panels);
}

// 0x5ee1b1..0x5ee1ca: the six siblings of 0x5e4ce0, each with its IsOpen: the event NPCs, the chaos box (0x5fe610),
// the warehouse (0x5ea060), the trade, the personal shops and the NPC shop (0x5ed960, with its rand()).
Core::Expected<void, std::string> StepTheInventorySiblings(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    // 0x5e5920: the event NPCs (CL9 milestone 11).
    if (auto events = Events::StepTheEventNpcWindow(context, panels); !events.HasValue())
        return events;
    if (auto chaos = Npc::StepTheChaosBox(context, panels); !chaos.HasValue())
        return chaos;
    if (auto warehouse = Npc::StepTheWarehouse(context, panels); !warehouse.HasValue())
        return warehouse;
    if (auto trade = Social::StepTheTradeWindow(context, panels); !trade.HasValue())
        return trade;
    // 0x5ea290, which starts with its IsOpen (CL9 milestone 7).
    if (auto shop = PersonalShop::StepThePersonalShopWindow(context, panels); !shop.HasValue())
        return shop;
    return Npc::StepTheNpcShop(context, panels);
}

// 0x5ee140: the panels with their own step, without a dialog and without the Esc menu.
// 0x5d8d50: the cursor item, from 0x5ee1de with 0x78abaac at 0. With the chaos box open, the value and the mix type
// (0x5d8db6, ChaosMix::ScanTheChaosBox); the siege list with the inventory stops at the start (no rand(), CL9.5); with
// the C3 24 lock, nothing. Then the answers to 0x78abb14's warnings (1 to 6, the NPC windows' with
// Npc::AnswerTheNpcWarning) and to the price dialog (0x78aba60 at 2), which can end the step (StepTheWarningAnswers,
// CL9 milestone 7). With none it goes on at 0x5deac5 with esi at 6 (0x5da592): with an item, drop it on the inventory
// grid (0x5ca250), the trade's, the warehouse's, the chaos box, the personal shop's or the trainer's and, with the
// click outside them, sell it in the NPC shop or throw it on the floor.
Core::Expected<void, std::string> StepTheCursorItem(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::PanelState& state = runtime.panels;
    // 0x5d8d72..0x5d8ddf.
    if (panels.IsOpen(Ui::ChaosBoxPanel) ||
        (panels.IsOpen(Ui::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         CastleSiege::HeroCanCraftAtTheCastle(context.world)))
    {
        if (auto scanned = ChaosMix::ScanTheChaosBox(context.world, context.random, ServicesOf(context).items);
            !scanned.HasValue())
            return scanned;
    }
    // 0x5d8ddf..0x5d8dee.
    if (runtime.ui.field78abb39 != 0)
        return {};
    const Items::InventoryMoveContext moves = MovesOf(context);
    // 0x5d8dee..0x5dc798 and 0x5deaa7..0x5deac0.
    const auto answered = PersonalShop::StepTheWarningAnswers(context, moves);
    if (!answered.HasValue())
        return Core::Unexpected(answered.Error());
    if (!answered.Value())
        return {};
    // 0x5deac5..0x5deacc.
    if (runtime.cursorItemCount <= 0)
        return {};
    // 0x5dead2..0x5deb0e: 0x5ca250(x + 15, y + 200, 0x78a9bb0, 8, 8, 0).
    const auto inGrid =
        Items::DropOnTheInventoryGrid(moves, Items::GridDrop{.mouseX = context.mouseX,
                                                             .mouseY = context.mouseY,
                                                             .left = runtime.hud.inventoryX + 15,
                                                             .top = runtime.hud.inventoryY + 200,
                                                             .inventoryOpen = panels.IsOpen(Ui::InventoryPanel),
                                                             .panels = &panels});
    if (!inGrid.HasValue())
        return Core::Unexpected(inGrid.Error());
    // 0x5deb11..0x5dec41: the other grids. The siege list already stopped above.
    bool inAGrid = inGrid.Value();
    if (panels.IsOpen(Ui::TradePanel))
    {
        // 0x5deb1c..0x5deb43: 0x5ca250(x + 15, y + 0x10e, 0x78a92a8, 8, 4, 1), with edi at 1 from 0x5d8df3.
        const auto inTrade = Items::DropOnTheTradeGrid(moves, Items::GridDrop{.mouseX = context.mouseX,
                                                                              .mouseY = context.mouseY,
                                                                              .left = runtime.hud.tradeX + 15,
                                                                              .top = runtime.hud.tradeY + 0x10e,
                                                                              .inventoryOpen = false,
                                                                              .panels = nullptr});
        if (!inTrade.HasValue())
            return Core::Unexpected(inTrade.Error());
        inAGrid = inAGrid || inTrade.Value();
    }
    const std::int32_t npcLeft = runtime.ui.npcWindowX + 0xf;
    if (panels.IsOpen(Ui::WarehousePanel))
    {
        // 0x5deb57: 0x5ca250(x + 0xf, y + 0x32, 0x78a7070, 8, 0xf, 2).
        const auto dropped = Items::DropOnOtherGrid(moves, Items::OtherGridDrop{.mouseX = context.mouseX,
                                                                                .mouseY = context.mouseY,
                                                                                .left = npcLeft,
                                                                                .top = runtime.ui.npcWindowY + 0x32,
                                                                                .grid = Kernel::CursorGrid::NpcGrid});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inAGrid = inAGrid || dropped.Value();
    }
    if ((panels.IsOpen(Ui::ChaosBoxPanel) && runtime.npcWindows.chaosMixState != 1) ||
        (panels.IsOpen(Ui::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         CastleSiege::HeroCanCraftAtTheCastle(context.world)))
    {
        // 0x5debe4: 0x5ca250(x + 0xf, y + 0x6e, 0x7d2d380, 8, 4, 3).
        const auto dropped = Items::DropOnOtherGrid(moves, Items::OtherGridDrop{.mouseX = context.mouseX,
                                                                                .mouseY = context.mouseY,
                                                                                .left = npcLeft,
                                                                                .top = runtime.ui.npcWindowY + 0x6e,
                                                                                .grid = Kernel::CursorGrid::ChaosBox});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inAGrid = inAGrid || dropped.Value();
    }
    if (panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel))
    {
        // 0x5debf0..0x5dec1e: 0x5ca250(x + 15, y + 0x6e, 0x78ab0b8, 8, 4, 4), with bl at what it returns (CL9 milestone
        // 7).
        const Social::PersonalShopState& shops = runtime.social.personalShop;
        const auto inShop = Items::DropOnTheShopGrid(moves, Items::GridDrop{.mouseX = context.mouseX,
                                                                            .mouseY = context.mouseY,
                                                                            .left = shops.windowX + ShopGridLeft,
                                                                            .top = shops.windowY + ShopGridTop,
                                                                            .inventoryOpen = false,
                                                                            .panels = nullptr});
        if (!inShop.HasValue())
            return Core::Unexpected(inShop.Error());
        inAGrid = inAGrid || inShop.Value();
    }
    // 0x5dec41: 0x44aca0, with 0x724580 at 3, 0x5ca250(0x113, 0x6e, 0x723c80, 8, 4, 5).
    if (state.delegated.trainer == TrainerWithBox)
    {
        const auto dropped =
            Items::DropOnOtherGrid(moves, Items::OtherGridDrop{.mouseX = context.mouseX,
                                                               .mouseY = context.mouseY,
                                                               .left = TrainerBoxLeft,
                                                               .top = TrainerBoxTop,
                                                               .grid = Kernel::CursorGrid::TrainerBox});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inAGrid = inAGrid || dropped.Value();
    }
    // 0x5dec46..0x5deca1: the click, consumed, and nothing more if it stayed in a grid.
    if (!runtime.mouse.pendingClick)
        return {};
    runtime.mouse.pendingClick = false;
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    if (inAGrid)
        return {};
    // 0x5deca7..0x5df448: the NPC shop, with its rand(); with the mouse in its grid, sell it.
    const auto sold = Npc::SellTheCursorItem(context, panels);
    if (!sold.HasValue())
        return Core::Unexpected(sold.Error());
    if (sold.Value())
        return {};
    return Items::DropOnTheGround(moves, Items::GroundDrop{.mouseX = context.mouseX,
                                                           .inventoryOpen = panels.IsOpen(Ui::InventoryPanel),
                                                           .panels = &panels});
}

Core::Expected<void, std::string> StepThePanels(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    if (runtime.dialogs.current == Ui::EscapeMenuDialog)
        return {};
    // 0x5e7bf0: with 0x78abb20 at 0 it returns 0; otherwise, the password box and 1, and 0x5ee140 does not go on.
    auto keypad = Warehouse::StepTheKeypad(context, panels);
    if (!keypad.HasValue())
        return Core::Unexpected(std::move(keypad).Error());
    if (keypad.Value())
        return {};
    // 0x5ee15a..0x5ee188.
    if (runtime.dialogs.current != 0 || ui.capture != 0 || ui.hovered != 0 || ui.field78abad9 != 0)
        return {};
    // 0x5e3c80 and 0x5e4510: the party and the character, each with its IsOpen.
    if (auto party = Party::StepThePartyWindow(context, panels); !party.HasValue())
        return party;
    if (auto character = StepTheCharacterWindow(context, panels); !character.HasValue())
        return character;
    // 0x5ee1a3: the inventory (0x5e4ce0) and its six siblings (0x5e5920, 0x5fe610, 0x5ea060, 0x5ecde0, 0x5ea290 and
    // 0x5ed960).
    if (panels.IsOpen(Ui::InventoryPanel))
    {
        if (auto inventory = StepTheInventoryWindow(context, panels); !inventory.HasValue())
            return inventory;
        if (auto siblings = StepTheInventorySiblings(context, panels); !siblings.HasValue())
            return siblings;
    }
    // 0x44b4e0: the trainer, through 0x724580.
    if (auto trainer = PetTrainer::StepTheTrainer(context, panels); !trainer.HasValue())
        return trainer;
    // 0x44dd50 and 0x44f810: the siege weapon and the gate.
    if (auto siege = CastleSiege::StepTheSiegeControls(context); !siege.HasValue())
        return siege;
    // 0x5ee1de: 0x5d8d50.
    if (ui.field78abaac == 0)
    {
        if (auto cursor = StepTheCursorItem(context, panels); !cursor.HasValue())
            return cursor;
    }
    // 0x5ee1ec: 0x5e6520, with 0x78abb00 other than 0.
    if (runtime.panels.field78abb00 != 0)
    {
        if (auto chips = Events::StepTheEventChipWindow(context, panels); !chips.HasValue())
            return chips;
    }
    // 0x5e74f0
    if (auto stepped = Npc::StepThePanel2000(context, panels); !stepped.HasValue())
        return stepped;
    // 0x40a450, the commands ([0x6e592c]), which starts with its +4.
    if (auto commands = StepTheCommandsWindow(context); !commands.HasValue())
        return commands;
    // 0x5ee20d (CL9 milestone 2): 0x41b5a0, the quest window and its button in the character's.
    if (auto quest = Quests::StepTheQuestWindow(context, panels); !quest.HasValue())
        return quest;
    // 0x5ee212: 0x424130.
    return StepThePetButton(context, panels);
}

// The skill `index` of the list's tooltip and click (0x5a08a1..0x5a08f3) and of the raven order buttons
// (0x420ee4..0x420f4a): the tooltip, the mouse on the UI and, with the click, the hero's skill, the list closed, sound
// 0x19 and the mouse clock.
void PointAtTheSkill(const Ui::GameUiContext& context, std::uint8_t index, std::int32_t tooltipX, std::int32_t tooltipY)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    ui.skillTooltipX = tooltipX;
    ui.skillTooltip = index;
    ui.skillTooltipY = tooltipY;
    ui.mouseOnUi = 1;
    if (!runtime.mouse.pendingClick)
        return;
    runtime.mouse.pendingClick = false;
    context.world.Hero().skillIndex = index;
    ui.skillListOpen = 0;
    runtime.RequestSound(ButtonSound);
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = 6;
}

// 0x420e90: with the raven (+0x430), the four buttons of its orders (skills 0x78 to 0x7b), 0x20 apart from x 0x100,
// with y from 0x14a to 0x16e.
void StepThePetOrderButtons(const Ui::GameUiContext& context)
{
    const Kernel::ActorState& hero = context.world.Hero();
    if (!hero.darkSpirit)
        return;
    constexpr std::uint8_t FirstOrder = 0x78;
    constexpr std::uint8_t OrderCount = 4;
    constexpr std::int32_t Width = 0x20;
    for (std::uint8_t order = 0; order < OrderCount; ++order)
    {
        const std::int32_t x = 0x100 + Width * order;
        if (InRect(context, x, 0x14a, x + Width, 0x16e))
            PointAtTheSkill(context, static_cast<std::uint8_t>(FirstOrder + order), x + 0x10, 0x14a);
    }
}

// The Ctrl + number keys on the list's skill `index` (0x5a08fd..0x5a106e): for each number down, 1 to 9 and 0, with
// the own record open, it takes that number off the 64 keys of the chosen one's slot (0x6d5edc) and gives it to the
// skill.
void StepTheSkillKeys(const Ui::GameUiContext& context, std::uint8_t index)
{
    const Input::InputState& input = context.input;
    if (!input.IsDown(Key::Ctrl))
        return;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::OwnCharacterRecord& own = context.world.OwnCharacter();
    const std::size_t place = context.chosenSlot.value_or(std::uint8_t{0});
    constexpr std::size_t KeyedSlots = 0x40;
    constexpr std::uint8_t NoKey = 0xff;
    for (int digit = 1; digit <= 10; ++digit)
    {
        const auto number = static_cast<std::uint8_t>(digit % 10);
        if (!input.IsDown(Digit(number).key) || place >= own.skillKeys.size())
            continue;
        const Models::LogicalAccess access(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        auto& keys = own.skillKeys[place];
        for (std::size_t slot = 0; slot < KeyedSlots; ++slot)
        {
            if (keys[slot] == number)
                keys[slot] = NoKey;
        }
        keys[index] = number;
    }
}

// 0x5a070e..0x5a1098: the open list. For each of the 64 skills there are, except 0x43 to 0x48 and those with
// Skill.bmd's +0x31 at 1 (with its entry opened and closed: 60 rand() if it closes it at 0), a 32 × 42 icon in rows of
// up to 20 centered at x 320 from y 0x172, the second 0x34 higher. Under the mouse, its tooltip and its click
// (PointAtTheSkill) and the keys (StepTheSkillKeys). Then, the raven order buttons (0x420e90).
Core::Expected<void, std::string> StepTheSkillList(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::OwnCharacterRecord& own = context.world.OwnCharacter();
    constexpr std::int32_t RowSize = 0x14;
    const std::int32_t width =
        std::min(static_cast<std::int32_t>(own.byte6a) - static_cast<std::int32_t>(own.byte6b), RowSize);
    std::int32_t shown = 0;
    std::int32_t column = 0;
    std::int32_t y = 0x172;
    constexpr std::uint8_t ListedSlots = 0x40;
    for (std::uint8_t index = 0; index < ListedSlots; ++index)
    {
        const std::uint8_t skill = Kernel::OwnSkillByte(own, index);
        if (skill == 0 || (skill >= 0x43 && skill <= 0x48))
            continue;
        if (context.skills == nullptr)
            return Core::Unexpected(std::string("0x5a07a3: la lista de skills lee Skill.bmd y MuRet no lo cargó"));
        const auto record = context.skills->Record(skill);
        if (!record)
            return Core::Unexpected(std::format("0x5a07a3: Skill.bmd no tiene la skill {}", skill));
        std::uint8_t hidden = 0;
        {
            const Models::LogicalAccess entry(runtime.skillDepths[skill], context.random,
                                              static_cast<std::uint16_t>(Tables::SkillRecordSize));
            constexpr std::size_t HiddenOffset = 0x31;
            hidden = std::to_integer<std::uint8_t>((*record)[HiddenOffset]);
        }
        if (hidden == 1)
            continue;
        if (shown == RowSize)
        {
            y -= 0x34;
            column = 0;
        }
        // 0x5a084f..0x5a085f: the CDQ/SAR divides toward 0.
        const std::int32_t x = (column + 10) * 32 - width * 32 / 2;
        ++shown;
        ++column;
        if (!InRect(context, x, y, x + 0x20, y + 0x2a))
            continue;
        PointAtTheSkill(context, index, x + 0x10, y);
        StepTheSkillKeys(context, index);
    }
    StepThePetOrderButtons(context);
    return {};
}

// 0x420e30: the Dark Lord with Shift and 1 to 4 chooses the raven's order (skills 0x78 to 0x7b), the first one down.
// Says whether it chose.
bool StepThePetOrderKeys(const Ui::GameUiContext& context)
{
    Kernel::ActorState& hero = context.world.Hero();
    if (Kernel::BaseClassOf(hero.characterClass) != Kernel::CharacterClass::DarkLord ||
        !context.input.IsDown(Key::Shift))
        return false;
    for (int order = 0; order < 4; ++order)
    {
        if (context.input.IsDown(Digit(order + 1).key))
        {
            hero.skillIndex = static_cast<std::uint8_t>(0x78 + order);
            return true;
        }
    }
    return false;
}

// 0x59f810(number): with the own record open, the first of the 64 skills with that key in the chosen one's slot that
// is not one of 0x43 to 0x48 becomes the hero's; if none, the first of the raven's orders (0x78 to 0x7b) with the key.
// On each pass without choosing, with auto attack outside maps 6 and 18 to 23 and the hero's skill at 6 or 0xf, the
// chosen 0x6c8e30 and 0x6c8e38 go back to −1. Says whether it chose.
bool ChooseTheSkillByKey(const Ui::GameUiContext& context, std::uint8_t number)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::OwnCharacterRecord& own = context.world.OwnCharacter();
    Kernel::ActorState& hero = context.world.Hero();
    const Models::LogicalAccess access(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const std::size_t place = context.chosenSlot.value_or(std::uint8_t{0});
    if (place >= own.skillKeys.size())
        return false;
    const auto& keys = own.skillKeys[place];
    constexpr std::uint8_t KeyedSlots = 0x40;
    for (std::uint8_t slot = 0; slot < KeyedSlots; ++slot)
    {
        const std::uint8_t skill = Kernel::OwnSkillByte(own, slot);
        if (skill != 0 && keys[slot] == number && (skill < 0x43 || skill > 0x48))
        {
            hero.skillIndex = slot;
            return true;
        }
        const std::int32_t map = runtime.map;
        if (runtime.ui.autoAttack && map != 6 && !(map >= 0x12 && map <= 0x17))
        {
            const std::uint8_t chosen = Kernel::OwnSkillByte(own, hero.skillIndex);
            if (chosen == 6 || chosen == 0xf)
            {
                runtime.selectedTargets[2] = -1;
                runtime.selectedTargets[4] = -1;
            }
        }
    }
    for (std::uint8_t slot = 0x78; slot < 0x7c; ++slot)
    {
        if (keys[slot] == number)
        {
            hero.skillIndex = slot;
            return true;
        }
    }
    return false;
}

// 0x5a05e5..0x5a10e2: the skill bar. Opens the own record even with no skills and closes it at the end.
Core::Expected<void, std::string> StepTheSkillBar(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    if (context.world.OwnCharacter().byte6a == 0)
        return {};
    // 0x5a066c..0x5a06f9: the bar's button.
    if (InRect(context, 0x130, 0x1bc, 0x150, 0x1e0))
    {
        ui.skillTooltipX = 0x140;
        ui.skillTooltip = context.world.Hero().skillIndex;
        ui.skillTooltipY = 0x1bc;
        ui.mouseOnUi = 1;
        runtime.mouse.updateTime = 0;
        if (runtime.mouse.pendingClick)
        {
            runtime.mouse.pendingClick = false;
            ui.skillListOpen = ui.skillListOpen == 0 ? 1 : 0;
            runtime.RequestSound(ButtonSound);
        }
    }
    // 0x652330: without the siege (0x4294b0) it does nothing.
    if (auto siege = CastleSiege::StepTheBattleSkill(context); !siege.HasValue())
        return siege;
    if (ui.skillListOpen != 0)
        return StepTheSkillList(context);
    return {};
}

// 0x59ff45..0x5a009b: the right click on a drawn row of the old area (0x77d9868, from 0x6c8eb4) (with text and,
// without F2, not of the normal chat), someone else's, with the chat open and the hero in the safe zone: their name to
// the whisper buffer, without the EDIT (Q-C530), and the 0x19. The row's width is the one its draw left, in pixels.
void StepTheOldAreaRows(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    const Kernel::ActorState& hero = context.world.Hero();
    const Ui::MessageAreaState& area = runtime.chat.area;
    for (std::int32_t y = 0, shown = 0; y < OldAreaBottom; y += OldAreaRowHeight, ++shown)
    {
        const std::int32_t index = area.firstShown + shown;
        if (index < 0 || index >= static_cast<std::int32_t>(Ui::MessageAreaRows))
            continue;
        const Ui::MessageAreaRow& row = area.rows[static_cast<std::size_t>(index)];
        if (row.text.empty() || (!ui.chatModes.showChat && row.type == Ui::ChatMessage))
            continue;
        const std::uint32_t width =
            static_cast<std::uint32_t>(runtime.hud.messageFrame.widths[static_cast<std::size_t>(index)]);
        const std::uint32_t right =
            context.screen.width > 0 ? width * 640u / static_cast<std::uint32_t>(context.screen.width) : width;
        if (context.mouseX < 0 || context.mouseX >= static_cast<std::int32_t>(right) || context.mouseY < y ||
            context.mouseY >= y + OldAreaRowHeight || !ui.chatOpen || hero.safeZone == 0)
            continue;
        if (row.name == Actors::HeroNameOf(hero) || !ui.rightClick)
            continue;
        ui.rightClick = false;
        Chat::PutTheWhisperName(runtime.chat.input, row.name);
        runtime.RequestSound(ButtonSound);
    }
}

// 0x5a0124..0x5a03c3: up (−1) and down (+1). With the focus on one of the chat EDITs and the even field, the history
// (the vtable + 0xc of 0x57634f8); otherwise, the whisper name of the previous or next place (0x6c8ea8).
void StepTheArrowKey(const Ui::GameUiContext& context, GameKey key, std::int32_t direction)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Chat::ChatInputState& input = runtime.chat.input;
    if (!KeyEdge(runtime.ui, context.input, key))
        return;
    bool sound = false;
    if ((input.focus == Chat::ChatFocus::Message || input.focus == Chat::ChatFocus::Whisper) && input.field % 2 == 0)
        sound = Chat::RecallFromTheHistory(input, direction);
    else
        sound = Chat::StepTheWhisperName(input, direction, &runtime.ui.focusedList);
    if (sound)
        runtime.RequestSound(ButtonSound);
}

// 0x45aa80(0) and 0x468d50 on a chat EDIT: the mouse over it makes it the window under the mouse (if nobody grabbed
// the mouse or it grabbed it) and, inside (x, y − 4, w, h + 8), it belongs to the UI; with the click, the EDIT takes
// the focus (0x4682a0(1)). The arrows and the bar that 0x468d50 checks with the click (EM_SCROLL, the grabbed bar) are
// not ported (P-C86). Returns what 0x468d50 does: the mouse in the big rectangle.
bool StepTheChatEdit(const Ui::GameUiContext& context, const ChatEditPlace& edit)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    if (InRect(context, edit.x, edit.y, edit.x + edit.width, edit.y + edit.height))
    {
        if (ui.hovered == 0)
        {
            if (ui.capture != 0 && ui.capture != edit.id)
                return false;
            ui.hovered = edit.id;
        }
        // +0x48 at 0, visible. With the left button down, the old windows' list (0x490aa0, milestone 7): the chat EDITs
        // have no parent.
        if (ui.hovered != edit.id && ui.capture != edit.id)
            return false;
    }
    if (!InRect(context, edit.x, edit.y - 4, edit.x + edit.width, edit.y + edit.height + 4))
        return false;
    ui.mouseOnUi = 1;
    if (runtime.mouse.pendingClick)
    {
        Chat::ClickTheChatField(runtime.chat.input, edit.field, &ui.focusedList);
        runtime.mouse.updateTime = 0;
        runtime.mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    }
    return true;
}

// 0x5a32dd..0x5a343c: with a text field, the chat EDITs (0x469500: the one under the mouse is the chosen field) and
// the F3 button (0x6c8dd0).
void StepTheChatButtons(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    for (const ChatEditPlace& edit : ChatEdits)
    {
        if (StepTheChatEdit(context, edit))
            runtime.chat.input.field = edit.field;
    }
    if (!InRect(context, WhisperModeLeft, WhisperModeTop, WhisperModeRight, WhisperModeBottom) ||
        !runtime.mouse.pendingClick)
        return;
    runtime.mouse.pendingClick = false;
    runtime.chat.input.whisperMode = !runtime.chat.input.whisperMode;
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    runtime.RequestSound(ButtonSound);
}

// 0x5a116f..0x5a2b47: with the macro brake at 0 (0x77f3b74), outside Chaos Castle and with Alt, the macros of the
// pressed numbers (GetAsyncKeyState, not the edge).
Core::Expected<void, std::string> StepTheMacros(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.hud.macroTime != 0 || (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap) ||
        !context.input.IsDown(Key::Alt))
        return {};
    std::array<bool, Chat::MacroCount> pressed{};
    bool any = false;
    for (std::size_t macro = 0; macro < Chat::MacroCount; ++macro)
    {
        pressed[macro] = context.input.IsDown(Digit(static_cast<int>((macro + 1) % Chat::MacroCount)).key);
        any = any || pressed[macro];
    }
    if (!any)
        return {};
    if (context.chat == nullptr)
        return Core::Unexpected(std::string("0x5a11cb: las macros sin la entrada del chat"));
    return Chat::SayTheMacros(*context.chat, pressed);
}

// 0x59ff10
Core::Expected<void, std::string> StepTheKeysAndButtons(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    Kernel::MouseState& mouse = runtime.mouse;
    const Input::InputState& input = context.input;
    const Kernel::ActorState& hero = context.world.Hero();
    // 0x59ff32..0x59ff3f: with the camera tour (0x4033d0), nothing.
    if (Events::Crywolf::CameraTourRunning(runtime.events.cameraTour))
        return {};
    StepTheOldAreaRows(context);
    // 0x5a00a1..0x5a0124: PgUp and PgDn scroll the chat log as many rows as it shows (the vtable + 0x38 gives them and
    // + 0x34 scrolls).
    if (KeyEdge(ui, input, PageUpKey))
        Chat::ScrollTheLog(runtime.chat.log, ui.chatModes, -runtime.chat.log.rows);
    if (KeyEdge(ui, input, PageDownKey))
        Chat::ScrollTheLog(runtime.chat.log, ui.chatModes, runtime.chat.log.rows);
    StepTheArrowKey(context, UpKey, -1);
    StepTheArrowKey(context, DownKey, 1);
    // 0x5a03c3..0x5a05e5: the HUD buttons, each with its rectangle: closes it if open, otherwise opens it.
    if (mouse.pendingClick)
    {
        struct HudButton
        {
            Ui::PanelId id;
            std::int32_t left, top, right, bottom;
        };
        constexpr std::array<HudButton, 6> Buttons{{
            {Ui::GuildPanel, 0x246, 0x1cb, 0x27a, 0x1dd},
            {Ui::PartyPanel, 0x15c, 0x1c4, 0x174, 0x1dc},
            {Ui::CharacterPanel, 0x17b, 0x1c4, 0x193, 0x1dc},
            {Ui::InventoryPanel, 0x19a, 0x1c4, 0x1b2, 0x1dc},
            {Ui::FriendsPanel, 0x246, 0x1b3, 0x27a, 0x1c5},
            {Ui::CommandsPanel, 6, 0x1b2, 0x3b, 0x1c5},
        }};
        for (const HudButton& button : Buttons)
        {
            if (!InRect(context, button.left, button.top, button.right, button.bottom))
                continue;
            mouse.pendingClick = false;
            if (auto toggled = Toggle(panels, button.id); !toggled.HasValue())
                return toggled;
        }
    }
    if (auto bar = StepTheSkillBar(context); !bar.HasValue())
        return bar;
    // 0x5a10e7..0x5a116f: the numbers choose the skill (0x59f810), without the 0x73 dialog (0x5a10f0, with 0x6c7b8c at
    // 1: it does not even check the focus), with the focus on the game, without text fields, without a skill charging
    // (0x5a1115, 0x7d368a4 at 0), without the raven's orders (0x420e30, Shift with 1 to 4) and without Alt.
    if (runtime.dialogs.current != MoneyDialog && runtime.chat.input.focus == Chat::ChatFocus::Game && !ui.chatOpen &&
        !runtime.chat.input.otherInput && ui.rightHoldFrames == 0)
    {
        if (!StepThePetOrderKeys(context) && !input.IsDown(Key::Alt))
        {
            // 0x5a1139..0x5a116a: 1 to 9 and then 0, each one that is down.
            for (int digit = 1; digit <= 10; ++digit)
            {
                if (input.IsDown(Digit(digit % 10).key))
                    static_cast<void>(ChooseTheSkillByKey(context, static_cast<std::uint8_t>(digit % 10)));
            }
        }
    }
    if (auto macros = StepTheMacros(context); !macros.HasValue())
        return macros;
    // 0x5a2b47..0x5a32dd: the right click on the three quick slots, if the hero is not dying. A slot without its item
    // in the inventory (0x558110 gives −1) goes on with the next.
    const std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells(context.world.Inventory().cells);
    if (hero.dying == 0 && ui.rightClick)
    {
        for (std::int32_t slot = 0; slot < 3; ++slot)
        {
            const std::int32_t left = 0xd0 + 0x1f * slot;
            if (!ui.rightClick || !InRect(context, left, 0x1c4, left + 0x14, 0x1d8))
                continue;
            ui.rightClick = false;
            auto found =
                Items::FindQuickSlotItem(runtime.hud.quickSlots, cells, slot, runtime.movement.gateRequestBlocked != 0);
            if (!found.HasValue())
                return Core::Unexpected(found.Error());
            // 0x5a2bc8..0x5a32cc: use it (CL9 milestone 5) and leave the slots.
            if (found.Value())
            {
                if (auto used =
                        Items::UseInventoryItem(MovesOf(context), panels, static_cast<std::int32_t>(*found.Value()));
                    !used.HasValue())
                    return used;
                break;
            }
        }
    }
    if (TextInputBusy(runtime))
        StepTheChatButtons(context);
    // 0x5a343e..0x5a359a: Q, W and E only without a text field, with all this closed and the hero alive. The trade and
    // NPC shop queries take one rand() each on every step. 0x77f3b68, between 0x40000 and the dialog (0x5a34c3), was
    // already checked by 0x6578e0, the first.
    if (!TextInputBusy(runtime) && !panels.IsOpen(Ui::TradePanel) && !panels.IsOpen(Ui::WarehousePanel) &&
        !panels.IsOpen(Ui::NpcShopPanel) && !panels.IsOpen(Ui::ChaosBoxPanel) && !panels.IsOpen(Ui::SecondGuildPanel) &&
        runtime.dialogs.current == 0 && runtime.panels.field78abb00 == 0 && !panels.IsOpen(Ui::QuestPanel) &&
        !panels.IsOpen(Ui::EventNpcPanel) && !panels.IsOpen(Ui::SecondEventNpcPanel) && !panels.IsOpen(Ui::Panel2000) &&
        !panels.IsOpen(Ui::PersonalShopPanel) && !panels.IsOpen(Ui::OtherShopPanel) &&
        !panels.IsOpen(Ui::TrainerPanel) && hero.dying == 0)
    {
        // 0x5a35a6, 0x5a3d2a and 0x5a44b1: slot 0, 1 and 2. Without its item in the inventory, the next key.
        constexpr std::array<char, 3> QuickSlotLetters{'Q', 'W', 'E'};
        for (int slot = 0; slot < 3; ++slot)
        {
            const char letter = QuickSlotLetters[static_cast<std::size_t>(slot)];
            if (!KeyEdge(ui, input, Letter(letter)))
                continue;
            auto found =
                Items::FindQuickSlotItem(runtime.hud.quickSlots, cells, slot, runtime.movement.gateRequestBlocked != 0);
            if (!found.HasValue())
                return Core::Unexpected(found.Error());
            // 0x5a35e0.., 0x5a3d64.. and 0x5a44eb..: use it (CL9 milestone 5); the next key is read the same.
            if (found.Value())
            {
                if (auto used =
                        Items::UseInventoryItem(MovesOf(context), panels, static_cast<std::int32_t>(*found.Value()));
                    !used.HasValue())
                    return used;
            }
        }
    }
    // 0x5a4c6e..0x5a5504: the panel letters, B and R, without a text field, without a dialog and with the CS list
    // closed. The first one pressed does its thing and the following ones are not read on this step.
    if (!TextInputBusy(runtime) && runtime.dialogs.current == 0 && runtime.panels.delegated.castleSiegeList == 0)
    {
        struct PanelLetter
        {
            char letter;
            Ui::PanelId id;
        };
        constexpr std::array<PanelLetter, 9> Letters{{
            {'G', Ui::GuildPanel},
            {'P', Ui::PartyPanel},
            {'T', Ui::QuestPanel},
            {'C', Ui::CharacterPanel},
            {'I', Ui::InventoryPanel},
            {'V', Ui::InventoryPanel},
            {'F', Ui::FriendsPanel},
            {'M', Ui::MovePanel},
            {'D', Ui::CommandsPanel},
        }};
        bool done = false;
        for (const PanelLetter& letter : Letters)
        {
            if (!KeyEdge(ui, input, Letter(letter.letter)))
                continue;
            // 0x5a4ea6: the common tail.
            if (auto toggled = Toggle(panels, letter.id); !toggled.HasValue())
                return toggled;
            done = true;
            break;
        }
        Ui::PanelState& state = runtime.panels;
        // 0x5a4ec2..0x5a4f2e: B, with the NPC shop open and 0x78abb09, the shop's repair mode.
        if (!done && KeyEdge(ui, input, Letter('B')))
        {
            done = true;
            if (panels.IsOpen(Ui::NpcShopPanel) && state.field78abb09 != 0 && runtime.cursorItemCount == 0)
            {
                if (state.repairCursor == 0)
                {
                    state.repairCursor = 2;
                    state.repairMode = 1;
                }
                else
                {
                    state.repairCursor = 0;
                    state.repairMode = 0;
                }
            }
        }
        // 0x5a4f34..0x5a54e6: R, the repair mode with the inventory (level 50 or more).
        if (!done && KeyEdge(ui, input, Letter('R')))
        {
            std::uint16_t level = 0;
            {
                const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
                level = context.world.OwnCharacter().level;
            }
            constexpr std::uint16_t RepairLevel = 0x32;
            bool toggle = panels.IsOpen(Ui::NpcShopPanel) && state.field78abb09 != 0 && runtime.cursorItemCount == 0;
            if (!toggle && !panels.IsOpen(Ui::TradePanel) && !panels.IsOpen(Ui::NpcShopPanel) &&
                panels.IsOpen(Ui::InventoryPanel) && level >= RepairLevel && !panels.IsOpen(Ui::WarehousePanel) &&
                !panels.IsOpen(Ui::ChaosBoxPanel) && !panels.IsOpen(Ui::EventNpcPanel) && state.field78abb00 == 0)
                toggle = true;
            if (toggle)
            {
                // 0x5a54c8..0x5a54e1.
                state.repairMode = state.repairMode == 0 ? 1 : 0;
                state.repairCursor = state.repairMode != 0 ? 2 : 0;
            }
        }
    }
    // 0x5a5504: 0x6529e0, the siege with the wheel or the mouse in its rectangle; without the siege (0x4294b0) it does
    // nothing.
    if (auto siege = CastleSiege::StepTheBattleSkillChoice(context); !siege.HasValue())
        return siege;
    // 0x5a5511: with no panel open, the siege panel's step (0x4125a0). 0x413c90 does nothing.
    if (!panels.IsOpen(Ui::AnyPanel))
        return CastleSiege::StepTheBattlePanel(context);
    return {};
}

} // namespace

bool GameTextInputBusy(const Kernel::WorldRuntime& runtime)
{
    return TextInputBusy(runtime);
}

bool OldWindowKeyEdge(Ui::GameUiState& ui, const Input::InputState& input, std::uint8_t virtualKey)
{
    for (const GameKey& key : OldWindowEdgeKeys)
    {
        if (key.virtualKey == virtualKey)
            return KeyEdge(ui, input, key);
    }
    return false;
}

// 0x4b3961: a new DOWN re-arms held even if the UI had consumed it and UP+DOWN arrive in the same pump.
void LendTheGameHooks(Ui::OldWindowStepView& view, const Ui::GameUiContext& context, bool effects)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    view.keyDown = ArrowKeysDown(context.input);
    // CL9 milestone 10: the bad words filter of the letters and of the chat with a friend (0x622b40 with 0).
    if (const Chat::GameChatContext* chatContext = context.chat; chatContext != nullptr)
        view.badWord = [filter = chatContext->filter](std::string_view text)
        { return FrontEnd::ContainsAFilteredWord(text, filter, false); };
    view.heroName = std::string(Actors::HeroNameOf(world.Hero()));
    view.field78abaac = runtime.ui.field78abaac;
    if (!effects)
    {
        view.whisperTo = [](std::string_view /*name*/) {};
        view.replayLetter = [](const std::vector<std::byte>& /*copy*/) -> Core::Expected<void, std::string>
        { return {}; };
        view.portrait = [](Ui::OldPortraitPart& /*part*/, Ui::PortraitCallKind /*call*/,
                           std::int32_t /*delta*/) -> Core::Expected<void, std::string> { return {}; };
        return;
    }
    // 0x4694c0(0, 0, 1, nombre).
    view.whisperTo = [&chat = runtime.chat.input](std::string_view name)
    { Chat::SetTheChatFields(chat, std::nullopt, name); };
    // 0x4a11b0 with 10 and 2 on a saved letter: 0x4dcb70 with 0x4a03c0's copy.
    view.replayLetter = [&world, &random = context.random, measure = context.measure, screen = context.screen,
                         texts = context.texts, chosenSlot = context.chosenSlot, worldEntry = context.worldEntry](
                            const std::vector<std::byte>& copy) -> Core::Expected<void, std::string>
    {
        auto letter = Protocol::Messenger::ReadLetterCopy(copy);
        if (!letter.HasValue())
            return Core::Unexpected(std::string("0x4a11b0(2): la carta guardada no se lee"));
        const Chat::ChatReceiveContext receive{.world = world,
                                               .random = random,
                                               .measure = measure,
                                               .screen = screen,
                                               .scene = std::nullopt,
                                               .texts = texts,
                                               .chosenSlot = chosenSlot,
                                               .worldEntry = worldEntry};
        return Messenger::ApplyLetterRead(letter.Value(), receive);
    };
    // CL9 milestone 10: the character of the letters' portrait (Messenger/LetterPortrait), which its step reads.
    if (context.worldEntry != nullptr)
        view.portrait = Messenger::LetterPortraitHook(*context.worldEntry);
}

Ui::OldWindowGlobals OldWindowGlobalsOf(Kernel::WorldRuntime& runtime)
{
    Ui::GameUiState& ui = runtime.ui;
    Kernel::MouseState& mouse = runtime.mouse;
    return Ui::OldWindowGlobals{.leftHeld = mouse.leftHeld,
                                .leftClick = mouse.pendingClick,
                                .releasedClick = mouse.releasedClick,
                                .mouseOnUi = ui.mouseOnUi,
                                .hovered = ui.hovered,
                                .capture = ui.capture,
                                .pressedButton = ui.pressedButton,
                                .keyboardFocus = runtime.chat.input.focus,
                                .sound = runtime.lastSoundId,
                                .focusedList = ui.focusedList,
                                .wheel = ui.wheel,
                                .rightClick = ui.rightClick,
                                .doubleClick = ui.doubleClick,
                                .mouseUpdateTime = &mouse.updateTime,
                                .mouseUpdateTimeMax = &mouse.updateTimeMax,
                                .soundRequest = [&runtime](Audio::SoundId sound) { runtime.RequestSound(sound); }};
}

Core::Expected<void, std::string> StepTheGameDialogs(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    Ui::DialogState& dialogs = runtime.dialogs;
    GamePanels panels(context.world, context.random, GamePanelTables{.texts = context.texts});
    // Esc (0x623f17 → 0x657880) closes the lowest open window: the trade sends the 3D, another's shop the 3F 07 and the
    // quests the 31, through the session.
    LendTheUiPanelCloses(panels, context);
    // 0x623e9b..0x623ec2: with the friends one open, Esc belongs to the old windows (0x490490). What was read stays at
    // [EBP−0x7d], which 0x62c50e's No checks.
    const bool escape = !panels.Registry().IsOpen(Ui::FriendsPanel) && KeyEdge(ui, context.input, EscapeKey);
    if (escape)
    {
        // 0x623ec8..0x623ee9: with the camera tour (0x4033d0), Esc cuts it (+0x158 = 1 and 0x403d60) and returns.
        if (Events::Crywolf::CameraTourRunning(runtime.events.cameraTour))
        {
            runtime.events.cameraTour.skipped = true;
            Events::Crywolf::RemoveTheCameraTourActors(context.world, runtime.events.cameraTour);
            return {};
        }
        if (ui.field78abb39 == 0)
        {
            if (dialogs.current == 0)
            {
                auto closed = panels.Registry().PanelKey(0x1b);
                if (!closed.HasValue())
                    return Core::Unexpected(std::move(closed).Error());
                if (!closed.Value())
                {
                    // 0x623f22..0x623f33: the menu, or pending if something opened a dialog.
                    (dialogs.current != 0 ? dialogs.pending : dialogs.current) = Ui::EscapeMenuDialog;
                    // 0x623f42: with an item on the cursor, puts it back in its place.
                    if (auto returned = Items::ReturnTheCursorItem(MovesOf(context)); !returned.HasValue())
                        return returned;
                }
            }
            else if (dialogs.current == Ui::EscapeMenuDialog || dialogs.current == Ui::OptionsDialog)
            {
                dialogs.current = dialogs.pending;
                dialogs.pending = 0;
            }
            // 0x623f6a: sounds with any dialog.
            runtime.RequestSound(ButtonSound);
        }
    }
    // 0x623f78: the open dialog, through its branch.
    return Hud::Dialogs::StepTheOpenDialog(context, escape);
}

Core::Expected<void, std::string> StepTheGameUi(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    // 0x636df2..0x636e04.
    ui.hoveredItem = {};
    ui.skillTooltip = -1;
    ui.mouseOnUi = 0;
    // 0x636e0b: nobody writes 0x7d36899.
    if (context.mouseY >= HudTop)
        ui.mouseOnUi = 1;
    const OldWindowSight sight(context);
    GamePanels gamePanels(context.world, context.random,
                          GamePanelTables{.items = context.inventory != nullptr ? context.inventory->items : nullptr,
                                          .itemSetTables = context.itemSetTables,
                                          .texts = context.texts});
    gamePanels.LendThePartyList(context.world, context.requests);
    LendTheUiPanelCloses(gamePanels, context);
    if (context.chat != nullptr && context.chat->send)
        gamePanels.LendThePetRequests(context.chat->send);
    gamePanels.LendTheCastleWindowSteps(context);
    gamePanels.LendThePanelDialogStep(context.world, GuildSenderOf(context.requests), context.mouseX, context.mouseY,
                                      context.systemTime);
    gamePanels.LendTheSecondGuildStep(Guild::SecondGuildStepContext{
        .world = context.world,
        .mouseX = context.mouseX,
        .mouseY = context.mouseY,
        .now = context.systemTime,
        .screen = context.screen,
        .measure = context.measure,
        .texts = context.texts,
        .filterName = context.chat != nullptr ? context.chat->filterName : std::span<const std::string>{},
        .filter = context.chat != nullptr ? context.chat->filter : std::span<const std::string>{},
        .send = GuildSenderOf(context.requests)});
    gamePanels.LendTheGuildWindowStep(Guild::GuildWindowStepContext{.world = context.world,
                                                                    .mouseX = context.mouseX,
                                                                    .mouseY = context.mouseY,
                                                                    .screen = context.screen,
                                                                    .send = GuildSenderOf(context.requests),
                                                                    .keyEdge = PageKeyEdges(runtime.ui, context.input),
                                                                    .keyDown = ArrowKeysDown(context.input),
                                                                    .now = context.systemTime,
                                                                    .measure = context.measure,
                                                                    .texts = context.texts});
    gamePanels.LendTheFriends([&sight]() { return sight.OpenTheFriendsWindow(); });
    Ui::PanelRegistry& panels = gamePanels.Registry();
    // 0x636e24: 0x5ecdc0, the /move window, with nobody holding the mouse (0x5ecdd1).
    if (panels.IsOpen(Ui::MovePanel) && ui.capture == 0)
    {
        if (auto moved = StepTheMoveWindow(context); !moved.HasValue())
            return moved;
    }
    ui.hovered = 0;
    // 0x636e35: 0x46b7b0, the two queues of the Slide.bmd banner (0x46add0).
    if (auto slides = Ui::StepTheSlides(runtime.hud.slides, Ui::SlideStepContext{.measure = context.measure,
                                                                                 .dcFont = runtime.hud.dcFont,
                                                                                 .screen = context.screen,
                                                                                 .now = context.systemTime});
        !slides.HasValue())
        return slides;
    if (auto old = StepTheOldWindows(context, sight); !old.HasValue())
        return old;
    if (auto chat = StepTheChat(context); !chat.HasValue())
        return chat;
    StepTheUnownedClick(runtime);
    // 0x636ed6: 0x5ebca0, does nothing with 0x78abad9 at 0.
    if (auto duel = Social::StepTheDuelRequest(context); !duel.HasValue())
        return duel;
    // 0x636edb.
    if (auto crywolf = StepTheCrywolfInterface(context); !crywolf.HasValue())
        return crywolf;
    if (auto stepped = StepThePanels(context, panels); !stepped.HasValue())
        return stepped;
    if (auto keys = StepTheKeysAndButtons(context, panels); !keys.HasValue())
        return keys;
    // 0x5a7cc0: the help rectangle of the F3 24 (0x5767920), which nobody turns on: it does not consume the click.
    if (runtime.dialogs.current != Ui::EscapeMenuDialog)
    {
        if (auto objects = panels.StepTheObjectPanels(); !objects.HasValue())
            return objects;
    }
    PlaceTheChatLog(runtime, panels);
    // 0x63705b.
    if (runtime.dialogs.current != 0)
        ui.mouseOnUi = 1;
    return {};
}

Core::Expected<void, std::string> StepTheFunctionKeys(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    const Input::InputState& input = context.input;
    // 0x5ac8d9..0x5ac9ce: without an item on the cursor (0x7879fbc), the space bar with the chat closed and the mouse
    // off the UI (0x775857c) picks up the first live, visible floor item within 300 of the hero (0x5650c0, CL9
    // milestone 5).
    if (runtime.cursorItemCount == 0 && KeyEdge(ui, input, SpaceKey) && !ui.chatOpen && ui.mouseOnUi == 0)
    {
        if (auto picked = Items::PickUpTheNearestFloorItem(MovesOf(context), Terrain::TerrainOrEmpty(context.terrain),
                                                           context.fixedHeight);
            !picked.HasValue())
            return picked;
    }
    // 0x5ac9ce..0x5aca42: F1 cycles the helps (0x77f3b08): from 3 or 4 (the item's or the /move window) to 0 and
    // otherwise to the next of 0, 1 and 2. Turns off the set label (+0x9305 of [0x6eed3c]) and asks for the 0x19.
    if (KeyEdge(ui, input, F1Key))
    {
        std::int32_t& mode = runtime.panels.overlayMode;
        mode = mode == Ui::ItemHelpMode || mode == Ui::MoveOverlayMode ? 0 : (mode + 1) % FunctionHelpModes;
        context.world.ItemSets().byte9305 = 0;
        runtime.RequestSound(ButtonSound);
    }
    // 0x5aca42..0x5aca86: F2, the normal chat in the log (0x6c8dd1), and the log scrolls 0 (the vtable + 0x34).
    if (KeyEdge(ui, input, F2Key))
    {
        ui.chatModes.showChat = !ui.chatModes.showChat;
        runtime.RequestSound(ButtonSound);
        Chat::ScrollTheLog(runtime.chat.log, ui.chatModes, 0);
    }
    // 0x5aca86..0x5acad0: F3, the whisper mode (0x6c8dd0).
    if (KeyEdge(ui, input, F3Key))
    {
        runtime.chat.input.whisperMode = !runtime.chat.input.whisperMode;
        runtime.RequestSound(ButtonSound);
    }
    // 0x5acad0..0x5acbb0: F4, the log's size (0x45d8c0) and its corner (0x45a9d0): in its window, 0xba except with the
    // inventory and the character, the trade, the warehouse, the chaos box, the events NPC or the NPC shop (each query
    // in its order, up to the first open one), 0; outside it, (−10, 0x51).
    if (KeyEdge(ui, input, F4Key))
    {
        Chat::CycleTheLogSize(runtime.chat.log, ui.chatModes);
        Chat::ChatLogFrame& frame = runtime.hud.chatFrame;
        if (ui.chatModes.placement == 1)
        {
            GamePanels gamePanels(context.world, context.random);
            Ui::PanelRegistry& panels = gamePanels.Registry();
            const bool left = panels.IsOpen(Ui::InventoryPanel) &&
                              (panels.IsOpen(Ui::CharacterPanel) || panels.IsOpen(Ui::TradePanel) ||
                               panels.IsOpen(Ui::WarehousePanel) || panels.IsOpen(Ui::ChaosBoxPanel) ||
                               panels.IsOpen(Ui::EventNpcPanel) || panels.IsOpen(Ui::NpcShopPanel));
            frame.x = left ? 0 : 0xba;
            frame.y = 0x1a4;
        }
        else
        {
            frame.x = -10;
            frame.y = 0x51;
        }
        runtime.RequestSound(ButtonSound);
    }
    return {};
}

namespace
{
constexpr std::int32_t NoTarget = -1;

// 0x59ee4b..0x59ef10: the previous chosen (0x6c8e30) stays only with auto attack (0x6c8e3c), outside map 6 and Chaos
// Castle, between 0 and 400, and if it is a monster (+0x88 at 2) that is not dying (+0x389). If it stays, with the
// other chosen (0x6c8e38) at −1, a mouse button (0x7d36874, 0x7d366d4, 0x7d3685c or 0x7d36880) or the hero dying, it
// releases only 0x6c8e30.
void KeepTheChosen(const FramePickingContext& context)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    std::array<std::int32_t, 5>& targets = context.targets;
    constexpr std::int32_t LastRecord = 400;
    constexpr std::int32_t SpectatorMap = 6;
    const bool chaosCastle = runtime.map >= 18 && runtime.map <= 23;
    if (!runtime.ui.autoAttack || runtime.map == SpectatorMap || chaosCastle || targets[2] < 0 ||
        targets[2] > LastRecord)
    {
        targets[2] = NoTarget;
        targets[4] = NoTarget;
        return;
    }
    const auto actors = context.world.Actors();
    const Kernel::ActorState& chosen = actors[static_cast<std::size_t>(targets[2])];
    constexpr std::uint8_t MonsterKind = 2;
    if (chosen.dying != 0 || chosen.byte88 != MonsterKind)
    {
        targets[4] = NoTarget;
        targets[2] = NoTarget;
        return;
    }
    const bool mouse =
        runtime.mouse.leftHeld || runtime.mouse.pendingClick || runtime.ui.rightDown || runtime.ui.rightClick;
    if (targets[4] == NoTarget || mouse || context.world.Hero().dying != 0)
        targets[2] = NoTarget;
}

// 0x59efb9..0x59f48d: the order of the two character searches without Alt. The Dark Wizard and the Elf read their
// slot's skill (the hero's +0x425) in the own record, open; with 0x1a to 0x1c, mask 1's first.
std::array<std::uint8_t, 2> CharacterSearchOrder(const FramePickingContext& context)
{
    constexpr std::uint8_t Monsters = 0x22;
    constexpr std::uint8_t Players = 1;
    const Kernel::ActorState& hero = context.world.Hero();
    const Kernel::CharacterClass heroClass = Kernel::BaseClassOf(hero.characterClass);
    if (heroClass != Kernel::CharacterClass::DarkWizard && heroClass != Kernel::CharacterClass::Elf)
        return {Monsters, Players};
    std::uint8_t skill = 0;
    {
        const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        skill = Kernel::OwnSkillByte(context.world.OwnCharacter(), hero.skillIndex);
    }
    constexpr std::uint8_t FirstPlayersFirst = 0x1a;
    constexpr std::uint8_t LastPlayersFirst = 0x1c;
    if (skill >= FirstPlayersFirst && skill <= LastPlayersFirst)
        return {Players, Monsters};
    return {Monsters, Players};
}

// 0x59f52f.
void ForgetTheOther(std::array<std::int32_t, 5>& targets)
{
    if (targets[2] == NoTarget)
        targets[4] = NoTarget;
}
} // namespace

Core::Expected<void, std::string> PickInTheFrame(const FramePickingContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    std::array<std::int32_t, 5>& targets = context.targets;
    const auto pick = [&context](std::uint8_t mask)
    {
        return Actors::PickTheCharacter(context.world, Terrain::TerrainOrEmpty(context.terrain), mask,
                                        context.cameraPosition, context.ray, context.pickingBoxes);
    };
    // 0x59ec97..0x59ee48: nobody turns on the editor modes (0x77f3b18).
    KeepTheChosen(context);
    // 0x59ef16..0x59ef29.
    targets[0] = NoTarget;
    targets[1] = NoTarget;
    targets[3] = NoTarget;
    // 0x59ef2f → 0x59f52a: with the mouse on the UI, 0x59e3b0 (the party in the viewport). 0x59eb90 (the objects) is
    // not ported: it leaves −1.
    if (runtime.ui.mouseOnUi != 0)
    {
        Party::FindThePartyInTheViewport(context.world);
        ForgetTheOther(targets);
        return {};
    }
    // 0x59e2e0, the floor item search (CL9 milestone 5), without an item on the cursor (0x7879fbc: 0x59ef46 and
    // 0x59f4e3).
    const auto pickTheFloorItem = [&context, &runtime, &targets]
    {
        if (runtime.cursorItemCount == 0 && context.ray)
            targets[0] = Items::PickTheFloorItem(context.floorItems, *context.ray);
    };
    if (context.altDown)
    {
        // 0x59ef46..0x59efb4: the floor item, which jumps to 0x59f52a (with 0x59e3b0); without it, those of mask 4,
        // those of 0x22 and 1, and the objects (0x59eb90, which leave −1).
        pickTheFloorItem();
        if (targets[0] != NoTarget)
            Party::FindThePartyInTheViewport(context.world);
        else
        {
            targets[1] = pick(4);
            if (targets[1] == NoTarget)
            {
                targets[2] = pick(0x22);
                if (targets[2] != NoTarget)
                    return {};
                targets[2] = pick(1);
                if (targets[2] != NoTarget)
                    return {};
            }
        }
        ForgetTheOther(targets);
        return {};
    }
    const std::array<std::uint8_t, 2> order = CharacterSearchOrder(context);
    // 0x59f48d → 0x5e4140: with the party closed, the list on the right; if it chooses, 0x59e3b0 (0x59f494 → 0x59f52a).
    GamePanels panels(context.world, context.random);
    if (Party::PickFromThePartyList(context, panels.Registry()))
    {
        Party::FindThePartyInTheViewport(context.world);
        ForgetTheOther(targets);
        return {};
    }
    // 0x59f49a..0x59f509.
    if (targets[2] == NoTarget)
    {
        targets[2] = pick(order[0]);
        if (targets[2] == NoTarget)
        {
            targets[2] = pick(order[1]);
            if (targets[2] != NoTarget)
                return {};
            targets[1] = pick(4);
            // 0x59f4e3..0x59f509: without an NPC, the floor item and, without an item, the objects (0x59eb90), which
            // leave −1; both jump to 0x59f52f, without 0x59e3b0.
            if (targets[1] == NoTarget)
                pickTheFloorItem();
            ForgetTheOther(targets);
            return {};
        }
    }
    // 0x59f522..0x59f52a: with a chosen one and 0x6c8e38 set, 0x59e3b0.
    if (targets[4] != NoTarget)
        Party::FindThePartyInTheViewport(context.world);
    ForgetTheOther(targets);
    return {};
}
} // namespace Client::Hud
