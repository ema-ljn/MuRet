#include "Scenes/GameUi.h"
#include "Maps/MapId.h"
#include "Scenes/SiegeBattleUi.h"
#include "Scenes/CastleWindows.h"
#include "Scenes/CastleSiegeReceive.h"

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
#include "Scenes/ActorState.h"
#include "Scenes/ChaosMix.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/LetterPortrait.h"
#include "Scenes/CharacterPicking.h"
#include "Scenes/CameraTour.h"
#include "Scenes/CrywolfContract.h"
#include "Scenes/EventChipWindow.h"
#include "Scenes/EventNpcWindows.h"
#include "Scenes/FloorItemActions.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameCommandsWindow.h"
#include "Scenes/GameDialogSteps.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameLetters.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMoveWindow.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GamePartyWindow.h"
#include "Scenes/GameSocialSteps.h"
#include "Scenes/GridItemUse.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/NpcWindowSteps.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PartyActors.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/PetTrainer.h"
#include "Scenes/WarehouseKeypad.h"
#include "Scenes/PersonalShopWindow.h"
#include "Scenes/QuestWindow.h"
#include "Scenes/SocialState.h"
#include "Scenes/GuildWindow.h"
#include "Scenes/SecondGuildWindow.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/ChatLogWindow.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/NameChecks.h"
#include "UI/OldWindows.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"
#include "UI/SlideBanner.h"
#include "World/ItemState.h"

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

namespace Client::Scenes
{
namespace
{
using Input::Key;

// Una tecla de MuRet y su tecla virtual de Windows, la que el original pasa a GetAsyncKeyState y que indexa los
// latches de 0x77f36d4.
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

// 0x41ed10(0x19, 0, 0): el sonido de los botones y de Esc.
constexpr std::int32_t ButtonSound = 0x19;
// 0x5a10f0: el diálogo del zen, con el campo en el EDIT de [0x5763504].
constexpr std::int32_t MoneyDialog = 0x73;
// 0x5e455a y 0x5e4573: la ventana del personaje, 0xbe × 0x1b1 desde 0x78aba14 y 0x78aba18.
constexpr std::int32_t CharacterWindowWidth = 0xbe;
constexpr std::int32_t CharacterWindowHeight = 0x1b1;
// La clase 4, el Dark Lord: cinco botones de puntos (0x5e4598) y las órdenes de la mascota con Shift (0x420e30).
// 0x4246ea..0x424709: el botón de la mascota, fijo en la pantalla.
constexpr std::int32_t PetButtonLeft = 0x217;
constexpr std::int32_t PetButtonRight = 0x22f;
constexpr std::int32_t PetButtonTop = 0x18b;
constexpr std::int32_t PetButtonBottom = 0x1a3;
// 0x42474c: 0x723911 en 2, la ventana de la mascota.
constexpr std::uint8_t PetWindowOpen = 2;

// Los paquetes del gremio que manda la UI (CL9 hito 8) van por las peticiones de la escena; sin ellas, nada.
GuildSender GuildSenderOf(IGameUiRequests* requests)
{
    if (requests == nullptr)
        return {};
    return [requests](Protocol::Outgoing packet) { requests->SendGuildPacket(std::move(packet)); };
}

// El paso de diálogos y el de las ventanas tienen registros distintos; ambos necesitan los cierres completos.
void LendTheUiPanelCloses(GamePanels& panels, const GameUiContext& context)
{
    CastleSender send;
    if (context.requests != nullptr)
        send = [requests = context.requests](Protocol::Outgoing packet)
        { requests->SendGamePacket(std::move(packet)); };
    LendTheCastlePanelServices(
        panels, context.world, context.random, send, context.worldEntry,
        {.measure = context.measure, .texts = context.texts, .screen = context.screen, .now = context.systemTime});
    panels.LendTheNpcWindows(context.world, context.random,
                             context.inventory != nullptr ? *context.inventory : InventoryServices{},
                             context.sendNpcWindow);
    panels.LendTheGuild(context.world, GuildSenderOf(context.requests));
}

// 0x42475f..0x424990: la ventana de la mascota y sus botones (0x6b8a08, 0x6b8a04, 0x6b87bc, 0x6b8874, 0x6b8a00,
// 0x6b89f8, 0x6b89fc, 0x6b86d0, 0x6b8800, 0x6b8804 y 0x6b89f4).
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
// 0x5aca1b..0x5aca24: F1 da la vuelta por 0, 1 y 2 (idiv por 3).
constexpr std::int32_t FunctionHelpModes = 3;
// CL9 hito 5, el clic derecho de la grilla (0x5c58b0..0x5c740c): el pergamino del portal y la caja de la suerte de
// nivel 14 (+4 & 0x78 en 0x70). Los tres que mandan el C1 .. 91, en GridItemUse.
constexpr std::int32_t TownPortalScroll = 0x1c0a;
constexpr std::int32_t BoxOfLuck = 0x1c0b;
constexpr std::uint32_t UsedLevelBits = 0x78;
constexpr std::uint32_t UsedBoxLevelBits = 0x70;
// 0x636e11: desde esta y, el mouse está sobre la barra.
constexpr std::int32_t HudTop = 0x1b0;
// 0x5a1181..0x5a1189: Chaos Castle.
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
// 0x5a0420 y vecinas: 0x6c8dcc, el máximo de MouseUpdateTime que ponen los botones.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x59ff45..0x5a009b: los seis renglones del área vieja, de 13 de alto desde y = 0 (hasta 0x4e).
constexpr std::int32_t OldAreaRowHeight = 0xd;
constexpr std::int32_t OldAreaBottom = 0x4e;
// 0x4691e0: los dos EDIT del chat, en (0xc1, 0x1a6) de 0xb4 × 0xe y en (0x178, 0x1a6) de 0x32 × 0xe. Sus ids
// (0x45a210) son *inferidos*: los dos primeros que arma WinMain (0x4bb01b y 0x4bb02c), antes de los otros tres
// objetos de EDIT y del registro del chat (el 9).
struct ChatEditPlace
{
    std::int32_t field;
    std::int32_t id;
    std::int32_t x, y, width, height;
};
constexpr std::array<ChatEditPlace, 2> ChatEdits{{
    {UI::MessageField, UI::ChatMessageEditId, 0xc1, 0x1a6, 0xb4, 0xe},
    {UI::WhisperField, UI::ChatWhisperEditId, 0x178, 0x1a6, 0x32, 0xe},
}};
// 0x5a33dc..0x5a343a: el botón de F3, junto al EDIT del susurro.
constexpr std::int32_t WhisperModeLeft = 0x1b2;
constexpr std::int32_t WhisperModeTop = 0x19f;
constexpr std::int32_t WhisperModeRight = 0x1c6;
constexpr std::int32_t WhisperModeBottom = 0x1bc;

// 0x553520(vk).
bool KeyEdge(UI::GameUiState& ui, const Input::InputState& input, GameKey key)
{
    return UI::LatchTheKey(ui.keyLatches[key.virtualKey], input.IsDown(key.key));
}

// 0x6578e0: hay un campo de texto con el foco (el chat 0x6c8e68, 0x77f3b68, el del diálogo de los paneles) o la
// ventana del juego no lo tiene (GetFocus() distinto de [0x5763544]). MuRet tiene el del chat (hito 3), el del alta
// del gremio (0x77f3b68, CL9 hito 8) y, con el foco de Windows, el EDIT de las ventanas viejas (CL9 hito 9) y los dos
// de los diálogos: el de [0x5763504] (la entrada del número de serie, hito 11, y el alta del gremio, CL9 hito 8) y el
// del número del 0x7d (CL9 hito 8); los del gremio se quedan con el foco al cerrar (Q-C1291 y Q-C1295). El del diálogo
// de los paneles es de la CL9.5 (P-C285), y su ventana es la del juego.
bool TextInputBusy(const WorldRuntime& runtime)
{
    const UI::ChatFocus focus = runtime.chat.input.focus;
    const auto& panelDialog = runtime.panels.delegated.panelDialog;
    const bool panelField =
        UI::PanelDialogHandle(panelDialog) != 0 && (panelDialog.flags & UI::PanelDialogTextField) != 0;
    return runtime.ui.chatOpen || runtime.chat.input.dialogFieldOn || panelField || focus != UI::ChatFocus::Game;
}

// 0x636e51..0x636e5a: el registro del chat, 0x45aa80(0) por la vtable + 0x18 de 0x576350c.
Core::Expected<void, std::string> StepTheChat(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    ChatRuntime& chat = runtime.chat;
    const ActorState& hero = context.world.Hero();
    const UI::ChatLogGlobals globals{.leftClick = runtime.movement.pendingClick,
                                     .rightDown = ui.rightDown,
                                     .wheel = ui.wheel,
                                     .capture = ui.capture,
                                     .hovered = ui.hovered,
                                     .focusedList = ui.focusedList,
                                     .mouseOnUi = ui.mouseOnUi,
                                     .modes = ui.chatModes};
    // El foco en uno de los EDIT del chat: mientras el campo está abierto lo tiene (0x636e80).
    const UI::ChatLogStepView view{.mouseX = context.mouseX,
                                   .mouseY = context.mouseY,
                                   .leftDown = runtime.movement.leftHeld,
                                   .chatOpen = ui.chatOpen,
                                   .heroSafeZone = hero.safeZone != 0,
                                   .heroName = HeroNameOf(hero),
                                   .chatEditFocused = ui.chatOpen,
                                   .measure = context.measure,
                                   .dcFont = runtime.hud.dcFont};
    const UI::ChatLogStepResult result = UI::StepTheChatLog(chat.log, runtime.hud.chatFrame, globals, view);
    for (const std::int32_t sound : result.sounds)
        runtime.RequestSound(sound);
    // 0x4694c0(0, 0, 1, nombre): el nombre al EDIT del susurro (SetText), sin tocar su buffer.
    if (result.whisperTo)
        UI::SetTheChatFields(chat.input, std::nullopt, *result.whisperTo);
    return {};
}

// 0x636e5d..0x636ed1: con el izquierdo abajo y el mouse fuera de las ventanas (o sobre el registro del chat), el foco
// del administrador de las ventanas viejas (+0x44 de 0x5763514) en 0 y, con el chat y los otros campos cerrados
// (0x636e8c..0x636ea5: 0x77f3b69 el de los paneles, 0x77f3b68 el del alta del gremio y 0x77f3b6a el modo del zen del
// 0x73 y del precio del 0x91), la lista de las flechas en 0, 0x4a5e10 sobre la lista de ventanas y el foco del teclado
// a la ventana del juego (SetFocus; el IME de 0x467360, MuRet no lo tiene).
void StepTheUnownedClick(WorldRuntime& runtime)
{
    UI::GameUiState& ui = runtime.ui;
    if (!runtime.movement.leftHeld || (ui.hovered != 0 && ui.hovered != UI::ChatLogId))
        return;
    runtime.oldWindows.focus = 0;
    if (ui.chatOpen || runtime.chat.input.otherInput || runtime.chat.input.dialogFieldOn ||
        runtime.social.moneyField != 0)
        return;
    ui.focusedList = 0;
    UI::HideTheWindowList(runtime.oldWindows.windowList);
    runtime.chat.input.focus = UI::ChatFocus::Game;
}

// 0x435bd0: el paso de la interfaz de Crywolf (06 § La interfaz de Crywolf), solo en el mapa 34 (0x4303c0). Con los
// resultados (0x723bf9 en 1, del BD 08) y el izquierdo abajo (0x7d36874), sobre el botón (300, 354) × (300, 330), sin
// los bordes, deja 0x723bdc en 5, que el dibujo lee para cerrarlos con el mouse todavía encima (Q-C1342); en cualquier
// lugar apaga el botón y el flanco (0x435c4e): con los resultados a la vista, la UI se queda con todos los clics.
// Después, la ventana del contrato (0x723a44 en 1 o 2, 0x435c5c: Scenes/CrywolfContract), con la mascota del héroe
// (+0x318) y el send() del paso para el Sí.
Core::Expected<void, std::string> StepTheCrywolfInterface(const GameUiContext& context)
{
    using Maps::CrywolfMap;
    constexpr std::int32_t ButtonLeft = 0x12c;
    constexpr std::int32_t ButtonRight = 0x162;
    constexpr std::int32_t ButtonTop = 0x12c;
    constexpr std::int32_t ButtonBottom = 0x14a;
    constexpr std::int32_t ButtonPushed = 5;
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.map != CrywolfMap)
        return {};
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    CrywolfInterface& crywolf = runtime.crywolf.ui;
    GameMovementState& movement = runtime.movement;
    if (crywolf.byte723bf9 == 1 && movement.leftHeld)
    {
        if (mouseX > ButtonLeft && mouseX < ButtonRight && mouseY > ButtonTop && mouseY < ButtonBottom)
            crywolf.dwords723bdc[0] = ButtonPushed;
        movement.leftHeld = false;
        movement.pendingClick = false;
    }
    return StepTheContractWindow(
        ContractWindowContext{.runtime = runtime,
                              .texts = context.texts,
                              .heroPet = context.world.Hero().pet.model,
                              .mouseX = mouseX,
                              .mouseY = mouseY,
                              .send = context.chat != nullptr ? context.chat->send : ChatSender{}});
}

// 0x636f04..0x637056: la esquina del registro del chat (0x45a9d0). Fuera de su ventana, (−10, 0x65); en ella, 0x1a4 de
// alto y la x en 0 debajo de una ventana de la izquierda, o 0xba. Las consultas van en el orden del original, que se
// corta en la primera abierta: las del comercio y de la tienda de NPC sacan un rand().
void PlaceTheChatLog(WorldRuntime& runtime, UI::PanelRegistry& panels)
{
    UI::ChatLogFrame& frame = runtime.hud.chatFrame;
    if (runtime.ui.chatModes.placement != 1)
    {
        frame.x = -10;
        frame.y = 0x65;
        return;
    }
    bool left = false;
    if (panels.IsOpen(UI::InventoryPanel) &&
        (panels.IsOpen(UI::CharacterPanel) || panels.IsOpen(UI::TradePanel) || panels.IsOpen(UI::WarehousePanel) ||
         panels.IsOpen(UI::ChaosBoxPanel) || panels.IsOpen(UI::EventNpcPanel) ||
         panels.IsOpen(UI::SecondEventNpcPanel) || panels.IsOpen(UI::NpcShopPanel) ||
         panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel) || panels.IsOpen(UI::TrainerPanel)))
        left = true;
    // 0x636fff: sin el inventario, o con él y ninguna de esas, el personaje con la mascota o la misión.
    else if (panels.IsOpen(UI::CharacterPanel) && (panels.IsOpen(UI::PetPanel) || panels.IsOpen(UI::QuestPanel)))
        left = true;
    frame.x = left ? 0 : 0xba;
    frame.y = 0x1a4;
}

bool InRect(const GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

Core::Expected<void, std::string> Toggle(UI::PanelRegistry& panels, UI::PanelId id)
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

// Las cinco teclas que leen las ventanas viejas con 0x553520 (Esc, F5, F6, Enter y la F de la de amigos, 0x4a2f70) y
// las dos que leen abajo con GetAsyncKeyState (las flechas de las listas, 0x47b0f0).
// El panel del asedio consulta también 0x553520(9) en 0x40f6e8; comparte el latch con el resto de la UI.
constexpr std::array<GameKey, 6> OldWindowEdgeKeys{EscapeKey, F5Key, F6Key, EnterKey, Letter('F'), TabKey};
constexpr std::array<GameKey, 2> OldWindowDownKeys{UpKey, DownKey};

// 0x650387 y 0x6503a4: 0x553520 de PgUp y PgDn, que lee la lista de los miembros del gremio.
std::function<bool(std::uint8_t)> PageKeyEdges(UI::GameUiState& ui, const Input::InputState& input)
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

// GetAsyncKeyState de las flechas de una lista vieja (0x47b0f0).
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

// Lo que las ventanas viejas leen del juego en el paso de la UI (CL9 hito 9): la vista de 0x490490 (el mouse, las
// teclas, el chat abierto, Text.bmd, la medida del DC, el cable y el EDIT del susurro) y 0x491080, que la usa también
// (el panel 1 y 0x4906a0). Vive lo que el paso.
class OldWindowSight
{
public:
    explicit OldWindowSight(const GameUiContext& context) : m_context(context), m_texts(OldWindowTextsOf(context.texts))
    {
        if (context.measure != nullptr)
            m_measure.emplace(UI::OldWindowMeasure{
                .measure = *context.measure, .dcFont = context.world.Runtime().hud.dcFont, .screen = context.screen});
    }
    OldWindowSight(const OldWindowSight&) = delete;
    OldWindowSight& operator=(const OldWindowSight&) = delete;
    OldWindowSight(OldWindowSight&&) = delete;
    OldWindowSight& operator=(OldWindowSight&&) = delete;
    ~OldWindowSight() = default;

    [[nodiscard]] UI::OldWindowStepView View() const
    {
        UI::GameUiState& ui = m_context.world.Runtime().ui;
        const Input::InputState& input = m_context.input;
        UI::OldWindowStepView view{.mouseX = m_context.mouseX,
                                   .mouseY = m_context.mouseY,
                                   .keyEdge = [&ui, &input](std::uint8_t virtualKey)
                                   { return OldWindowKeyEdge(ui, input, virtualKey); },
                                   .chatOpen = ui.chatOpen,
                                   .texts = &m_texts,
                                   .measure = m_measure ? &*m_measure : nullptr,
                                   .send = {},
                                   .openFriends = [this]() { return OpenTheFriendsWindow(); }};
        if (IGameUiRequests* requests = m_context.requests; requests != nullptr)
            view.send = [requests, &random = m_context.random](const UI::FriendSend& send)
            { SendAFriendPacket(*requests, random, send); };
        LendTheGameHooks(view, m_context, true);
        return view;
    }

    // 0x491080(0x186, 0x106) con los globales de ahora.
    [[nodiscard]] Core::Expected<void, std::string> OpenTheFriendsWindow() const
    {
        const UI::OldWindowGlobals globals = OldWindowGlobalsOf(m_context.world.Runtime());
        const UI::OldWindowStepView view = View();
        return OpenTheFriends(FriendsOpenContext{.world = m_context.world,
                                                 .random = m_context.random,
                                                 .measure = m_context.measure,
                                                 .screen = m_context.screen,
                                                 .texts = m_context.texts},
                              globals, view);
    }

private:
    const GameUiContext& m_context;
    UI::OldWindowTexts m_texts;
    std::optional<UI::OldWindowMeasure> m_measure;
};

// 0x636e3c..0x636e4c: el paso de la lista de ventanas ([0x5763524], la vtable + 0x18 con 0) y el de las ventanas
// viejas (0x490490). Esc llega a 0x490490 ya leído por 0x623e70, salvo con la de amigos abierta (Q-C446).
Core::Expected<void, std::string> StepTheOldWindows(const GameUiContext& context, const OldWindowSight& sight)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::OldWindowStepView view = sight.View();
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    if (auto list = UI::StepTheWindowList(runtime.oldWindows, globals, view); !list.HasValue())
        return list;
    return UI::StepTheOldWindows(runtime.oldWindows, globals, view);
}

// ---------------------------------------------------------------------------------------------------------------
// El paso del inventario abierto (0x5ee1ac..0x5ee1ca) y el del ítem del cursor (0x5d8d50), CL8 hito 4. Leen la esquina
// que dejó su dibujo (0x78a5a14 y 0x78a5a10). Levantar, soltar y equipar están en Scenes/InventoryMoves.

// La lista del asedio en el modo 3, que con 0x6689d0 se suma a las ventanas con grilla.
constexpr std::int32_t SiegeListWithInventory = 3;
// 0x449c00: la caja del entrenador, y su grilla fija de 0x44ad10 y 0x44aca0.
constexpr std::int32_t TrainerWithBox = 3;
constexpr std::int32_t TrainerBoxLeft = 0x113;
constexpr std::int32_t TrainerBoxTop = 0x6e;
constexpr std::uint16_t RepairButtonLevel = 0x32;
constexpr std::uint16_t PersonalShopLevel = 6;
// 0x41ed10(0x1d, 0, 0): el sonido de levantar de la grilla (0x5c587a).
constexpr std::int32_t ItemMoveSound = 0x1d;
// El rand() del relleno (0x69fd70) y el del número de serie (0x40b0a0) de un C3.
constexpr std::size_t C3SendRandoms = 2;
// 0x5c4298..0x5c42a9 y 0x5aed6c..0x5aed7d: el lugar del ítem del cartel es la casilla + 12; con una tienda personal
// abierta, + 0x4c (0x5c4353..0x5c4363).
constexpr std::int32_t FirstGridSlot = 12;
constexpr std::int32_t FirstPersonalShopSlot = 0x4c;
// 0x5c5131: 0x78a6758 sin lugar.
constexpr std::int32_t NoCursorSlot = -1;
// 0x5c516b..0x5c5177: 0x633300(1, 1100, 0x94), la compra en la tienda de otro, que espera el aviso 5 sin respuesta.
constexpr std::int32_t BuyDialogLines = 1;
constexpr std::int32_t BuyDialogText = 1100;
constexpr std::int32_t BuyDialog = 0x94;
constexpr std::int32_t BuyWarningKind = 5;
constexpr std::uint8_t NoWarningAnswer = 0xff;
// 0x5c4b04..0x5c4b22: Ctrl con Q, W o E pone el ítem bajo el mouse en esa ranura rápida, con su nivel.
constexpr std::array<char, 3> CtrlQuickSlotLetters{'Q', 'W', 'E'};
constexpr GameKey CtrlKey{Key::Ctrl, 0x11};
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t CellSize = 20;
// 0x5e58f0..0x5e58fe: la grilla de las tiendas personales (0x78ab0b8), de 8 × 4, desde (x + 15, y + 0x6e).
constexpr std::int32_t ShopGridLeft = 15;
constexpr std::int32_t ShopGridTop = 0x6e;
constexpr std::int32_t ShopGridColumns = 8;
constexpr std::int32_t ShopGridRows = 4;
constexpr std::uint8_t HoveredMark = 2;
// 0x5aed40: 0x78abb28 en 2 con la grilla propia del comercio (0x78a92a8), en 3 con la del otro (0x78a5a20) y en 5 con
// la de la tienda mirando la de otro (0x78ab0b8).
constexpr std::int32_t OwnTradeTooltip = 2;
constexpr std::int32_t OtherTradeTooltip = 3;
constexpr std::int32_t OtherShopTooltip = 5;

// 0x5c43f4..0x5c445d: lo que la reparación no mira (las mascotas de 0x1a00 a 0x1a04, 0x1a0a, las flechas, lo de
// 0x1c00 en adelante, las alas de 0x1807 a 0x1813, de 0x1a0e a 0x1a13, 0x1c15 y el Wizard's Ring), con signo.
// 0x5c2e4d..0x5c2e5d: el del equipo tampoco repara el Dark Raven.
constexpr std::uint16_t RavenItem = 0x1a05;

bool IsNotRepaired(std::uint16_t id)
{
    const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(id));
    const auto in = [type](std::int32_t first, std::int32_t last) { return type >= first && type <= last; };
    return in(0x1a00, 0x1a04) || type == 0x1a0a || type == 0x807 || type == 0x80f || type >= 0x1c00 ||
           in(0x1807, 0x1813) || in(0x1a0e, 0x1a13) || type == 0x1c15 || type == 0x1a14;
}

// Los servicios de levantar y soltar; sin ellos, los vacíos, y lo que los necesita corta.
const InventoryServices& ServicesOf(const GameUiContext& context)
{
    static const InventoryServices None{};
    return context.inventory != nullptr ? *context.inventory : None;
}

InventoryMoveContext MovesOf(const GameUiContext& context)
{
    return InventoryMoveContext{.world = context.world, .random = context.random, .services = ServicesOf(context)};
}

// El mouse en [left, left + w) × [top, top + h), con el borde de la derecha y el de abajo en la x87:
// FILD mouse; FILD left; FADD w; FCOMPP.
bool MouseInEquipmentRect(const GameUiContext& context, std::int32_t left, std::int32_t top, float width, float height)
{
    const auto x = static_cast<double>(context.mouseX);
    const auto y = static_cast<double>(context.mouseY);
    return context.mouseX >= left && static_cast<double>(left) + static_cast<double>(width) > x &&
           context.mouseY >= top && static_cast<double>(top) + static_cast<double>(height) > y;
}

// 0x5bec50(x, y, w, h, lugar): un lugar del equipo. Con la espera de 0x5767954 o la traba de C3 24 sale sin abrir el
// registro propio; si no, lo abre (3.848 rand() al cerrarlo sin otros accesos) y todo lo que sigue corre adentro. Con
// un ítem en el cursor y el mouse en el lugar, soltarlo (0x5bee04); sin él, con el lugar ocupado y el mouse en el
// lugar, 0x78abaac en 1 y, con el clic, levantarlo (0x5c2c65) o, sin el clic, la marca y la ayuda (0x5c35ad).
Core::Expected<void, std::string> StepAnEquipmentSlot(const GameUiContext& context, float x, float y, float width,
                                                      float height, std::size_t slot)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0)
        return {};
    // _ftol de las constantes, que son enteras.
    const std::int32_t left = static_cast<std::int32_t>(x) + runtime.hud.inventoryX;
    const std::int32_t top = static_cast<std::int32_t>(y) + runtime.hud.inventoryY;
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const bool inside = MouseInEquipmentRect(context, left, top, width, height);
    if (runtime.cursorItemCount > 0)
    {
        if (!inside)
            return {};
        return DropOnTheEquipment(MovesOf(context), slot);
    }
    if (context.world.OwnCharacter().equipment[slot].id == EmptyOwnItemId || !inside)
        return {};
    // 0x5c2c56..0x5c2c5f.
    runtime.ui.field78abaac = 1;
    if (runtime.movement.pendingClick)
    {
        // 0x5c2c65..0x5c2c79 y 0x5c2dd5..0x5c3594: con el modo de reparar (y sin 0x576794c), lo que se repara gasta el
        // clic y sale el C1 05 34 con el lugar y 0x78abb10, en C3 y con el registro abierto.
        if (runtime.movement.gateRequestBlocked == 0 && runtime.panels.repairMode != 0)
        {
            const std::uint16_t worn = context.world.OwnCharacter().equipment[slot].id;
            if (IsNotRepaired(worn) || worn == RavenItem)
                return {};
            runtime.movement.pendingClick = false;
            return SendRepair(context.random, context.sendNpcWindow, static_cast<std::uint8_t>(slot & 0xff),
                              static_cast<std::uint8_t>(runtime.field78abb10 & 0xff));
        }
        return PickUpFromTheEquipment(MovesOf(context), slot);
    }
    HoverTheEquipment(MovesOf(context), slot, left, top, width);
    return {};
}

// 0x5c3d20: los doce lugares, el 8 y el 7 primero, el casco con la clase leída con el registro abierto (su
// 0x5bec50 queda adentro y no saca nada al cerrar) y los otros nueve.
Core::Expected<void, std::string> StepTheEquipment(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
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

// Lo que hizo el ítem de la grilla bajo el mouse: con levantarlo (0x5c5884) o el clic derecho (0x5c5895..0x5c9b0b),
// 0x5c4110 sale.
enum class GridHover : std::uint8_t
{
    Stayed,
    PickedUp,
    RightClicked,
};

// La grilla de 0x5c4110(x, y, grilla, columnas, filas, tipo), todas de 8 columnas: la del inventario (0x78a9bb0,
// 8 × 8), las del comercio (0x78a92a8 y 0x78a5a20, 8 × 4), la de la tienda personal y las de las ventanas de NPC (CL9
// hito 6). Sus casillas, el lugar que anota el ítem bajo el mouse y, en las de NPC, sus filas, el tipo (1 la tienda de
// NPC, que compra; 0 las demás) y la grilla de donde sale lo que se levanta.
struct HoverGrid
{
    std::span<World::ItemState> cells;
    UI::HoveredItem::Place place = UI::HoveredItem::Place::InventoryCell;
    std::int32_t rows = 0;
    std::uint8_t kind = 0;
    CursorGrid grid = CursorGrid::None;
};

// 0x5c4110 con el tipo 1 (0x5e57af, la tienda): comprar.
constexpr std::uint8_t ShopGridKind = 1;
// 0x5aed40: el lugar del ítem para su ayuda en la grilla del almacén (1) y en la caja del caos (4).
constexpr std::int32_t WarehouseHelpPlace = 1;
constexpr std::int32_t ChaosBoxHelpPlace = 4;
// 0x5c4b6e: con la tienda, 0x78abb28 en 6.
constexpr std::int32_t ShopHelpPlace = 6;
static_assert(NpcGridColumns == static_cast<std::int32_t>(InventoryColumns));

// 0x5c4b64..0x5c510f: el ítem bajo el mouse en la grilla de la tienda. 0x78abb28 en 6 y 0x78abb2c en la casilla; con
// el clic, sin el modo de reparar y sin otra compra esperando (0x5767950), el clic gastado, el precio de 0x54d0f0 en
// el modo 0 del ítem de arriba a la izquierda, el C1 04 32 con su lugar en C3 y el precio en 0x5767950, hasta el 32.
Core::Expected<GridHover, std::string> BuyTheGridItem(const GameUiContext& context, const HoverGrid& grid,
                                                      std::size_t index)
{
    WorldRuntime& runtime = context.world.Runtime();
    const World::ItemState& hovered = grid.cells[index];
    runtime.ui.field78abb28 = ShopHelpPlace;
    runtime.ui.field78abb2c = static_cast<std::int32_t>(index);
    NpcWindowState& npc = runtime.npcWindows;
    if (!runtime.movement.pendingClick || runtime.panels.repairMode != 0)
        return GridHover::Stayed;
    runtime.movement.pendingClick = false;
    if (npc.buyLock != 0)
        return GridHover::Stayed;
    const std::int32_t topLeft = hovered.gridRow * NpcGridColumns + hovered.gridColumn;
    if (topLeft >= static_cast<std::int32_t>(grid.cells.size()))
        return Core::Unexpected(std::format("0x5c4bc4: el ítem de arriba a la izquierda ({}) cae fuera de la grilla de "
                                            "la tienda",
                                            topLeft));
    const Tables::ItemTable* items = ServicesOf(context).items;
    if (items == nullptr)
        return Core::Unexpected(std::string("0x5c4bc4: el precio de 0x54d0f0 sin Item.bmd"));
    const PetPriceLevels pets{.raven = runtime.pets[RavenPetStats].level,
                              .horse = runtime.pets[DarkHorsePetStats].level};
    auto price = ItemPrice(grid.cells[static_cast<std::size_t>(topLeft)], *items, ShopGridPrice, pets);
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

// 0x5c5895..0x5c9b0b (CL9 hito 5): el clic derecho sobre el ítem de la casilla `index` de la grilla, consumido, con la
// casilla de arriba a la izquierda del ítem (con el ancho de la grilla, sin el 12). Los que solo mandan el C1 06 26
// (UseInventoryItem): el 0x1c0a sin 0x576794c (0x5c58bd), el 0x1c0b de nivel 14 (0x5c608c) y las pociones (0x1c00..
// 0x1c06, 0x1c08, 0x1c09, el 0x1c14 de nivel 0 y el 0x1c07, 0x5c73ea). Las entradas a los eventos cortan con su
// dirección (0x5c6865, después de las ventanas que mira el 0x1a1d; 0x5c6c36 y 0x5c7010). Desde 0x5c7bc6, los libros y
// las esferas de skills, la fruta, el 0x1a0b y el resto: UseTheLaterGridItem. Con la grilla propia del comercio
// (0x5c4aa5 con 0x78a92a8, CL9 hito 7, 08 P-C235) la rama es la misma: lo que llega a UseInventoryItem da el aviso 474,
// porque el comercio está abierto, y los demás siguen en UseTheLaterGridItem con esa grilla.
Core::Expected<void, std::string> UseTheGridItem(const GameUiContext& context, UI::PanelRegistry& panels,
                                                 const HoverGrid& grid, std::size_t index)
{
    const World::ItemState& hovered = grid.cells[index];
    WorldRuntime& runtime = context.world.Runtime();
    runtime.ui.rightClick = false;
    constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    const std::int32_t type = std::bit_cast<std::int16_t>(hovered.id);
    const std::uint32_t levelBits = hovered.scalar1Low & UsedLevelBits;
    const auto use = [&] { return UseInventoryItem(MovesOf(context), panels, cell); };
    if (type == TownPortalScroll)
    {
        if (runtime.movement.gateRequestBlocked != 0)
            return {};
        return use();
    }
    if (type == BoxOfLuck && levelBits == UsedBoxLevelBits)
        return use();
    if (IsAnEventTicket(type))
        return UseTheEventTicket(MovesOf(context), panels, UsedGrid{.cells = grid.cells, .grid = grid.grid}, index);
    const bool potion = (type >= 0x1c00 && type <= 0x1c06) || type == 0x1c08 || type == 0x1c09 ||
                        (type == 0x1c14 && levelBits == 0) || type == 0x1c07;
    if (potion)
        return use();
    const CursorGrid used = grid.place == UI::HoveredItem::Place::OwnTradeCell    ? CursorGrid::OwnTrade
                            : grid.place == UI::HoveredItem::Place::InventoryCell ? CursorGrid::Inventory
                                                                                  : grid.grid;
    return UseTheLaterGridItem(MovesOf(context), panels, UsedGrid{.cells = grid.cells, .grid = used}, index);
}

// 0x5c5361..0x5c5876, después de levantar: IsOpen(0x20), con su rand(); con el comercio abierto, el OK propio puesto
// y el ítem de la grilla propia, el OK en 0 y el C1 04 3C 00 en C3. Después el sonido 0x1d.
Core::Expected<void, std::string> AfterThePickUp(const GameUiContext& context, UI::PanelRegistry& panels,
                                                 const HoverGrid& grid)
{
    WorldRuntime& runtime = context.world.Runtime();
    TradeState& trade = runtime.social.trade;
    if (panels.IsOpen(UI::TradePanel) && trade.ownOk != 0 && grid.place == UI::HoveredItem::Place::OwnTradeCell)
    {
        trade.ownOk = 0;
        context.random.Discard(2);
        const InventoryServices& services = ServicesOf(context);
        if (!services.sendGamePacket)
            return Core::Unexpected(std::string("0x5c5396: el C1 04 3C sin la sesión"));
        services.sendGamePacket(Protocol::Social::WriteTradeOkButton(0));
    }
    runtime.RequestSound(ItemMoveSound);
    return {};
}

// La grilla de `place` como la guarda 0x78ab9b8.
CursorGrid CursorGridOf(UI::HoveredItem::Place place)
{
    switch (place)
    {
    case UI::HoveredItem::Place::InventoryCell:
        return CursorGrid::Inventory;
    case UI::HoveredItem::Place::OwnTradeCell:
        return CursorGrid::OwnTrade;
    case UI::HoveredItem::Place::OtherTradeCell:
        return CursorGrid::OtherTrade;
    case UI::HoveredItem::Place::ShopCell:
        return CursorGrid::PersonalShop;
    default:
        return CursorGrid::None;
    }
}

// 0x5c5110..0x5c5241 (CL9 hito 7, 08 P-C208 b): el clic sobre el ítem con una tienda personal abierta, sin reparar.
// El clic consumido, la grilla en 0x78ab9b8, la casilla de arriba a la izquierda y 0x78a6758 en −1. Mirando la de otro
// (0x6c931c en 2), el ítem de su grilla va a 0x78a59c8 con su lugar (+ 0x4c) y el diálogo 0x94 de los textos 1100
// (0x633300(1, 1100, 0x94)) espera el aviso 5 (0x78abb14) sin respuesta; uno del inventario se levanta (+ 12). Con la
// propia (1), se levanta el de su grilla (+ 0x4c) o el del inventario (+ 12). Al final, 0x78abaa8 en 0; ni sonido ni el
// comercio de 0x5c5361.
Core::Expected<void, std::string> ClickWithTheShopOpen(const GameUiContext& context, const HoverGrid& grid,
                                                       const World::ItemState& hovered)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.movement.pendingClick = false;
    const CursorGrid from = CursorGridOf(grid.place);
    runtime.cursorGrid = from;
    constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    runtime.slot78a6758 = NoCursorSlot;
    const std::int32_t mode = runtime.social.personalShop.mode;
    const bool shopGrid = from == CursorGrid::PersonalShop;
    const bool inventoryGrid = from == CursorGrid::Inventory;
    if (mode == OtherShopView && shopGrid)
    {
        runtime.slot78a6758 = cell + FirstPersonalShopSlot;
        if (static_cast<std::size_t>(cell) >= grid.cells.size())
            return Core::Unexpected(std::format("0x5c515f: la casilla {} está fuera de la grilla de la tienda", cell));
        runtime.dialogItem = grid.cells[static_cast<std::size_t>(cell)];
        if (auto opened = UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, BuyDialogLines,
                                             BuyDialogText, BuyDialog);
            !opened.HasValue())
            return opened;
        runtime.social.trade.warningKind = BuyWarningKind;
        runtime.social.trade.warningByte = NoWarningAnswer;
        runtime.ui.hoveredItem = {};
        return {};
    }
    const bool picks = (mode == OwnShopView && (shopGrid || inventoryGrid)) || (mode == OtherShopView && inventoryGrid);
    if (picks)
    {
        const std::int32_t slot = cell + (shopGrid ? FirstPersonalShopSlot : FirstGridSlot);
        if (auto picked = PickUpForTheShop(MovesOf(context), slot, from); !picked.HasValue())
            return picked;
    }
    runtime.ui.hoveredItem = {};
    return {};
}

// 0x5c5242..0x5c52b0 (CL9 hito 7, 08 P-C208 b): el clic derecho con una tienda personal abierta, sin reparar. El clic
// derecho consumido, la grilla en 0x78ab9b8, el ítem de arriba a la izquierda en 0x78a59c8 y su casilla + 0x4c en
// 0x78a6758 (con cualquier grilla). Con la propia y su grilla, el diálogo del precio (0x5eba70(2)). 0x78abaa8 queda.
Core::Expected<void, std::string> RightClickWithTheShopOpen(const GameUiContext& context, const HoverGrid& grid,
                                                            const World::ItemState& hovered)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.ui.rightClick = false;
    const CursorGrid from = CursorGridOf(grid.place);
    runtime.cursorGrid = from;
    constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    if (static_cast<std::size_t>(cell) >= grid.cells.size())
        return Core::Unexpected(std::format("0x5c526d: la casilla {} está fuera de la grilla", cell));
    runtime.dialogItem = grid.cells[static_cast<std::size_t>(cell)];
    runtime.slot78a6758 = cell + FirstPersonalShopSlot;
    if (runtime.social.personalShop.mode == OwnShopView && from == CursorGrid::PersonalShop)
        OpenTheShopDialog(runtime, ShopPriceMode);
    return {};
}

// 0x5c41f4..0x5c4b3b y 0x5c52b1..0x5c5894: el ítem de la casilla `index` de `grid` bajo el mouse. La marca 2 en su
// huella (en la del otro del comercio, sin pisar el 99), el ítem del cartel (0x78abaa8, su copia en 0x787a518 y su
// lugar) y la esquina de su ayuda. Con la grilla del otro, solo 0x5aed40 (0x5c4b2b); con la tienda de NPC, comprar. Si
// no, según las tiendas personales, la reparación y los clics, levantarlo, usarlo, 0x5aed40 y Ctrl con Q, W o E.
Core::Expected<GridHover, std::string> HoverTheGridItem(const GameUiContext& context, UI::PanelRegistry& panels,
                                                        const HoverGrid& grid, std::size_t index, std::int32_t left,
                                                        std::int32_t top)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const std::span<World::ItemState> cells = grid.cells;
    const World::ItemState& hovered = cells[index];
    const bool ownTrade = grid.place == UI::HoveredItem::Place::OwnTradeCell;
    const bool otherTrade = grid.place == UI::HoveredItem::Place::OtherTradeCell;
    // 0x5c41f4..0x5c4284: el ancho y el alto de Item.bmd, y la marca desde la columna y la fila del ítem, sin mirar los
    // bordes: un ítem puesto entra entero.
    const auto footprint = ItemFootprintOf(ServicesOf(context).items, hovered.id, "0x5c4110 (0x5c420a)");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    const std::int32_t column = hovered.gridColumn;
    const std::int32_t row = hovered.gridRow;
    constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
    static_assert(Columns == TradeGridColumns && Columns == NpcGridColumns);
    for (std::int32_t y = row; y < row + footprint.Value().height; ++y)
    {
        for (std::int32_t x = column; x < column + footprint.Value().width; ++x)
        {
            const std::int32_t marked = y * Columns + x;
            if (marked >= static_cast<std::int32_t>(cells.size()))
                return Core::Unexpected(std::format("0x5c425e: la marca de la casilla {} cae fuera de la grilla "
                                                    "(Q-C200)",
                                                    marked));
            World::ItemState& marking = cells[static_cast<std::size_t>(marked)];
            // 0x5c4243..0x5c4256: en la grilla del otro, el 99 de la copia (0x5bc470) queda.
            if (otherTrade && marking.warning == ChangedFromTheCopy)
                continue;
            marking.warning = HoveredMark;
        }
    }
    // 0x5c4286..0x5c4302.
    const auto cell = static_cast<std::int32_t>(index);
    ui.field78aba27 = grid.kind;
    ui.hoveredItem = UI::HoveredItem{.place = grid.place, .index = index};
    runtime.hoveredItemCopy = hovered;
    ui.field78abb2c = cell + FirstGridSlot;
    ui.skillTooltipX = left + footprint.Value().width * CellSize / 2 + column * CellSize;
    ui.skillTooltipY = top + row * CellSize;
    // 0x5c42fd..0x5c4308 → 0x5c4b2b: la del otro del comercio, 0x5aed40 con 3 y la casilla, y la casilla siguiente.
    if (otherTrade)
    {
        ui.field78abb28 = OtherTradeTooltip;
        ui.field78abb2c = cell;
        return GridHover::Stayed;
    }
    if (grid.kind != 0)
        return BuyTheGridItem(context, grid, index);
    // 0x5c4319..0x5c4324: la grilla de la tienda personal.
    if (grid.place == UI::HoveredItem::Place::ShopCell)
        ui.shopGridHovered = 1;
    // 0x5c432b..0x5c43ee: las tiendas personales (0x800 y 0x20000), sin rand(), y la reparación (0x78abb0c).
    const auto shopOpen = [&panels]
    { return panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel); };
    if (shopOpen())
    {
        ui.field78abb28 = 0;
        ui.field78abb2c = cell + FirstPersonalShopSlot;
    }
    const bool repairing = runtime.panels.repairMode != 0;
    if (shopOpen() && runtime.movement.pendingClick && !repairing)
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
        if (IsNotRepaired(hovered.id) || !runtime.movement.pendingClick)
            return GridHover::Stayed;
        // 0x5c4472..0x5c4a89: el clic consumido y el C1 05 34 con el lugar de la esquina del ítem (fila · columnas +
        // columna + 12) y 0x78abb10; después sigue con la casilla de al lado (0x5c4b3b).
        runtime.movement.pendingClick = false;
        const auto position = static_cast<std::uint8_t>((row * Columns + column + FirstGridSlot) & 0xff);
        if (auto sent = SendRepair(context.random, context.sendNpcWindow, position,
                                   static_cast<std::uint8_t>(runtime.field78abb10 & 0xff));
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return GridHover::Stayed;
    }
    // 0x5c4a8e..0x5c4a9f y 0x5c52b1..0x5c5894: levantarlo.
    if (runtime.movement.pendingClick && runtime.movement.gateRequestBlocked == 0)
    {
        auto picked = ownTrade ? PickUpFromTheTradeGrid(MovesOf(context), index)
                      : grid.place == UI::HoveredItem::Place::InventoryCell
                          ? PickUpFromTheGrid(MovesOf(context), index)
                          : PickUpFromOtherGrid(MovesOf(context), grid.grid, index);
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
    // 0x5c4ab7: 0x5aed40 con la grilla: la del inventario con 0 y la casilla + 12, la propia del comercio con 2 y la
    // casilla; la de la tienda, mirando la de otro (0x5aedee..0x5aee10), con 5 y la casilla + 0x4c, y con la propia
    // nada (quedan los de 0x5c4353); la caja del caos y la del entrenador (0x449c30) con 4 y la casilla, y la del
    // almacén (con 0x40 abierto, sin rand()) con 1 y la casilla.
    if (grid.place == UI::HoveredItem::Place::ShopCell)
    {
        if (runtime.social.personalShop.mode == OtherShopView)
        {
            ui.field78abb28 = OtherShopTooltip;
            ui.field78abb2c = cell + FirstPersonalShopSlot;
        }
    }
    else if (grid.grid == CursorGrid::ChaosBox || grid.grid == CursorGrid::TrainerBox)
    {
        ui.field78abb28 = ChaosBoxHelpPlace;
        ui.field78abb2c = cell;
    }
    else if (grid.grid == CursorGrid::NpcGrid)
    {
        if (panels.IsOpen(UI::WarehousePanel))
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
    // 0x5c4abf..0x5c4b29: GetAsyncKeyState(0x11) y 0x553520 de Q, W y E, en ese orden; la primera gana.
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

// 0x5c4110(x, y, grilla, 8, filas, tipo) con las grillas de las ventanas de NPC. Sale con la espera, la traba de C3 24
// o un ítem en el cursor; si no, 0x78abb38 en 0 (0x5c4163) y recorre las casillas por filas: con el mouse sobre una con
// un tipo, lo de ese ítem. Levantar o el clic derecho la terminan.
Core::Expected<void, std::string> StepTheGrid(const GameUiContext& context, UI::PanelRegistry& panels,
                                              const HoverGrid& grid, std::int32_t left, std::int32_t top)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    runtime.ui.shopGridHovered = 0;
    for (std::int32_t row = 0; row < grid.rows; ++row)
    {
        const std::int32_t cellTop = top + row * CellSize;
        for (std::int32_t column = 0; column < NpcGridColumns; ++column)
        {
            const std::int32_t cellLeft = left + column * CellSize;
            const auto index = static_cast<std::size_t>(row * NpcGridColumns + column);
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize || grid.cells[index].id == EmptyOwnItemId)
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

// 0x5c4110(x + 15, y + 200, 0x78a9bb0, 8, 8, 0): la grilla del inventario. Sale con la espera, la traba de C3 24 o
// un ítem en el cursor; si no, recorre las casillas por filas y, con el mouse sobre una con un tipo, lo de ese ítem.
Core::Expected<void, std::string> StepTheInventoryGrid(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    // 0x5c4163.
    runtime.ui.shopGridHovered = 0;
    const std::int32_t left = runtime.hud.inventoryX + 15;
    const std::int32_t top = runtime.hud.inventoryY + 200;
    for (std::size_t row = 0; row < InventoryColumns; ++row)
    {
        const std::int32_t cellTop = top + static_cast<std::int32_t>(row) * CellSize;
        for (std::size_t column = 0; column < InventoryColumns; ++column)
        {
            const std::int32_t cellLeft = left + static_cast<std::int32_t>(column) * CellSize;
            const std::size_t index = row * InventoryColumns + column;
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize || context.world.Inventory().cells[index].id == EmptyOwnItemId)
                continue;
            const auto hovered = HoverTheGridItem(
                context, panels,
                HoverGrid{.cells = context.world.Inventory().cells, .place = UI::HoveredItem::Place::InventoryCell},
                index, left, top);
            if (!hovered.HasValue())
                return Core::Unexpected(hovered.Error());
            if (hovered.Value() != GridHover::Stayed)
                return {};
        }
    }
    return {};
}

// 0x5e545f..0x5e5717: los botones del pie (reparar, cerrar y la tienda personal) y la S, sin la caja del
// entrenador (0x449c00). Con la ventana de un NPC de evento (0x1000 o 0x4000), solo cerrar en x + 0x19; MuRet ya
// cortó con ella abierta.
Core::Expected<void, std::string> StepTheFootButtons(const GameUiContext& context, UI::PanelRegistry& panels,
                                                     std::uint16_t level)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::PanelState& state = runtime.panels;
    GameMovementState& movement = runtime.movement;
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    const auto inFoot = [&context, x, y](std::int32_t from, std::int32_t to)
    { return InRect(context, x + from, y + 0x18b, x + to, y + 0x1a3); };
    const auto toggleThePersonalShop = [&panels]() -> Core::Expected<void, std::string>
    {
        if (panels.IsOpen(UI::PersonalShopPanel))
        {
            if (auto closed = panels.Close(UI::PersonalShopPanel); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            return {};
        }
        if (auto opened = panels.Open(UI::PersonalShopPanel); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return {};
    };
    if (state.delegated.trainer == TrainerWithBox)
        return {};
    if (panels.IsOpen(UI::EventNpcPanel) || panels.IsOpen(UI::SecondEventNpcPanel))
        return {};
    if (panels.IsOpen(UI::TradePanel) || panels.IsOpen(UI::NpcShopPanel) || panels.IsOpen(UI::WarehousePanel) ||
        panels.IsOpen(UI::ChaosBoxPanel) || state.field78abb00 != 0)
        return {};
    if (level >= RepairButtonLevel)
    {
        runtime.field78abb10 = 1;
        if (inFoot(0x30, 0x48) && movement.pendingClick)
        {
            movement.pendingClick = false;
            state.repairMode ^= 1;
        }
    }
    if (inFoot(0x14, 0x2c) && movement.pendingClick)
    {
        movement.pendingClick = false;
        movement.mouseUpdateTime = 0;
        movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
        if (auto closed = panels.Close(UI::InventoryPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    if (inFoot(0x4c, 100))
    {
        if (level < PersonalShopLevel)
            return {};
        if (movement.pendingClick)
        {
            movement.pendingClick = false;
            if (auto toggled = toggleThePersonalShop(); !toggled.HasValue())
                return toggled;
        }
    }
    // 0x5e5643..0x5e5717: la S, con nivel 6 o más y sin el campo del chat. 0x77f3b69 (un campo del hito 6) MuRet no
    // lo tiene, y el diálogo 0x73 con 0x6c7b8c no puede estar: 0x5ee140 no llega acá con un diálogo.
    if (level < PersonalShopLevel || runtime.ui.chatOpen || !KeyEdge(runtime.ui, context.input, Letter('S')))
        return {};
    return toggleThePersonalShop();
}

// Manda un paquete por el socket del juego con send(), sin la cola (0x5e494b, 0x424610). Con el socket en −1 (`send`
// vacío) no manda nada y sigue, como el original; sin el contexto de la sesión (los tests que no lo miran), corta.
Core::Expected<void, std::string> SendRaw(const GameUiContext& context, Protocol::Outgoing packet,
                                          std::string_view where)
{
    if (context.chat == nullptr)
        return Core::Unexpected(std::format("{}: el send() sin el contexto de la sesión", where));
    if (context.chat->send)
        context.chat->send(std::move(packet));
    return {};
}

// 0x5e4510: con el personaje abierto, el mouse sobre la ventana es de la UI; con puntos para repartir (+0x68), los
// botones de las cuatro estadísticas (cinco con el Dark Lord, 0x6c9330) mandan F3 06, con el registro propio abierto
// alrededor (3.848 rand() al cerrarlo sin otros accesos); y el botón de cerrar.
Core::Expected<void, std::string> StepTheCharacterWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::CharacterPanel))
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    const std::int32_t x = runtime.hud.characterX;
    const std::int32_t y = runtime.hud.characterY;
    if (InRect(context, x, y, x + CharacterWindowWidth, y + CharacterWindowHeight))
        runtime.ui.mouseOnUi = 1;
    const ActorState& hero = context.world.Hero();
    const std::int32_t buttons = Kernel::BaseClassOf(hero.characterClass) == Kernel::CharacterClass::DarkLord ? 5 : 4;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        if (context.world.OwnCharacter().levelUpPoints != 0)
        {
            for (std::int32_t button = 0; button < buttons; ++button)
            {
                const std::int32_t top = y + 0x73 + button * 0x3c;
                if (!InRect(context, x + 0x7d, top, x + 0x95, top + 0x18) || !movement.pendingClick)
                    continue;
                movement.pendingClick = false;
                if (auto sent = SendRaw(context, Protocol::Player::WriteLevelUpPoint(static_cast<std::uint8_t>(button)),
                                        "0x5e494b");
                    !sent.HasValue())
                    return sent;
                runtime.RequestSound(ButtonSound);
            }
        }
    }
    if (!InRect(context, x + 0x19, y + 0x18b, x + 0x31, y + 0x1a3) || !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    if (auto closed = panels.Close(UI::CharacterPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

// 0x424759..0x424990, la ventana de la mascota abierta (CL9 hito 4): a la izquierda de 0x1c2, o de 0x104 con el
// personaje abierto, 190 × 433 es de la UI (0x775857c). Las dos pestañas (la 0 el caballo y la 1 el cuervo, de a 45
// desde 15, de 49 de ancho): un clic en la que no está elegida, entre 67 y 83, la elige; en la elegida, entre 63 y 83,
// solo se come el clic. El botón de cerrar (20, 395, de 24 × 24) cierra el panel 0x400. Todo en float, con los bordes
// incluidos salvo los de la UI y los del botón. Sin rand().
Core::Expected<void, std::string> StepThePetWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    const std::int32_t left = panels.IsOpen(UI::CharacterPanel) ? PetWindowBesideTheCharacter : PetWindowLeft;
    const auto mouseX = static_cast<float>(context.mouseX);
    const auto mouseY = static_cast<float>(context.mouseY);
    const auto base = static_cast<float>(left);
    if (left <= context.mouseX && mouseX < base + PetWindowWidth && context.mouseY >= 0 && mouseY < PetWindowHeight)
        runtime.ui.mouseOnUi = 1;
    const auto clickTab = [&](std::uint8_t tab, float top)
    {
        const float x = static_cast<float>(tab * PetTabStep) + base + PetTabLeft;
        if (x <= mouseX && mouseX <= x + PetTabWidth && top <= mouseY && mouseY <= PetTabBottom &&
            movement.pendingClick)
        {
            movement.leftHeld = false;
            movement.pendingClick = false;
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
        mouseY < PetCloseTop + PetCloseSize && movement.pendingClick)
    {
        movement.leftHeld = false;
        movement.pendingClick = false;
        if (auto closed = panels.Close(UI::PetPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    return {};
}

// 0x424130 → 0x424230: con el personaje cerrado, 0x723911 en 0. Abierto, la primera vez el pedido del caballo oscuro
// (C1 06 A9 01 00 08, Q-C16) y 0x723911 en 1; el botón de la mascota abre o cierra el panel 0x400. Con 0x723911 en 2,
// el paso de la ventana de la mascota. 0x416660, desde 0x424130 con 2, no hace nada.
Core::Expected<void, std::string> StepThePetButton(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    std::uint8_t& pet = runtime.panels.delegated.pet;
    if (!panels.IsOpen(UI::CharacterPanel))
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
    GameMovementState& movement = runtime.movement;
    if (InRect(context, PetButtonLeft, PetButtonTop, PetButtonRight, PetButtonBottom) && movement.pendingClick)
    {
        movement.leftHeld = false;
        movement.pendingClick = false;
        auto toggled = panels.IsOpen(UI::PetPanel) ? panels.Close(UI::PetPanel) : panels.Open(UI::PetPanel);
        if (!toggled.HasValue())
            return Core::Unexpected(std::move(toggled).Error());
    }
    if (pet == PetWindowOpen)
        return StepThePetWindow(context, panels);
    return {};
}

// 0x5e5728..0x5e5767: 0x5c4110 con las dos grillas del comercio (0x78a5a20 en y + 0x46 y 0x78a92a8 en y + 0x10e, 8 × 4,
// desde x + 15, con el último argumento en 0). Cada una sale como con el inventario (la espera, la traba de C3 24 o un
// ítem en el cursor) y recorre sus casillas por filas: con el mouse sobre una con un tipo, lo de ese ítem
// (HoverTheGridItem). Levantar o el clic derecho terminan la de la grilla propia; la del otro sigue.
Core::Expected<void, std::string> StepTheTradeGrids(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    TradeState& trade = runtime.social.trade;
    const auto scan = [&](const HoverGrid& grid, std::int32_t top) -> Core::Expected<void, std::string>
    {
        if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
            return {};
        runtime.ui.shopGridHovered = 0;
        const std::int32_t left = runtime.hud.tradeX + 15;
        for (std::int32_t row = 0; row < TradeGridRows; ++row)
        {
            for (std::int32_t column = 0; column < TradeGridColumns; ++column)
            {
                const std::int32_t cellLeft = left + column * CellSize;
                const std::int32_t cellTop = top + row * CellSize;
                const auto index = static_cast<std::size_t>(row * TradeGridColumns + column);
                if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                    context.mouseY >= cellTop + CellSize || grid.cells[index].id == EmptyOwnItemId)
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
    if (auto other = scan(HoverGrid{.cells = trade.otherGrid, .place = UI::HoveredItem::Place::OtherTradeCell},
                          runtime.hud.tradeY + 0x46);
        !other.HasValue())
        return other;
    return scan(HoverGrid{.cells = trade.ownGrid, .place = UI::HoveredItem::Place::OwnTradeCell},
                runtime.hud.tradeY + 0x10e);
}

// 0x5e58ea..0x5e5904 (CL9 hito 7, 08 P-C208 b): 0x5c4110(x + 15, y + 0x6e, 0x78ab0b8, 8, 4, 0) con la esquina de la
// ventana de las tiendas personales (0x78aba58 y 0x78aba5c, que escriben su paso 0x5ea290 con la propia y su dibujo
// 0x5f3030). Sale como las otras y recorre las casillas por filas; levantar o el clic derecho la terminan.
Core::Expected<void, std::string> StepTheShopGrid(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    runtime.ui.shopGridHovered = 0;
    const PersonalShopState& shops = runtime.social.personalShop;
    const std::int32_t left = shops.windowX + ShopGridLeft;
    const std::int32_t top = shops.windowY + ShopGridTop;
    const std::span<World::ItemState> cells(context.world.Inventory().secondCells);
    for (std::int32_t row = 0; row < ShopGridRows; ++row)
    {
        for (std::int32_t column = 0; column < ShopGridColumns; ++column)
        {
            const std::int32_t cellLeft = left + column * CellSize;
            const std::int32_t cellTop = top + row * CellSize;
            const auto index = static_cast<std::size_t>(row * ShopGridColumns + column);
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize || cells[index].id == EmptyOwnItemId)
                continue;
            const auto hovered =
                HoverTheGridItem(context, panels, HoverGrid{.cells = cells, .place = UI::HoveredItem::Place::ShopCell},
                                 index, left, top);
            if (!hovered.HasValue())
                return Core::Unexpected(hovered.Error());
            if (hovered.Value() != GridHover::Stayed)
                return {};
        }
    }
    return {};
}

// 0x5e571c..0x5e5910: las grillas de las otras ventanas, la caja del entrenador (0x44ad10) y el cartel del set
// (0x40e6c0).
Core::Expected<void, std::string> StepTheOtherGrids(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::PanelState& state = runtime.panels;
    if (panels.IsOpen(UI::TradePanel))
    {
        if (auto trade = StepTheTradeGrids(context, panels); !trade.HasValue())
            return trade;
    }
    // 0x5e577d..0x5e589c: 0x5c4110 desde la esquina de la ventana del NPC, la grilla de la tienda con el tipo 1 y la
    // del almacén con el 0, de 8 × 15 en (x + 0xf, y + 0x32), y la caja del caos de 8 × 4 en (x + 0xf, y + 0x6e)
    // con la mezcla fuera de 1.
    NpcWindowState& npc = runtime.npcWindows;
    const std::int32_t npcLeft = runtime.ui.npcWindowX + 0xf;
    const HoverGrid npcGrid{.cells = npc.grid,
                            .place = UI::HoveredItem::Place::NpcGridCell,
                            .rows = static_cast<std::int32_t>(NpcGridCellCount) / NpcGridColumns,
                            .kind = 0,
                            .grid = CursorGrid::NpcGrid};
    if (panels.IsOpen(UI::NpcShopPanel))
    {
        HoverGrid shop = npcGrid;
        shop.kind = ShopGridKind;
        if (auto hovered = StepTheGrid(context, panels, shop, npcLeft, runtime.ui.npcWindowY + 0x32);
            !hovered.HasValue())
            return hovered;
    }
    if (panels.IsOpen(UI::WarehousePanel))
    {
        if (auto hovered = StepTheGrid(context, panels, npcGrid, npcLeft, runtime.ui.npcWindowY + 0x32);
            !hovered.HasValue())
            return hovered;
    }
    if ((panels.IsOpen(UI::ChaosBoxPanel) && npc.chaosMixState != 1) ||
        (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         HeroCanCraftAtTheCastle(context.world)))
    {
        const HoverGrid chaos{.cells = npc.chaosBox,
                              .place = UI::HoveredItem::Place::ChaosBoxCell,
                              .rows = ChaosBoxRows,
                              .kind = 0,
                              .grid = CursorGrid::ChaosBox};
        if (auto hovered = StepTheGrid(context, panels, chaos, npcLeft, runtime.ui.npcWindowY + 0x6e);
            !hovered.HasValue())
            return hovered;
    }
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
    {
        if (auto shop = StepTheShopGrid(context, panels); !shop.HasValue())
            return shop;
    }
    // 0x5e58f2: 0x44ad10, con 0x724580 en 3, 0x5c4110(0x113, 0x6e, 0x723c80, 8, 4, 0).
    if (state.delegated.trainer == TrainerWithBox)
    {
        const HoverGrid trainer{.cells = npc.trainerBox,
                                .place = UI::HoveredItem::Place::TrainerBoxCell,
                                .rows = ChaosBoxRows,
                                .kind = 0,
                                .grid = CursorGrid::TrainerBox};
        if (auto hovered = StepTheGrid(context, panels, trainer, TrainerBoxLeft, TrainerBoxTop); !hovered.HasValue())
            return hovered;
    }
    // 0x40e6c0: el mouse sobre el cartel, sin mirar el clic (Q-C472), prende la ventana del set (+0x9304), que dibujan
    // los tooltips (0x40e850), y gasta el flanco del clic.
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    if (InRect(context, x + 0xe, y + 0x16, x + 0xb0, y + 0x2a))
    {
        context.world.ItemSets().byte9304 = 1;
        runtime.movement.pendingClick = false;
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    }
    return {};
}

// 0x5e4ce0, el inventario. Las otras ventanas con grilla cortan donde se consultan por primera vez.
Core::Expected<void, std::string> StepTheInventoryWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const UI::PanelState& state = runtime.panels;
    const std::int32_t x = runtime.hud.inventoryX;
    const std::int32_t y = runtime.hud.inventoryY;
    // 0x5e4ce0..0x5e4f2c: el mouse sobre la ventana es de la UI, y sobre la del NPC abierta (la tienda, con su
    // rand(), el almacén y la caja del caos), en la esquina de 0x78aba28, también.
    if (InRect(context, x, y, x + 0xbe, y + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    const std::int32_t npcX = runtime.ui.npcWindowX;
    const std::int32_t npcY = runtime.ui.npcWindowY;
    const auto overTheNpcWindow = [&context, &runtime, npcX, npcY]
    {
        if (InRect(context, npcX, npcY, npcX + 0xbe, npcY + 0x1b1))
            runtime.ui.mouseOnUi = 1;
    };
    if (panels.IsOpen(UI::NpcShopPanel))
        overTheNpcWindow();
    if (panels.IsOpen(UI::WarehousePanel))
        overTheNpcWindow();
    if (panels.IsOpen(UI::ChaosBoxPanel) ||
        (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         HeroCanCraftAtTheCastle(context.world)))
        overTheNpcWindow();
    // 0x5e4e34..0x5e4e7b: el mouse sobre la ventana del comercio (0xbe × 0x1b1 desde 0x78a5a1c y 0x78a5a18).
    if (panels.IsOpen(UI::TradePanel) &&
        InRect(context, runtime.hud.tradeX, runtime.hud.tradeY, runtime.hud.tradeX + 0xbe, runtime.hud.tradeY + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    // 0x5e4e76..0x5e4ed0: con una de los NPC de evento, el mouse sobre su esquina (0x78aba28, que ponen su paso y su
    // dibujo) es de la UI.
    if (panels.IsOpen(UI::EventNpcPanel) || panels.IsOpen(UI::SecondEventNpcPanel))
        overTheNpcWindow();
    // 0x5e4ed7..0x5e4f31: el mouse sobre la ventana de las tiendas personales (0xbe × 0x1b1 desde 0x78aba58 y
    // 0x78aba5c).
    const PersonalShopState& shops = runtime.social.personalShop;
    if ((panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel)) &&
        InRect(context, shops.windowX, shops.windowY, shops.windowX + 0xbe, shops.windowY + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    // 0x5e4f31..0x5e5128: los doce lugares del equipo, cada uno con su acceso, y +0x44 en 1 con un tipo.
    for (World::ItemState& item : world.OwnCharacter().equipment)
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        item.warning = item.id == EmptyOwnItemId ? std::uint8_t{0} : std::uint8_t{1};
    }
    // 0x5e512f..0x5e5207: lo mismo en las grillas: el inventario (0x78a9bb0), la del otro en el comercio (0x78a5a20,
    // 0x5e515d..0x5e5185, que conserva el 99 de 0x5bc470), la propia (0x78a92a8), la de la tienda y del almacén
    // (0x78a70b4..), la caja del caos (0x7d2d3c4..) y la de las tiendas personales (0x78ab0b8, CL9 hito 7); 0x449c60
    // (0x5e5207) hace lo mismo con la caja del entrenador (0x723c80).
    const auto warn = [](std::span<World::ItemState> cells)
    {
        for (World::ItemState& cell : cells)
            cell.warning = cell.id == EmptyOwnItemId ? std::uint8_t{0} : std::uint8_t{1};
    };
    warn(world.Inventory().cells);
    TradeState& trade = runtime.social.trade;
    for (World::ItemState& cell : trade.otherGrid)
    {
        if (cell.id == EmptyOwnItemId)
            cell.warning = 0;
        else if (cell.warning != ChangedFromTheCopy)
            cell.warning = 1;
    }
    warn(trade.ownGrid);
    warn(runtime.npcWindows.grid);
    warn(runtime.npcWindows.chaosBox);
    warn(world.Inventory().secondCells);
    warn(runtime.npcWindows.trainerBox);
    runtime.ui.field78abaac = 0;
    // 0x5e520c..0x5e5256: arriba de y + 200 (_ftol de y + 200f), el equipo; si no, la grilla.
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

// 0x5ee1b1..0x5ee1ca: las seis hermanas de 0x5e4ce0, cada una con su IsOpen: los NPC de evento, la caja del caos
// (0x5fe610), el almacén (0x5ea060), el comercio, las tiendas personales y la tienda de NPC (0x5ed960, con su rand()).
Core::Expected<void, std::string> StepTheInventorySiblings(const GameUiContext& context, UI::PanelRegistry& panels)
{
    // 0x5e5920: los NPC de evento (CL9 hito 11).
    if (auto events = StepTheEventNpcWindow(context, panels); !events.HasValue())
        return events;
    if (auto chaos = StepTheChaosBox(context, panels); !chaos.HasValue())
        return chaos;
    if (auto warehouse = StepTheWarehouse(context, panels); !warehouse.HasValue())
        return warehouse;
    if (auto trade = StepTheTradeWindow(context, panels); !trade.HasValue())
        return trade;
    // 0x5ea290, que empieza con sus IsOpen (CL9 hito 7).
    if (auto shop = StepThePersonalShopWindow(context, panels); !shop.HasValue())
        return shop;
    return StepTheNpcShop(context, panels);
}

// 0x5ee140: los paneles con paso propio, sin diálogo y sin el menú de Esc.
// 0x5d8d50: el ítem del cursor, desde 0x5ee1de con 0x78abaac en 0. Con la caja del caos abierta, lo que vale y el
// tipo de la mezcla (0x5d8db6, Scenes::ScanTheChaosBox); la lista del asedio con el inventario corta al principio
// (sin rand(), CL9.5); con la traba de C3 24, nada. Después las respuestas de los avisos de 0x78abb14 (de 1 a 6, los
// de las ventanas de NPC con Scenes::AnswerTheNpcWarning) y del diálogo del precio (0x78aba60 en 2), que pueden
// terminar el paso (StepTheWarningAnswers, CL9 hito 7). Sin ninguno sigue en 0x5deac5 con esi en 6 (0x5da592): con un
// ítem, soltarlo en la grilla del inventario (0x5ca250), en la del comercio, en la del almacén, en la caja del caos, en
// la de la tienda personal o en la del entrenador y, con el clic fuera de ellas, venderlo en la tienda de NPC o tirarlo
// al piso.
Core::Expected<void, std::string> StepTheCursorItem(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::PanelState& state = runtime.panels;
    // 0x5d8d72..0x5d8ddf.
    if (panels.IsOpen(UI::ChaosBoxPanel) ||
        (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         HeroCanCraftAtTheCastle(context.world)))
    {
        if (auto scanned = ScanTheChaosBox(context.world, context.random, ServicesOf(context).items);
            !scanned.HasValue())
            return scanned;
    }
    // 0x5d8ddf..0x5d8dee.
    if (runtime.ui.field78abb39 != 0)
        return {};
    const InventoryMoveContext moves = MovesOf(context);
    // 0x5d8dee..0x5dc798 y 0x5deaa7..0x5deac0.
    const auto answered = StepTheWarningAnswers(context, moves);
    if (!answered.HasValue())
        return Core::Unexpected(answered.Error());
    if (!answered.Value())
        return {};
    // 0x5deac5..0x5deacc.
    if (runtime.cursorItemCount <= 0)
        return {};
    // 0x5dead2..0x5deb0e: 0x5ca250(x + 15, y + 200, 0x78a9bb0, 8, 8, 0).
    const auto inGrid = DropOnTheInventoryGrid(moves, GridDrop{.mouseX = context.mouseX,
                                                               .mouseY = context.mouseY,
                                                               .left = runtime.hud.inventoryX + 15,
                                                               .top = runtime.hud.inventoryY + 200,
                                                               .inventoryOpen = panels.IsOpen(UI::InventoryPanel),
                                                               .panels = &panels});
    if (!inGrid.HasValue())
        return Core::Unexpected(inGrid.Error());
    // 0x5deb11..0x5dec41: las otras grillas. La lista del asedio ya cortó arriba.
    bool inAGrid = inGrid.Value();
    if (panels.IsOpen(UI::TradePanel))
    {
        // 0x5deb1c..0x5deb43: 0x5ca250(x + 15, y + 0x10e, 0x78a92a8, 8, 4, 1), con edi en 1 desde 0x5d8df3.
        const auto inTrade = DropOnTheTradeGrid(moves, GridDrop{.mouseX = context.mouseX,
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
    if (panels.IsOpen(UI::WarehousePanel))
    {
        // 0x5deb57: 0x5ca250(x + 0xf, y + 0x32, 0x78a7070, 8, 0xf, 2).
        const auto dropped = DropOnOtherGrid(moves, OtherGridDrop{.mouseX = context.mouseX,
                                                                  .mouseY = context.mouseY,
                                                                  .left = npcLeft,
                                                                  .top = runtime.ui.npcWindowY + 0x32,
                                                                  .grid = CursorGrid::NpcGrid});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inAGrid = inAGrid || dropped.Value();
    }
    if ((panels.IsOpen(UI::ChaosBoxPanel) && runtime.npcWindows.chaosMixState != 1) ||
        (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory &&
         HeroCanCraftAtTheCastle(context.world)))
    {
        // 0x5debe4: 0x5ca250(x + 0xf, y + 0x6e, 0x7d2d380, 8, 4, 3).
        const auto dropped = DropOnOtherGrid(moves, OtherGridDrop{.mouseX = context.mouseX,
                                                                  .mouseY = context.mouseY,
                                                                  .left = npcLeft,
                                                                  .top = runtime.ui.npcWindowY + 0x6e,
                                                                  .grid = CursorGrid::ChaosBox});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inAGrid = inAGrid || dropped.Value();
    }
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
    {
        // 0x5debf0..0x5dec1e: 0x5ca250(x + 15, y + 0x6e, 0x78ab0b8, 8, 4, 4), con bl en lo que devuelve (CL9 hito 7).
        const PersonalShopState& shops = runtime.social.personalShop;
        const auto inShop = DropOnTheShopGrid(moves, GridDrop{.mouseX = context.mouseX,
                                                              .mouseY = context.mouseY,
                                                              .left = shops.windowX + ShopGridLeft,
                                                              .top = shops.windowY + ShopGridTop,
                                                              .inventoryOpen = false,
                                                              .panels = nullptr});
        if (!inShop.HasValue())
            return Core::Unexpected(inShop.Error());
        inAGrid = inAGrid || inShop.Value();
    }
    // 0x5dec41: 0x44aca0, con 0x724580 en 3, 0x5ca250(0x113, 0x6e, 0x723c80, 8, 4, 5).
    if (state.delegated.trainer == TrainerWithBox)
    {
        const auto dropped = DropOnOtherGrid(moves, OtherGridDrop{.mouseX = context.mouseX,
                                                                  .mouseY = context.mouseY,
                                                                  .left = TrainerBoxLeft,
                                                                  .top = TrainerBoxTop,
                                                                  .grid = CursorGrid::TrainerBox});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inAGrid = inAGrid || dropped.Value();
    }
    // 0x5dec46..0x5deca1: el clic, consumido, y nada más si quedó en una grilla.
    if (!runtime.movement.pendingClick)
        return {};
    runtime.movement.pendingClick = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    if (inAGrid)
        return {};
    // 0x5deca7..0x5df448: la tienda de NPC, con su rand(); con el mouse en su grilla, venderlo.
    const auto sold = SellTheCursorItem(context, panels);
    if (!sold.HasValue())
        return Core::Unexpected(sold.Error());
    if (sold.Value())
        return {};
    return DropOnTheGround(
        moves,
        GroundDrop{.mouseX = context.mouseX, .inventoryOpen = panels.IsOpen(UI::InventoryPanel), .panels = &panels});
}

Core::Expected<void, std::string> StepThePanels(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    if (runtime.dialogs.current == UI::EscapeMenuDialog)
        return {};
    // 0x5e7bf0: con 0x78abb20 en 0 devuelve 0; si no, la caja de la contraseña y 1, y 0x5ee140 no sigue.
    auto keypad = StepTheKeypad(context, panels);
    if (!keypad.HasValue())
        return Core::Unexpected(std::move(keypad).Error());
    if (keypad.Value())
        return {};
    // 0x5ee15a..0x5ee188.
    if (runtime.dialogs.current != 0 || ui.capture != 0 || ui.hovered != 0 || ui.field78abad9 != 0)
        return {};
    // 0x5e3c80 y 0x5e4510: la party y el personaje, cada uno con su IsOpen.
    if (auto party = StepThePartyWindow(context, panels); !party.HasValue())
        return party;
    if (auto character = StepTheCharacterWindow(context, panels); !character.HasValue())
        return character;
    // 0x5ee1a3: el inventario (0x5e4ce0) y sus seis hermanas (0x5e5920, 0x5fe610, 0x5ea060, 0x5ecde0, 0x5ea290 y
    // 0x5ed960).
    if (panels.IsOpen(UI::InventoryPanel))
    {
        if (auto inventory = StepTheInventoryWindow(context, panels); !inventory.HasValue())
            return inventory;
        if (auto siblings = StepTheInventorySiblings(context, panels); !siblings.HasValue())
            return siblings;
    }
    // 0x44b4e0: el entrenador, por 0x724580.
    if (auto trainer = StepTheTrainer(context, panels); !trainer.HasValue())
        return trainer;
    // 0x44dd50 y 0x44f810: el arma de asedio y la puerta.
    if (auto siege = StepTheSiegeControls(context); !siege.HasValue())
        return siege;
    // 0x5ee1de: 0x5d8d50, el ítem del cursor.
    if (ui.field78abaac == 0)
    {
        if (auto cursor = StepTheCursorItem(context, panels); !cursor.HasValue())
            return cursor;
    }
    // 0x5ee1ec: 0x5e6520, con 0x78abb00 distinto de 0.
    if (runtime.panels.field78abb00 != 0)
    {
        if (auto chips = StepTheEventChipWindow(context, panels); !chips.HasValue())
            return chips;
    }
    // 0x5e74f0, el panel 0x2000.
    if (auto stepped = StepThePanel2000(context, panels); !stepped.HasValue())
        return stepped;
    // 0x40a450, los comandos ([0x6e592c]), que empieza con su +4.
    if (auto commands = StepTheCommandsWindow(context); !commands.HasValue())
        return commands;
    // 0x5ee20d (CL9 hito 2): 0x41b5a0, la ventana de las misiones y su botón en la del personaje.
    if (auto quest = StepTheQuestWindow(context, panels); !quest.HasValue())
        return quest;
    // 0x5ee212: 0x424130.
    return StepThePetButton(context, panels);
}

// La skill `index` del tooltip y del clic de la lista (0x5a08a1..0x5a08f3) y de los botones de las órdenes del
// cuervo (0x420ee4..0x420f4a): el tooltip, el mouse en la UI y, con el clic, la skill del héroe, la lista cerrada, el
// sonido 0x19 y el reloj del mouse.
void PointAtTheSkill(const GameUiContext& context, std::uint8_t index, std::int32_t tooltipX, std::int32_t tooltipY)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    ui.skillTooltipX = tooltipX;
    ui.skillTooltip = index;
    ui.skillTooltipY = tooltipY;
    ui.mouseOnUi = 1;
    if (!runtime.movement.pendingClick)
        return;
    runtime.movement.pendingClick = false;
    context.world.Hero().skillIndex = index;
    ui.skillListOpen = 0;
    runtime.RequestSound(ButtonSound);
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = 6;
}

// 0x420e90: con el cuervo (+0x430), los cuatro botones de sus órdenes (las skills 0x78 a 0x7b), de 0x20 desde x 0x100,
// con y de 0x14a a 0x16e.
void StepThePetOrderButtons(const GameUiContext& context)
{
    const ActorState& hero = context.world.Hero();
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

// Las teclas de Ctrl + número sobre la skill `index` de la lista (0x5a08fd..0x5a106e): por cada número abajo, del 1 al
// 9 y el 0, con el registro propio abierto, saca ese número de las 64 teclas del lugar del elegido (0x6d5edc) y se lo
// da a la skill.
void StepTheSkillKeys(const GameUiContext& context, std::uint8_t index)
{
    const Input::InputState& input = context.input;
    if (!input.IsDown(Key::Ctrl))
        return;
    WorldRuntime& runtime = context.world.Runtime();
    OwnCharacterRecord& own = context.world.OwnCharacter();
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

// 0x5a070e..0x5a1098: la lista abierta. Por cada una de las 64 skills que hay, salvo las 0x43 a 0x48 y las que tienen
// el +0x31 de Skill.bmd en 1 (con su entrada abierta y cerrada: 60 rand() si la cierra en 0), un ícono de 32 × 42 en
// filas de hasta 20 centradas en x 320 desde y 0x172, la segunda 0x34 más arriba. Bajo el mouse, su tooltip y su clic
// (PointAtTheSkill) y las teclas (StepTheSkillKeys). Después, los botones de las órdenes del cuervo (0x420e90).
Core::Expected<void, std::string> StepTheSkillList(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    constexpr std::int32_t RowSize = 0x14;
    const std::int32_t width =
        std::min(static_cast<std::int32_t>(own.byte6a) - static_cast<std::int32_t>(own.byte6b), RowSize);
    std::int32_t shown = 0;
    std::int32_t column = 0;
    std::int32_t y = 0x172;
    constexpr std::uint8_t ListedSlots = 0x40;
    for (std::uint8_t index = 0; index < ListedSlots; ++index)
    {
        const std::uint8_t skill = OwnSkillByte(own, index);
        if (skill == 0 || (skill >= 0x43 && skill <= 0x48))
            continue;
        if (context.skills == nullptr)
            return Core::Unexpected(std::string("0x5a07a3: la lista de skills lee Skill.bmd y MuRet no lo cargó"));
        const auto record = context.skills->Record(skill);
        if (!record)
            return Core::Unexpected(std::format("0x5a07a3: Skill.bmd no tiene la skill {}", skill));
        std::uint8_t hidden = 0;
        {
            const Models::LogicalAccess entry(runtime.movement.skillDepths[skill], context.random,
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
        // 0x5a084f..0x5a085f: el CDQ/SAR divide hacia el 0.
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

// 0x420e30: el Dark Lord con Shift y el 1 al 4 elige la orden del cuervo (las skills 0x78 a 0x7b), con el primero
// abajo. Dice si eligió.
bool StepThePetOrderKeys(const GameUiContext& context)
{
    ActorState& hero = context.world.Hero();
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

// 0x59f810(número): con el registro propio abierto, la primera de las 64 skills con esa tecla en el lugar del elegido
// que no sea de las 0x43 a 0x48 pasa a ser la del héroe; si no hay, la primera de las órdenes del cuervo (0x78 a 0x7b)
// con la tecla. En cada vuelta sin elegir, con el ataque automático fuera de los mapas 6 y 18 a 23 y la skill del héroe
// en 6 o 0xf, los elegidos 0x6c8e30 y 0x6c8e38 vuelven a −1. Dice si eligió.
bool ChooseTheSkillByKey(const GameUiContext& context, std::uint8_t number)
{
    WorldRuntime& runtime = context.world.Runtime();
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    ActorState& hero = context.world.Hero();
    const Models::LogicalAccess access(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const std::size_t place = context.chosenSlot.value_or(std::uint8_t{0});
    if (place >= own.skillKeys.size())
        return false;
    const auto& keys = own.skillKeys[place];
    constexpr std::uint8_t KeyedSlots = 0x40;
    for (std::uint8_t slot = 0; slot < KeyedSlots; ++slot)
    {
        const std::uint8_t skill = OwnSkillByte(own, slot);
        if (skill != 0 && keys[slot] == number && (skill < 0x43 || skill > 0x48))
        {
            hero.skillIndex = slot;
            return true;
        }
        const std::int32_t map = runtime.map;
        if (runtime.ui.autoAttack && map != 6 && !(map >= 0x12 && map <= 0x17))
        {
            const std::uint8_t chosen = OwnSkillByte(own, hero.skillIndex);
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

// 0x5a05e5..0x5a10e2: la barra de skills. Abre el registro propio aunque no haya skills y lo cierra al final.
Core::Expected<void, std::string> StepTheSkillBar(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    if (context.world.OwnCharacter().byte6a == 0)
        return {};
    // 0x5a066c..0x5a06f9: el botón de la barra.
    if (InRect(context, 0x130, 0x1bc, 0x150, 0x1e0))
    {
        ui.skillTooltipX = 0x140;
        ui.skillTooltip = context.world.Hero().skillIndex;
        ui.skillTooltipY = 0x1bc;
        ui.mouseOnUi = 1;
        runtime.movement.mouseUpdateTime = 0;
        if (runtime.movement.pendingClick)
        {
            runtime.movement.pendingClick = false;
            ui.skillListOpen = ui.skillListOpen == 0 ? 1 : 0;
            runtime.RequestSound(ButtonSound);
        }
    }
    // 0x652330: sin el asedio (0x4294b0) no hace nada.
    if (auto siege = StepTheBattleSkill(context); !siege.HasValue())
        return siege;
    if (ui.skillListOpen != 0)
        return StepTheSkillList(context);
    return {};
}

// 0x59ff45..0x5a009b: el clic derecho sobre un renglón del área vieja (0x77d9868, desde 0x6c8eb4) que se dibujó (con
// texto y, sin F2, que no sea del chat normal), de otro, con el chat abierto y el héroe en la zona segura: su nombre al
// buffer del susurro, sin el EDIT (Q-C530), y el 0x19. El ancho del renglón es el que dejó su dibujo, en píxeles.
void StepTheOldAreaRows(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const ActorState& hero = context.world.Hero();
    const UI::MessageAreaState& area = runtime.chat.area;
    for (std::int32_t y = 0, shown = 0; y < OldAreaBottom; y += OldAreaRowHeight, ++shown)
    {
        const std::int32_t index = area.firstShown + shown;
        if (index < 0 || index >= static_cast<std::int32_t>(UI::MessageAreaRows))
            continue;
        const UI::MessageAreaRow& row = area.rows[static_cast<std::size_t>(index)];
        if (row.text.empty() || (!ui.chatModes.showChat && row.type == UI::ChatMessage))
            continue;
        const std::uint32_t width =
            static_cast<std::uint32_t>(runtime.hud.messageFrame.widths[static_cast<std::size_t>(index)]);
        const std::uint32_t right =
            context.screen.width > 0 ? width * 640u / static_cast<std::uint32_t>(context.screen.width) : width;
        if (context.mouseX < 0 || context.mouseX >= static_cast<std::int32_t>(right) || context.mouseY < y ||
            context.mouseY >= y + OldAreaRowHeight || !ui.chatOpen || hero.safeZone == 0)
            continue;
        if (row.name == HeroNameOf(hero) || !ui.rightClick)
            continue;
        ui.rightClick = false;
        UI::PutTheWhisperName(runtime.chat.input, row.name);
        runtime.RequestSound(ButtonSound);
    }
}

// 0x5a0124..0x5a03c3: arriba (−1) y abajo (+1). Con el foco en uno de los EDIT del chat y el campo par, el historial
// (la vtable + 0xc de 0x57634f8); si no, el nombre del susurro del lugar de antes o del que sigue (0x6c8ea8).
void StepTheArrowKey(const GameUiContext& context, GameKey key, std::int32_t direction)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    if (!KeyEdge(runtime.ui, context.input, key))
        return;
    bool sound = false;
    if ((input.focus == UI::ChatFocus::Message || input.focus == UI::ChatFocus::Whisper) && input.field % 2 == 0)
        sound = UI::RecallFromTheHistory(input, direction);
    else
        sound = UI::StepTheWhisperName(input, direction, &runtime.ui.focusedList);
    if (sound)
        runtime.RequestSound(ButtonSound);
}

// 0x45aa80(0) y 0x468d50 sobre un EDIT del chat: el mouse sobre él lo hace la ventana de abajo (si nadie agarró el
// mouse o lo agarró él) y, dentro de (x, y − 4, w, h + 8), es de la UI; con el clic, el EDIT toma el foco
// (0x4682a0(1)). Las flechas y la barra que 0x468d50 mira con el clic (EM_SCROLL, la barra agarrada) no se portan
// (P-C86). Devuelve lo de 0x468d50: el mouse en el rectángulo grande.
bool StepTheChatEdit(const GameUiContext& context, const ChatEditPlace& edit)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    if (InRect(context, edit.x, edit.y, edit.x + edit.width, edit.y + edit.height))
    {
        if (ui.hovered == 0)
        {
            if (ui.capture != 0 && ui.capture != edit.id)
                return false;
            ui.hovered = edit.id;
        }
        // +0x48 en 0, a la vista. Con el izquierdo abajo, la lista de las ventanas viejas (0x490aa0, hito 7): los EDIT
        // del chat no tienen padre.
        if (ui.hovered != edit.id && ui.capture != edit.id)
            return false;
    }
    if (!InRect(context, edit.x, edit.y - 4, edit.x + edit.width, edit.y + edit.height + 4))
        return false;
    ui.mouseOnUi = 1;
    if (runtime.movement.pendingClick)
    {
        UI::ClickTheChatField(runtime.chat.input, edit.field, &ui.focusedList);
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    }
    return true;
}

// 0x5a32dd..0x5a343c: con un campo de texto, los EDIT del chat (0x469500: el del mouse es el campo elegido) y el
// botón de F3 (0x6c8dd0).
void StepTheChatButtons(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    for (const ChatEditPlace& edit : ChatEdits)
    {
        if (StepTheChatEdit(context, edit))
            runtime.chat.input.field = edit.field;
    }
    if (!InRect(context, WhisperModeLeft, WhisperModeTop, WhisperModeRight, WhisperModeBottom) ||
        !runtime.movement.pendingClick)
        return;
    runtime.movement.pendingClick = false;
    runtime.chat.input.whisperMode = !runtime.chat.input.whisperMode;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    runtime.RequestSound(ButtonSound);
}

// 0x5a116f..0x5a2b47: con el freno de las macros en 0 (0x77f3b74), fuera de Chaos Castle y con Alt, las macros de
// los números apretados (GetAsyncKeyState, no el flanco).
Core::Expected<void, std::string> StepTheMacros(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.hud.macroTime != 0 || (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap) ||
        !context.input.IsDown(Key::Alt))
        return {};
    std::array<bool, UI::MacroCount> pressed{};
    bool any = false;
    for (std::size_t macro = 0; macro < UI::MacroCount; ++macro)
    {
        pressed[macro] = context.input.IsDown(Digit(static_cast<int>((macro + 1) % UI::MacroCount)).key);
        any = any || pressed[macro];
    }
    if (!any)
        return {};
    if (context.chat == nullptr)
        return Core::Unexpected(std::string("0x5a11cb: las macros sin la entrada del chat"));
    return SayTheMacros(*context.chat, pressed);
}

// 0x59ff10: las teclas y los botones del HUD.
Core::Expected<void, std::string> StepTheKeysAndButtons(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    GameMovementState& movement = runtime.movement;
    const Input::InputState& input = context.input;
    const ActorState& hero = context.world.Hero();
    // 0x59ff32..0x59ff3f: con el recorrido de la cámara (0x4033d0), nada.
    if (CameraTourRunning(runtime.events.cameraTour))
        return {};
    StepTheOldAreaRows(context);
    // 0x5a00a1..0x5a0124: PgUp y PgDn corren el registro del chat tantos renglones como muestra (la vtable + 0x38 los
    // da y la + 0x34 corre).
    if (KeyEdge(ui, input, PageUpKey))
        UI::ScrollTheLog(runtime.chat.log, ui.chatModes, -runtime.chat.log.rows);
    if (KeyEdge(ui, input, PageDownKey))
        UI::ScrollTheLog(runtime.chat.log, ui.chatModes, runtime.chat.log.rows);
    StepTheArrowKey(context, UpKey, -1);
    StepTheArrowKey(context, DownKey, 1);
    // 0x5a03c3..0x5a05e5: los botones del HUD, cada uno con su rectángulo: abierto lo cierra y si no lo abre.
    if (movement.pendingClick)
    {
        struct HudButton
        {
            UI::PanelId id;
            std::int32_t left, top, right, bottom;
        };
        constexpr std::array<HudButton, 6> Buttons{{
            {UI::GuildPanel, 0x246, 0x1cb, 0x27a, 0x1dd},
            {UI::PartyPanel, 0x15c, 0x1c4, 0x174, 0x1dc},
            {UI::CharacterPanel, 0x17b, 0x1c4, 0x193, 0x1dc},
            {UI::InventoryPanel, 0x19a, 0x1c4, 0x1b2, 0x1dc},
            {UI::FriendsPanel, 0x246, 0x1b3, 0x27a, 0x1c5},
            {UI::CommandsPanel, 6, 0x1b2, 0x3b, 0x1c5},
        }};
        for (const HudButton& button : Buttons)
        {
            if (!InRect(context, button.left, button.top, button.right, button.bottom))
                continue;
            movement.pendingClick = false;
            if (auto toggled = Toggle(panels, button.id); !toggled.HasValue())
                return toggled;
        }
    }
    if (auto bar = StepTheSkillBar(context); !bar.HasValue())
        return bar;
    // 0x5a10e7..0x5a116f: los números eligen la skill (0x59f810), sin el diálogo 0x73 (0x5a10f0, con 0x6c7b8c en 1:
    // ni mira el foco), con el foco en el juego, sin campos de texto, sin una skill que se carga (0x5a1115, 0x7d368a4
    // en 0), sin las órdenes del cuervo (0x420e30, Shift con 1 a 4) y sin Alt.
    if (runtime.dialogs.current != MoneyDialog && runtime.chat.input.focus == UI::ChatFocus::Game && !ui.chatOpen &&
        !runtime.chat.input.otherInput && ui.rightHoldFrames == 0)
    {
        if (!StepThePetOrderKeys(context) && !input.IsDown(Key::Alt))
        {
            // 0x5a1139..0x5a116a: del 1 al 9 y después el 0, cada uno que esté abajo.
            for (int digit = 1; digit <= 10; ++digit)
            {
                if (input.IsDown(Digit(digit % 10).key))
                    static_cast<void>(ChooseTheSkillByKey(context, static_cast<std::uint8_t>(digit % 10)));
            }
        }
    }
    if (auto macros = StepTheMacros(context); !macros.HasValue())
        return macros;
    // 0x5a2b47..0x5a32dd: el clic derecho en las tres ranuras rápidas, si el héroe no se está muriendo. Una ranura
    // sin su ítem en el inventario (0x558110 da −1) sigue con la que viene.
    const std::span<const World::ItemState, InventoryCellCount> cells(context.world.Inventory().cells);
    if (hero.dying == 0 && ui.rightClick)
    {
        for (std::int32_t slot = 0; slot < 3; ++slot)
        {
            const std::int32_t left = 0xd0 + 0x1f * slot;
            if (!ui.rightClick || !InRect(context, left, 0x1c4, left + 0x14, 0x1d8))
                continue;
            ui.rightClick = false;
            auto found =
                FindQuickSlotItem(runtime.hud.quickSlots, cells, slot, runtime.movement.gateRequestBlocked != 0);
            if (!found.HasValue())
                return Core::Unexpected(found.Error());
            // 0x5a2bc8..0x5a32cc: usarlo (CL9 hito 5) y salir de las ranuras.
            if (found.Value())
            {
                if (auto used = UseInventoryItem(MovesOf(context), panels, static_cast<std::int32_t>(*found.Value()));
                    !used.HasValue())
                    return used;
                break;
            }
        }
    }
    if (TextInputBusy(runtime))
        StepTheChatButtons(context);
    // 0x5a343e..0x5a359a: Q, W y E solo sin campo de texto, con todo esto cerrado y el héroe vivo. Las consultas
    // del comercio y de la tienda de NPC sacan un rand() cada una en cada paso. 0x77f3b68, entre 0x40000 y el
    // diálogo (0x5a34c3), ya lo miró 0x6578e0, la primera.
    if (!TextInputBusy(runtime) && !panels.IsOpen(UI::TradePanel) && !panels.IsOpen(UI::WarehousePanel) &&
        !panels.IsOpen(UI::NpcShopPanel) && !panels.IsOpen(UI::ChaosBoxPanel) && !panels.IsOpen(UI::SecondGuildPanel) &&
        runtime.dialogs.current == 0 && runtime.panels.field78abb00 == 0 && !panels.IsOpen(UI::QuestPanel) &&
        !panels.IsOpen(UI::EventNpcPanel) && !panels.IsOpen(UI::SecondEventNpcPanel) && !panels.IsOpen(UI::Panel2000) &&
        !panels.IsOpen(UI::PersonalShopPanel) && !panels.IsOpen(UI::OtherShopPanel) &&
        !panels.IsOpen(UI::TrainerPanel) && hero.dying == 0)
    {
        // 0x5a35a6, 0x5a3d2a y 0x5a44b1: la ranura 0, 1 y 2. Sin su ítem en el inventario, la tecla que sigue.
        constexpr std::array<char, 3> QuickSlotLetters{'Q', 'W', 'E'};
        for (int slot = 0; slot < 3; ++slot)
        {
            const char letter = QuickSlotLetters[static_cast<std::size_t>(slot)];
            if (!KeyEdge(ui, input, Letter(letter)))
                continue;
            auto found =
                FindQuickSlotItem(runtime.hud.quickSlots, cells, slot, runtime.movement.gateRequestBlocked != 0);
            if (!found.HasValue())
                return Core::Unexpected(found.Error());
            // 0x5a35e0.., 0x5a3d64.. y 0x5a44eb..: usarlo (CL9 hito 5); la tecla que sigue se lee igual.
            if (found.Value())
            {
                if (auto used = UseInventoryItem(MovesOf(context), panels, static_cast<std::int32_t>(*found.Value()));
                    !used.HasValue())
                    return used;
            }
        }
    }
    // 0x5a4c6e..0x5a5504: las letras de los paneles, B y R, sin campo de texto, sin diálogo y con la lista de CS
    // cerrada. La primera que se apretó hace lo suyo y las que siguen no se leen en este paso.
    if (!TextInputBusy(runtime) && runtime.dialogs.current == 0 && runtime.panels.delegated.castleSiegeList == 0)
    {
        struct PanelLetter
        {
            char letter;
            UI::PanelId id;
        };
        constexpr std::array<PanelLetter, 9> Letters{{
            {'G', UI::GuildPanel},
            {'P', UI::PartyPanel},
            {'T', UI::QuestPanel},
            {'C', UI::CharacterPanel},
            {'I', UI::InventoryPanel},
            {'V', UI::InventoryPanel},
            {'F', UI::FriendsPanel},
            {'M', UI::MovePanel},
            {'D', UI::CommandsPanel},
        }};
        bool done = false;
        for (const PanelLetter& letter : Letters)
        {
            if (!KeyEdge(ui, input, Letter(letter.letter)))
                continue;
            // 0x5a4ea6: la cola común.
            if (auto toggled = Toggle(panels, letter.id); !toggled.HasValue())
                return toggled;
            done = true;
            break;
        }
        UI::PanelState& state = runtime.panels;
        // 0x5a4ec2..0x5a4f2e: B, con la tienda de NPC abierta y 0x78abb09, el modo de reparar de la tienda.
        if (!done && KeyEdge(ui, input, Letter('B')))
        {
            done = true;
            if (panels.IsOpen(UI::NpcShopPanel) && state.field78abb09 != 0 && runtime.cursorItemCount == 0)
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
        // 0x5a4f34..0x5a54e6: R, el modo de reparar con el inventario (nivel 50 o más).
        if (!done && KeyEdge(ui, input, Letter('R')))
        {
            std::uint16_t level = 0;
            {
                const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
                level = context.world.OwnCharacter().level;
            }
            constexpr std::uint16_t RepairLevel = 0x32;
            bool toggle = panels.IsOpen(UI::NpcShopPanel) && state.field78abb09 != 0 && runtime.cursorItemCount == 0;
            if (!toggle && !panels.IsOpen(UI::TradePanel) && !panels.IsOpen(UI::NpcShopPanel) &&
                panels.IsOpen(UI::InventoryPanel) && level >= RepairLevel && !panels.IsOpen(UI::WarehousePanel) &&
                !panels.IsOpen(UI::ChaosBoxPanel) && !panels.IsOpen(UI::EventNpcPanel) && state.field78abb00 == 0)
                toggle = true;
            if (toggle)
            {
                // 0x5a54c8..0x5a54e1.
                state.repairMode = state.repairMode == 0 ? 1 : 0;
                state.repairCursor = state.repairMode != 0 ? 2 : 0;
            }
        }
    }
    // 0x5a5504: 0x6529e0, el asedio con la rueda o el mouse en su rectángulo; sin el asedio (0x4294b0) no hace
    // nada.
    if (auto siege = StepTheBattleSkillChoice(context); !siege.HasValue())
        return siege;
    // 0x5a5511: sin ningún panel abierto, el paso del panel del asedio (0x4125a0). 0x413c90 no hace nada.
    if (!panels.IsOpen(UI::AnyPanel))
        return StepTheBattlePanel(context);
    return {};
}

} // namespace

bool GameTextInputBusy(const WorldRuntime& runtime)
{
    return TextInputBusy(runtime);
}

bool OldWindowKeyEdge(UI::GameUiState& ui, const Input::InputState& input, std::uint8_t virtualKey)
{
    for (const GameKey& key : OldWindowEdgeKeys)
    {
        if (key.virtualKey == virtualKey)
            return KeyEdge(ui, input, key);
    }
    return false;
}

// 0x4b3961: un DOWN nuevo rearma held aunque la UI lo hubiera consumido y UP+DOWN lleguen en un mismo pump.
void LatchTheLeftButton(GameMovementState& state, bool pressed, bool down)
{
    // WM_LBUTTONDBLCLK no toca 0x7d36874 (0x4b4444..0x4b47b6), que solo escriben WM_LBUTTONDOWN (0x4b3961) y
    // WM_LBUTTONUP (0x4b3cf1): con el segundo apretón de un doble clic como el último, `down` (el de los mensajes,
    // Input::MouseMessageState) queda en false y el botón queda arriba para el juego mientras se sostiene, hasta el
    // próximo WM_LBUTTONDOWN.
    if (!down)
        state.leftHeld = false;
    else if (pressed || !state.previousLeftDown)
        state.leftHeld = true;
    state.previousLeftDown = down;
    // WM_LBUTTONDOWN prende el flanco con el botón arriba (0x4b395a) y WM_LBUTTONUP lo apaga (0x4b3ce3): un clic
    // apretado y soltado en el mismo paso no queda, como después de los dos mensajes.
    state.pendingClick = down && (state.pendingClick || pressed);
}

void LatchTheLeftRelease(GameMovementState& state, bool pressed, bool released, bool down, std::int32_t mouseX,
                         std::int32_t mouseY)
{
    if (state.releasedClick && (state.releaseX != mouseX || state.releaseY != mouseY))
        state.releasedClick = false;
    // Apretado después del soltado, el botón queda abajo: el WM_LBUTTONDOWN fue el último.
    if (pressed && (!released || down))
    {
        state.releasedClick = false;
        return;
    }
    if (!released)
        return;
    state.releasedClick = true;
    state.releaseX = mouseX;
    state.releaseY = mouseY;
}

void LendTheGameHooks(UI::OldWindowStepView& view, const GameUiContext& context, bool effects)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    view.keyDown = ArrowKeysDown(context.input);
    // CL9 hito 10: el filtro de las malas palabras de las cartas y del chat con un amigo (0x622b40 con 0).
    if (const GameChatContext* chatContext = context.chat; chatContext != nullptr)
        view.badWord = [filter = chatContext->filter](std::string_view text)
        { return UI::ContainsAFilteredWord(text, filter, false); };
    view.heroName = std::string(HeroNameOf(world.Hero()));
    view.field78abaac = runtime.ui.field78abaac;
    if (!effects)
    {
        view.whisperTo = [](std::string_view /*name*/) {};
        view.replayLetter = [](const std::vector<std::byte>& /*copy*/) -> Core::Expected<void, std::string>
        { return {}; };
        view.portrait = [](UI::OldPortraitPart& /*part*/, UI::PortraitCallKind /*call*/,
                           std::int32_t /*delta*/) -> Core::Expected<void, std::string> { return {}; };
        return;
    }
    // 0x4694c0(0, 0, 1, nombre).
    view.whisperTo = [&chat = runtime.chat.input](std::string_view name)
    { UI::SetTheChatFields(chat, std::nullopt, name); };
    // 0x4a11b0 con el 10 y 2 sobre una carta guardada: 0x4dcb70 con la copia de 0x4a03c0.
    view.replayLetter = [&world, &random = context.random, measure = context.measure, screen = context.screen,
                         texts = context.texts, chosenSlot = context.chosenSlot, worldEntry = context.worldEntry](
                            const std::vector<std::byte>& copy) -> Core::Expected<void, std::string>
    {
        auto letter = Protocol::Messenger::ReadLetterCopy(copy);
        if (!letter.HasValue())
            return Core::Unexpected(std::string("0x4a11b0(2): la carta guardada no se lee"));
        const ChatReceiveContext receive{.world = world,
                                         .random = random,
                                         .measure = measure,
                                         .screen = screen,
                                         .scene = std::nullopt,
                                         .texts = texts,
                                         .chosenSlot = chosenSlot,
                                         .worldEntry = worldEntry};
        return ApplyLetterRead(letter.Value(), receive);
    };
    // CL9 hito 10: el personaje del retrato de las cartas (Scenes/LetterPortrait), que lee su paso.
    if (context.worldEntry != nullptr)
        view.portrait = LetterPortraitHook(*context.worldEntry);
}

UI::OldWindowGlobals OldWindowGlobalsOf(WorldRuntime& runtime)
{
    UI::GameUiState& ui = runtime.ui;
    GameMovementState& movement = runtime.movement;
    return UI::OldWindowGlobals{.leftHeld = movement.leftHeld,
                                .leftClick = movement.pendingClick,
                                .releasedClick = movement.releasedClick,
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
                                .mouseUpdateTime = &movement.mouseUpdateTime,
                                .mouseUpdateTimeMax = &movement.mouseUpdateTimeMax,
                                .soundRequest = [&runtime](std::int32_t sound) { runtime.RequestSound(sound); }};
}

Core::Expected<void, std::string> StepTheGameDialogs(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    UI::DialogState& dialogs = runtime.dialogs;
    GamePanels panels(context.world, context.random, GamePanelTables{.texts = context.texts});
    // Esc (0x623f17 → 0x657880) cierra la ventana abierta más baja: el comercio manda el 3D, la tienda de otro el
    // 3F 07 y las misiones el 31, por la sesión.
    LendTheUiPanelCloses(panels, context);
    // 0x623e9b..0x623ec2: con la de amigos abierta, Esc es de las ventanas viejas (0x490490). Lo leído queda en
    // [EBP−0x7d], que mira el no de 0x62c50e.
    const bool escape = !panels.Registry().IsOpen(UI::FriendsPanel) && KeyEdge(ui, context.input, EscapeKey);
    if (escape)
    {
        // 0x623ec8..0x623ee9: con el recorrido de la cámara (0x4033d0), Esc lo corta (+0x158 = 1 y 0x403d60) y sale.
        if (CameraTourRunning(runtime.events.cameraTour))
        {
            runtime.events.cameraTour.skipped = true;
            RemoveTheCameraTourActors(context.world, runtime.events.cameraTour);
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
                    // 0x623f22..0x623f33: el menú, o en espera si algo abrió un diálogo.
                    (dialogs.current != 0 ? dialogs.pending : dialogs.current) = UI::EscapeMenuDialog;
                    // 0x623f42: con un ítem en el cursor, lo vuelve a su lugar.
                    if (auto returned = ReturnTheCursorItem(MovesOf(context)); !returned.HasValue())
                        return returned;
                }
            }
            else if (dialogs.current == UI::EscapeMenuDialog || dialogs.current == UI::OptionsDialog)
            {
                dialogs.current = dialogs.pending;
                dialogs.pending = 0;
            }
            // 0x623f6a: suena con cualquier diálogo.
            runtime.RequestSound(ButtonSound);
        }
    }
    // 0x623f78: el diálogo abierto, por su tramo.
    return StepTheOpenDialog(context, escape);
}

Core::Expected<void, std::string> StepTheGameUi(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    // 0x636df2..0x636e04.
    ui.hoveredItem = {};
    ui.skillTooltip = -1;
    ui.mouseOnUi = 0;
    // 0x636e0b: 0x7d36899 no lo escribe nadie.
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
    gamePanels.LendTheSecondGuildStep(SecondGuildStepContext{
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
    gamePanels.LendTheGuildWindowStep(GuildWindowStepContext{.world = context.world,
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
    UI::PanelRegistry& panels = gamePanels.Registry();
    // 0x636e24: 0x5ecdc0, la ventana /move, sin nadie que agarró el mouse (0x5ecdd1).
    if (panels.IsOpen(UI::MovePanel) && ui.capture == 0)
    {
        if (auto moved = StepTheMoveWindow(context); !moved.HasValue())
            return moved;
    }
    ui.hovered = 0;
    // 0x636e35: 0x46b7b0, las dos colas del cartel de Slide.bmd (0x46add0).
    if (auto slides = UI::StepTheSlides(runtime.hud.slides, UI::SlideStepContext{.measure = context.measure,
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
    // 0x636ed6: 0x5ebca0, con 0x78abad9 en 0 no hace nada.
    if (auto duel = StepTheDuelRequest(context); !duel.HasValue())
        return duel;
    // 0x636edb.
    if (auto crywolf = StepTheCrywolfInterface(context); !crywolf.HasValue())
        return crywolf;
    if (auto stepped = StepThePanels(context, panels); !stepped.HasValue())
        return stepped;
    if (auto keys = StepTheKeysAndButtons(context, panels); !keys.HasValue())
        return keys;
    // 0x5a7cc0: el rectángulo de la ayuda del F3 24 (0x5767920), que nadie prende: no consume el clic.
    if (runtime.dialogs.current != UI::EscapeMenuDialog)
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

Core::Expected<void, std::string> StepTheFunctionKeys(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const Input::InputState& input = context.input;
    // 0x5ac8d9..0x5ac9ce: sin ítem en el cursor (0x7879fbc), la barra con el chat cerrado y el mouse fuera de la UI
    // (0x775857c) levanta el primer ítem del piso vivo y a la vista a menos de 300 del héroe (0x5650c0, CL9 hito 5).
    if (runtime.cursorItemCount == 0 && KeyEdge(ui, input, SpaceKey) && !ui.chatOpen && ui.mouseOnUi == 0)
    {
        if (auto picked = PickUpTheNearestFloorItem(MovesOf(context), context.fixedHeight); !picked.HasValue())
            return picked;
    }
    // 0x5ac9ce..0x5aca42: F1 pasa por las ayudas (0x77f3b08): de 3 o 4 (la del ítem o la ventana /move) a 0 y si no
    // al siguiente de 0, 1 y 2. Apaga el cartel del conjunto (+0x9305 de [0x6eed3c]) y pide el 0x19.
    if (KeyEdge(ui, input, F1Key))
    {
        std::int32_t& mode = runtime.panels.overlayMode;
        mode = mode == UI::ItemHelpMode || mode == UI::MoveOverlayMode ? 0 : (mode + 1) % FunctionHelpModes;
        context.world.ItemSets().byte9305 = 0;
        runtime.RequestSound(ButtonSound);
    }
    // 0x5aca42..0x5aca86: F2, el chat normal en el registro (0x6c8dd1), y el registro corre 0 (la vtable + 0x34).
    if (KeyEdge(ui, input, F2Key))
    {
        ui.chatModes.showChat = !ui.chatModes.showChat;
        runtime.RequestSound(ButtonSound);
        UI::ScrollTheLog(runtime.chat.log, ui.chatModes, 0);
    }
    // 0x5aca86..0x5acad0: F3, el modo del susurro (0x6c8dd0).
    if (KeyEdge(ui, input, F3Key))
    {
        runtime.chat.input.whisperMode = !runtime.chat.input.whisperMode;
        runtime.RequestSound(ButtonSound);
    }
    // 0x5acad0..0x5acbb0: F4, el tamaño del registro (0x45d8c0) y su esquina (0x45a9d0): en su ventana, 0xba salvo con
    // el inventario y el personaje, el comercio, el almacén, la caja del caos, el NPC de los eventos o la tienda de
    // NPC (cada consulta en su orden, hasta la primera abierta), 0; fuera de ella, (−10, 0x51).
    if (KeyEdge(ui, input, F4Key))
    {
        UI::CycleTheLogSize(runtime.chat.log, ui.chatModes);
        UI::ChatLogFrame& frame = runtime.hud.chatFrame;
        if (ui.chatModes.placement == 1)
        {
            GamePanels gamePanels(context.world, context.random);
            UI::PanelRegistry& panels = gamePanels.Registry();
            const bool left = panels.IsOpen(UI::InventoryPanel) &&
                              (panels.IsOpen(UI::CharacterPanel) || panels.IsOpen(UI::TradePanel) ||
                               panels.IsOpen(UI::WarehousePanel) || panels.IsOpen(UI::ChaosBoxPanel) ||
                               panels.IsOpen(UI::EventNpcPanel) || panels.IsOpen(UI::NpcShopPanel));
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

// 0x59ee4b..0x59ef10: el elegido de antes (0x6c8e30) sigue solo con el ataque automático (0x6c8e3c), fuera del mapa 6
// y de Chaos Castle, entre 0 y 400, y si es un monstruo (+0x88 en 2) que no se muere (+0x389). Si sigue, con el otro
// elegido (0x6c8e38) en −1, un botón del mouse (0x7d36874, 0x7d366d4, 0x7d3685c o 0x7d36880) o el héroe muriéndose,
// suelta solo 0x6c8e30.
void KeepTheChosen(const FramePickingContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
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
    const ActorState& chosen = actors[static_cast<std::size_t>(targets[2])];
    constexpr std::uint8_t MonsterKind = 2;
    if (chosen.dying != 0 || chosen.byte88 != MonsterKind)
    {
        targets[4] = NoTarget;
        targets[2] = NoTarget;
        return;
    }
    const bool mouse =
        runtime.movement.leftHeld || runtime.movement.pendingClick || runtime.ui.rightDown || runtime.ui.rightClick;
    if (targets[4] == NoTarget || mouse || context.world.Hero().dying != 0)
        targets[2] = NoTarget;
}

// 0x59efb9..0x59f48d: el orden de las dos búsquedas de personajes sin Alt. El Dark Wizard y el Elf leen la skill de su
// lugar (+0x425 del héroe) en el registro propio, abierto; con 0x1a a 0x1c, primero los de la máscara 1.
std::array<std::uint8_t, 2> CharacterSearchOrder(const FramePickingContext& context)
{
    constexpr std::uint8_t Monsters = 0x22;
    constexpr std::uint8_t Players = 1;
    const ActorState& hero = context.world.Hero();
    const Kernel::CharacterClass heroClass = Kernel::BaseClassOf(hero.characterClass);
    if (heroClass != Kernel::CharacterClass::DarkWizard && heroClass != Kernel::CharacterClass::Elf)
        return {Monsters, Players};
    std::uint8_t skill = 0;
    {
        const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        skill = OwnSkillByte(context.world.OwnCharacter(), hero.skillIndex);
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
    WorldRuntime& runtime = context.world.Runtime();
    std::array<std::int32_t, 5>& targets = context.targets;
    const auto pick = [&context](std::uint8_t mask)
    { return PickTheCharacter(context.world, mask, context.cameraPosition, context.ray, context.pickingBoxes); };
    // 0x59ec97..0x59ee48: los modos del editor (0x77f3b18) no los prende nadie.
    KeepTheChosen(context);
    // 0x59ef16..0x59ef29.
    targets[0] = NoTarget;
    targets[1] = NoTarget;
    targets[3] = NoTarget;
    // 0x59ef2f → 0x59f52a: con el mouse en la UI, 0x59e3b0 (la party en el viewport). 0x59eb90 (los objetos) no está
    // portado: deja −1.
    if (runtime.ui.mouseOnUi != 0)
    {
        FindThePartyInTheViewport(context.world);
        ForgetTheOther(targets);
        return {};
    }
    // 0x59e2e0, la búsqueda del ítem del piso (CL9 hito 5), sin ítem en el cursor (0x7879fbc: 0x59ef46 y 0x59f4e3).
    const auto pickTheFloorItem = [&context, &runtime, &targets]
    {
        if (runtime.cursorItemCount == 0 && context.ray)
            targets[0] = PickTheFloorItem(context.floorItems, *context.ray);
    };
    if (context.altDown)
    {
        // 0x59ef46..0x59efb4: el ítem del piso, que salta a 0x59f52a (con 0x59e3b0); sin él, los de la máscara 4, los
        // de 0x22 y 1, y los objetos (0x59eb90, que dejan −1).
        pickTheFloorItem();
        if (targets[0] != NoTarget)
            FindThePartyInTheViewport(context.world);
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
    // 0x59f48d → 0x5e4140: con la party cerrada, la lista de la derecha; si elige, 0x59e3b0 (0x59f494 → 0x59f52a).
    GamePanels panels(context.world, context.random);
    if (PickFromThePartyList(context, panels.Registry()))
    {
        FindThePartyInTheViewport(context.world);
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
            // 0x59f4e3..0x59f509: sin NPC, el ítem del piso y, sin ítem, los objetos (0x59eb90), que dejan −1; los dos
            // saltan a 0x59f52f, sin 0x59e3b0.
            if (targets[1] == NoTarget)
                pickTheFloorItem();
            ForgetTheOther(targets);
            return {};
        }
    }
    // 0x59f522..0x59f52a: con un elegido y 0x6c8e38 puesto, 0x59e3b0.
    if (targets[4] != NoTarget)
        FindThePartyInTheViewport(context.world);
    ForgetTheOther(targets);
    return {};
}
} // namespace Client::Scenes
