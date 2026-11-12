#include "Scenes/GameUi.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/HudItems.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/ChatLogWindow.h"
#include "UI/Dialogs.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

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
// 0x636e11: desde esta y, el mouse está sobre la barra.
constexpr std::int32_t HudTop = 0x1b0;
// 0x5a1181..0x5a1189: Chaos Castle.
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// 0x5a0420 y vecinas: 0x6c8dcc, el máximo de MouseUpdateTime que ponen los botones.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x59ff45..0x5a009b: los seis renglones del área vieja, de 13 de alto desde y = 0 (hasta 0x4e).
constexpr std::int32_t OldAreaRowHeight = 0xd;
constexpr std::int32_t OldAreaBottom = 0x4e;
// 0x4691e0: los dos EDIT del chat, en (0xc1, 0x1a6) de 0xb4 × 0xe y en (0x178, 0x1a6) de 0x32 × 0xe. Sus ids
// (0x45a210) son *inferidos*: los dos primeros que arma WinMain (0x4bb173 y 0x4bb184), antes de los otros tres
// objetos de EDIT y del registro del chat (el 9).
struct ChatEditPlace
{
    std::int32_t field;
    std::int32_t id;
    std::int32_t x, y, width, height;
};
constexpr std::array<ChatEditPlace, 2> ChatEdits{{
    {UI::MessageField, 1, 0xc1, 0x1a6, 0xb4, 0xe},
    {UI::WhisperField, 2, 0x178, 0x1a6, 0x32, 0xe},
}};
// 0x5a33dc..0x5a343a: el botón de F3, junto al EDIT del susurro.
constexpr std::int32_t WhisperModeLeft = 0x1b2;
constexpr std::int32_t WhisperModeTop = 0x19f;
constexpr std::int32_t WhisperModeRight = 0x1c6;
constexpr std::int32_t WhisperModeBottom = 0x1bc;
// 0x5ac93f..0x5ac97b: la barra levanta un ítem del piso a menos de 300 (0x6b8900).
constexpr float PickUpDistance = 300.0f;

// 0x553520(vk): con la tecla abajo y su latch en 0, lo pone en 1 y da verdadero; con la tecla arriba, lo vuelve a
// 0. GetAsyncKeyState mira el teclado en el momento: MuRet mira el estado del frame (Input::InputState).
bool KeyEdge(UI::GameUiState& ui, const Input::InputState& input, GameKey key)
{
    std::int32_t& latch = ui.keyLatches[key.virtualKey];
    if (!input.IsDown(key.key))
    {
        latch = 0;
        return false;
    }
    if (latch != 0)
        return false;
    latch = 1;
    return true;
}

// 0x6578e0: hay un campo de texto con el foco (el chat 0x6c8e68, 0x77f3b68, el del diálogo de los paneles) o la
// ventana del juego no lo tiene. MuRet tiene el del chat (hito 3); los otros son del hito 6, y su ventana es la del
// juego.
bool TextInputBusy(const WorldRuntime& runtime)
{
    return runtime.ui.chatOpen;
}

// 0x636e51..0x636e5a: el registro del chat, 0x45aa80(0) por la vtable + 0x18 de 0x576350c.
Core::Expected<void, std::string> StepTheChat(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    ChatRuntime& chat = runtime.chat;
    const ActorState& hero = context.world.Actors()[runtime.hero];
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
        runtime.lastSoundId = sound;
    // 0x4694c0(0, 0, 1, nombre): el nombre al EDIT del susurro (SetText), sin tocar su buffer.
    if (result.whisperTo)
        UI::SetTheChatFields(chat.input, std::nullopt, *result.whisperTo);
    return {};
}

// 0x636e5d..0x636ed1: con el izquierdo abajo y el mouse fuera de las ventanas (o sobre el registro del chat), el foco
// del administrador de las ventanas viejas (+0x44 de 0x5763514) en 0 y, con el chat y los otros campos cerrados
// (0x77f3b69, 0x77f3b68 y 0x77f3b6a, del hito 6), la lista de las flechas en 0, 0x4a5e10 sobre la lista de ventanas y
// el foco del teclado a la ventana del juego (SetFocus; el IME de 0x467360, MuRet no lo tiene).
void StepTheUnownedClick(WorldRuntime& runtime)
{
    UI::GameUiState& ui = runtime.ui;
    if (!runtime.movement.leftHeld || (ui.hovered != 0 && ui.hovered != UI::ChatLogId))
        return;
    runtime.oldWindows.focus = 0;
    if (ui.chatOpen)
        return;
    ui.focusedList = 0;
    UI::HideTheWindowList(runtime.oldWindows.windowList);
    runtime.chat.input.focus = UI::ChatFocus::Game;
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

// 0x623f78..0x62410b: el menú de Esc (el diálogo 0x6e) en la escena 5, con cinco botones.
Core::Expected<void, std::string> StepTheEscapeMenu(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DialogState& dialogs = runtime.dialogs;
    if (runtime.cursorItemCount > 0)
        return {};
    // 0x623f9c..0x623fb3: 5 en la escena 5.
    constexpr std::int32_t Buttons = 5;
    constexpr std::int32_t Left = 0x104;
    constexpr std::int32_t Right = 0x17c;
    constexpr std::int32_t Height = 0x16;
    constexpr std::int32_t LastJumpTableButton = 3;
    for (std::int32_t button = 0; button < Buttons; ++button)
    {
        // 0x623fdf..0x623fe6: (6 + 3i) · 10.
        const std::int32_t top = (6 + 3 * button) * 10;
        if (!InRect(context, Left, top, Right, top + Height) || !runtime.movement.pendingClick)
            continue;
        runtime.movement.pendingClick = false;
        if (button <= LastJumpTableButton)
        {
            // La tabla 0x62dac4: salir del juego (0x624036), elegir otro servidor, otro personaje y las opciones.
            return Core::Unexpected(std::format("0x62402f: el botón {} del menú de Esc es del hito 6", button));
        }
        // 0x6240a1: volver al juego.
        if (dialogs.current != UI::OptionsDialog)
        {
            dialogs.current = dialogs.pending;
            dialogs.pending = 0;
        }
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
        runtime.lastSoundId = ButtonSound;
    }
    return {};
}

// 0x636e3c..0x636e4c: el paso de la lista de ventanas ([0x5763524], la vtable + 0x18 con 0) y el de las ventanas
// viejas (0x490490). Esc llega a 0x490490 ya leído por 0x623e70, salvo con la de amigos abierta (Q-C446).
Core::Expected<void, std::string> StepTheOldWindows(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const Input::InputState& input = context.input;
    // 0x553520 con las cuatro teclas que leen las ventanas viejas.
    const UI::OldWindowStepView view{.mouseX = context.mouseX,
                                     .mouseY = context.mouseY,
                                     .keyEdge = [&ui, &input](std::uint8_t virtualKey)
                                     {
                                         for (const GameKey& key : {EscapeKey, F5Key, F6Key, EnterKey})
                                         {
                                             if (key.virtualKey == virtualKey)
                                                 return KeyEdge(ui, input, key);
                                         }
                                         return false;
                                     }};
    const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
    if (auto list = UI::StepTheWindowList(runtime.oldWindows, globals, view); !list.HasValue())
        return list;
    return UI::StepTheOldWindows(runtime.oldWindows, globals, view);
}

// 0x5ee140: los paneles con paso propio, sin diálogo y sin el menú de Esc.
Core::Expected<void, std::string> StepThePanels(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const UI::DelegatedPanels& delegated = runtime.panels.delegated;
    if (runtime.dialogs.current == UI::EscapeMenuDialog)
        return {};
    // 0x5e7bf0: con 0x78abb20 en 0 devuelve 0.
    if (ui.field78abb20 != 0)
        return Core::Unexpected(std::string("0x5e7bf0: 0x78abb20 es del hito 4"));
    // 0x5ee15a..0x5ee188.
    if (runtime.dialogs.current != 0 || ui.capture != 0 || ui.hovered != 0 || ui.field78abad9 != 0)
        return {};
    // 0x5e3c80 y 0x5e4510: la party y el personaje, cada uno con su IsOpen.
    if (panels.IsOpen(UI::PartyPanel))
        return Core::Unexpected(std::string("0x5e3c80: el paso del panel de la party es del hito 6"));
    if (panels.IsOpen(UI::CharacterPanel))
        return Core::Unexpected(std::string("0x5e4510: el paso del panel del personaje es del hito 5"));
    // 0x5ee1a3: las hijas del inventario (0x5e4ce0, 0x5e5920, 0x5fe610, 0x5ea060, 0x5ecde0, 0x5ea290, 0x5ed960).
    if (panels.IsOpen(UI::InventoryPanel))
        return Core::Unexpected(std::string("0x5ee1ac: el paso del inventario es del hito 4"));
    // 0x44b4e0: el NPC de la caja con 0x724580 fuera de 0 y 4.
    if (delegated.trainer != 0 && delegated.trainer != 4)
        return Core::Unexpected(std::string("0x44b4e0: el paso del NPC de la caja 0x723c80 es de la CL9.3"));
    // 0x44dd50 y 0x44f810: el arma de asedio y la puerta.
    if (delegated.siegeWeapon != 0 || delegated.siegeGate != 0)
        return Core::Unexpected(std::string("0x44dd50: el paso del arma y de la puerta del asedio es de la CL9.5"));
    // 0x5ee1de: 0x5d8d50, el ítem del cursor.
    if (ui.field78abaac == 0 && runtime.cursorItemCount > 0)
        return Core::Unexpected(std::string("0x5d8d50: el ítem del cursor es del hito 4"));
    // 0x5e6520, con 0x78abb00 distinto de 0.
    if (runtime.panels.field78abb00 != 0)
        return Core::Unexpected(std::string("0x5e6520: 0x78abb00 es de la CL9.5"));
    // 0x5e74f0, el panel 0x2000.
    if (panels.IsOpen(UI::Panel2000))
        return Core::Unexpected(std::string("0x5e74f0: el paso del panel 0x2000 es de la CL9"));
    // 0x40a450, los comandos (+4 de 0x6e592c).
    if (delegated.commands != 0)
        return Core::Unexpected(std::string("0x40a450: el paso de los comandos es del hito 6 y de la CL9.4"));
    // 0x41b5a0, el quest: con el personaje cerrado escribe +0x21390 y +0x21394 de su objeto, que MuRet no tiene
    // ni lee; con +0x21383 en 1 o 2, su ventana.
    if (delegated.quest == 1 || delegated.quest == 2)
        return Core::Unexpected(std::string("0x41b5a0: el paso del quest es de la CL9.1"));
    // 0x424130 → 0x424230: con el personaje cerrado, 0x723911 = 0. 0x416660 no hace nada.
    runtime.panels.delegated.pet = 0;
    return {};
}

// 0x5a05e5..0x5a10e2: la barra de skills. Abre el registro propio aunque no haya skills y lo cierra al final.
Core::Expected<void, std::string> StepTheSkillBar(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    if (context.world.OwnCharacter().byte6a == 0)
        return {};
    // 0x5a066c..0x5a06f9: el botón de la barra.
    if (InRect(context, 0x130, 0x1bc, 0x150, 0x1e0))
    {
        ui.skillTooltipX = 0x140;
        ui.skillTooltip = context.world.Actors()[runtime.hero].skillIndex;
        ui.skillTooltipY = 0x1bc;
        ui.mouseOnUi = 1;
        runtime.movement.mouseUpdateTime = 0;
        if (runtime.movement.pendingClick)
        {
            runtime.movement.pendingClick = false;
            ui.skillListOpen = ui.skillListOpen == 0 ? 1 : 0;
            runtime.lastSoundId = ButtonSound;
        }
    }
    // 0x652330: sin el asedio (0x4294b0) no hace nada.
    if (runtime.siegeActive)
        return Core::Unexpected(std::string("0x652330: la skill del asedio en la barra es de la CL9.5"));
    if (ui.skillListOpen != 0)
        return Core::Unexpected(std::string("0x5a070e: la lista de skills es de la CL9.2"));
    return {};
}

// 0x40f6d0: el paso del panel del asedio de la clase 0 (vtable 0x6b88a8 + 8).
Core::Expected<void, std::string> StepTheSiegePanel(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.siegePanelKind)
        return {};
    if (*runtime.siegePanelKind != 0)
        return Core::Unexpected(std::string("0x4125a0: el paso del panel del asedio con gremio es de la CL9.5"));
    UI::GameUiState& ui = runtime.ui;
    UI::SiegePanelState& panel = runtime.siegePanel;
    if (!TextInputBusy(runtime) && KeyEdge(ui, context.input, TabKey))
        panel.visible ^= 1;
    if (panel.visible == 0)
        return {};
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (panel.x < x && x <= panel.x + 0x97 && panel.y < y && y < panel.y + 0xad)
        ui.mouseOnUi = 1;
    if (!runtime.movement.pendingClick)
        return {};
    // 0x40f74c..0x40f7a0: la flecha de la página, con los float 127, 24, 13 y 22 (0x6b8838, 0x6b8800, 0x6b8834 y
    // 0x6b8820), que con enteros dan lo mismo.
    const bool outsideThePage =
        x < panel.x + 127 || panel.x + 127 + 24 < x || y < panel.y + 13 || panel.y + 13 + 22 < y;
    if (!outsideThePage)
    {
        ++panel.page;
        if (panel.page >= 3)
            panel.page = 1;
        else if (panel.page < 1)
            panel.page = 2;
    }
    else
    {
        // 0x40f7cc..0x40f7f4: fuera de los dos, el clic sigue para los demás.
        if (x < panel.x + 5 || panel.x + 0x2b < x || y < panel.y + 0x1c || panel.y + 0x2c < y)
            return {};
        // 0x40f7f6..0x40f827: la suma queda en el registro de la x87 para las comparaciones.
        const double sum = static_cast<double>(panel.alpha) + static_cast<double>(0.1f);
        panel.alpha = static_cast<float>(sum);
        if (sum > static_cast<double>(1.1f))
            panel.alpha = 0.5f;
        else if (sum < 0.5)
            panel.alpha = 1.0f;
    }
    runtime.movement.pendingClick = false;
    runtime.movement.leftHeld = false;
    return {};
}

// 0x59ff45..0x5a009b: el clic derecho sobre un renglón del área vieja (0x77d9868, desde 0x6c8eb4) que se dibujó (con
// texto y, sin F2, que no sea del chat normal), de otro, con el chat abierto y el héroe en la zona segura: su nombre al
// buffer del susurro, sin el EDIT (Q-C530), y el 0x19. El ancho del renglón es el que dejó su dibujo, en píxeles.
void StepTheOldAreaRows(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const ActorState& hero = context.world.Actors()[runtime.hero];
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
        runtime.lastSoundId = ButtonSound;
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
    if (input.focus != UI::ChatFocus::Game && input.field % 2 == 0)
        sound = UI::RecallFromTheHistory(input, direction);
    else
        sound = UI::StepTheWhisperName(input, direction);
    if (sound)
        runtime.lastSoundId = ButtonSound;
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
        UI::ClickTheChatField(runtime.chat.input, edit.field);
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
    runtime.lastSoundId = ButtonSound;
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
    const ActorState& hero = context.world.Actors()[runtime.hero];
    // 0x59ff3f: con el recorrido de la cámara, nada.
    if (movement.cameraTour)
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
    // 0x5a10e7..0x5a116f: los números eligen la skill (0x59f810), con el foco en el juego, sin campos de texto,
    // sin Alt y sin las órdenes de la mascota del Dark Lord (0x420e30, Shift con 1 a 4). 0x7d368a4 no lo escribe
    // MuRet, y el diálogo 0x73 (con el que no se mira el foco) es del hito 6.
    if (runtime.chat.input.focus == UI::ChatFocus::Game && !ui.chatOpen && !runtime.chat.input.otherInput)
    {
        constexpr std::uint8_t DarkLordClass = 4;
        if ((hero.characterClass & 7u) == DarkLordClass && input.IsDown(Key::Shift))
        {
            for (int digit = 1; digit <= 4; ++digit)
            {
                if (input.IsDown(Digit(digit).key))
                    return Core::Unexpected(std::string("0x420e30: las órdenes de la mascota del Dark Lord son de la "
                                                        "CL9.2"));
            }
        }
        if (!input.IsDown(Key::Alt))
        {
            for (int digit = 1; digit <= 10; ++digit)
            {
                if (input.IsDown(Digit(digit % 10).key))
                    return Core::Unexpected(std::string("0x59f810: elegir la skill con los números es de la CL9.2"));
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
            if (found.Value())
                return Core::Unexpected(std::string("0x5a2bc8: usar el ítem de la ranura rápida (0x5af520 y el C1 26) "
                                                    "es de la CL9.3"));
        }
    }
    if (TextInputBusy(runtime))
        StepTheChatButtons(context);
    // 0x5a343e..0x5a359a: Q, W y E solo sin campo de texto, con todo esto cerrado y el héroe vivo. Las consultas
    // del comercio y de la tienda de NPC sacan un rand() cada una en cada paso. 0x77f3b68, entre 0x40000 y el
    // diálogo, es un campo de texto que MuRet no tiene.
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
            if (found.Value())
                return Core::Unexpected(
                    std::format("0x5a35e0: {} usa el ítem de la ranura rápida (0x5af520 y el C1 26), "
                                "de la CL9.3",
                                letter));
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
                const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                        Render::Models::OwnRecordCloseRandoms);
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
    if (runtime.siegeActive)
        return Core::Unexpected(std::string("0x6529e0: la UI del asedio es de la CL9.5"));
    // 0x5a5511: sin ningún panel abierto, el paso del panel del asedio (0x4125a0). 0x413c90 no hace nada.
    if (!panels.IsOpen(UI::AnyPanel))
        return StepTheSiegePanel(context);
    return {};
}
} // namespace

void LatchTheLeftButton(GameMovementState& state, bool pressed, bool down)
{
    if (!down)
        state.leftHeld = false;
    else if (!state.previousLeftDown)
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
                                .sound = runtime.lastSoundId};
}

Core::Expected<void, std::string> StepTheGameDialogs(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    UI::DialogState& dialogs = runtime.dialogs;
    GamePanels panels(context.world, context.random);
    // 0x623e9b..0x623ec2: con la de amigos abierta, Esc es de las ventanas viejas (0x490490).
    if (!panels.Registry().IsOpen(UI::FriendsPanel) && KeyEdge(ui, context.input, EscapeKey))
    {
        // 0x623ec8: con el recorrido de la cámara, Esc lo corta (+0x158 = 1, 0x403d60) y sale.
        if (runtime.movement.cameraTour)
            return Core::Unexpected(std::string("0x623ed7: cortar el recorrido de la cámara con Esc es de la CL9.5"));
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
                    if (runtime.cursorItemCount > 0)
                        return Core::Unexpected(std::string("0x623f42: soltar el ítem del cursor (0x5be150) es del "
                                                            "hito 4"));
                }
            }
            else if (dialogs.current == UI::EscapeMenuDialog || dialogs.current == UI::OptionsDialog)
            {
                dialogs.current = dialogs.pending;
                dialogs.pending = 0;
            }
            // 0x623f6a: suena con cualquier diálogo.
            runtime.lastSoundId = ButtonSound;
        }
    }
    // 0x623f78.
    if (dialogs.current == 0)
        return {};
    if (dialogs.current != UI::EscapeMenuDialog)
        return Core::Unexpected(std::format("0x625563: el diálogo {:#x} es del hito 6", dialogs.current));
    return StepTheEscapeMenu(context);
}

Core::Expected<void, std::string> StepTheGameUi(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    // 0x636df2..0x636e04.
    ui.field78abaa8 = 0;
    ui.skillTooltip = -1;
    ui.mouseOnUi = 0;
    // 0x636e0b: 0x7d36899 no lo escribe nadie.
    if (context.mouseY >= HudTop)
        ui.mouseOnUi = 1;
    GamePanels gamePanels(context.world, context.random);
    UI::PanelRegistry& panels = gamePanels.Registry();
    // 0x636e24: 0x5ecdc0, la ventana /move, sin nadie que agarró el mouse (0x5ecdd1).
    if (panels.IsOpen(UI::MovePanel) && ui.capture == 0)
        return Core::Unexpected(std::string("0x5ecdc0: el paso de la ventana /move (0x449850) es del hito 6"));
    ui.hovered = 0;
    // 0x636e35: 0x46b7b0, las dos colas del cartel de Slide.bmd (0x46add0), sin portar (08, P-C80): vacías, porque
    // el 0D que las llena corta.
    if (auto old = StepTheOldWindows(context); !old.HasValue())
        return old;
    if (auto chat = StepTheChat(context); !chat.HasValue())
        return chat;
    StepTheUnownedClick(runtime);
    // 0x636ed6: 0x5ebca0, con 0x78abad9 en 0 no hace nada.
    // 0x636edb: 0x435bd0 → 0x4303c0, Crywolf, con sus estados (0x723bf9, 0x723a44) que solo escribe su paquete.
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
    // (0x775857c) levanta el primer ítem del piso vivo a menos de 300 del héroe (0x5650c0). El byte +0x15c del ítem
    // que también mira no está identificado: con un ítem cerca, el corte.
    if (runtime.cursorItemCount == 0 && KeyEdge(ui, input, SpaceKey) && !ui.chatOpen && ui.mouseOnUi == 0)
    {
        const ActorState& hero = context.world.Actors()[runtime.hero];
        for (const FloorItemState& item : context.world.DrawTail().floorItems)
        {
            if (!item.alive)
                continue;
            const double dx = static_cast<double>(item.position.x) - static_cast<double>(hero.position.x);
            const double dy = static_cast<double>(item.position.y) - static_cast<double>(hero.position.y);
            const double dz = static_cast<double>(item.position.z) - static_cast<double>(hero.position.z);
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < static_cast<double>(PickUpDistance))
                return Core::Unexpected(std::string("0x5ac9ae: levantar el ítem del piso con la barra (0x5650c0) es "
                                                    "del hito 6"));
        }
    }
    // 0x5ac9ce..0x5aca42: F1, la ayuda de las superposiciones (0x77f3b08) y +0x9305 de [0x6eed3c].
    if (KeyEdge(ui, input, F1Key))
        return Core::Unexpected(std::string("0x5ac9ce: F1, la ayuda (0x77f3b08), es del hito 6"));
    // 0x5aca42..0x5aca86: F2, el chat normal en el registro (0x6c8dd1), y el registro corre 0 (la vtable + 0x34).
    if (KeyEdge(ui, input, F2Key))
    {
        ui.chatModes.showChat = !ui.chatModes.showChat;
        runtime.lastSoundId = ButtonSound;
        UI::ScrollTheLog(runtime.chat.log, ui.chatModes, 0);
    }
    // 0x5aca86..0x5acad0: F3, el modo del susurro (0x6c8dd0).
    if (KeyEdge(ui, input, F3Key))
    {
        runtime.chat.input.whisperMode = !runtime.chat.input.whisperMode;
        runtime.lastSoundId = ButtonSound;
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
        runtime.lastSoundId = ButtonSound;
    }
    return {};
}

Core::Expected<void, std::string> PickInTheFrame(const FramePickingContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    std::array<std::int32_t, 5>& targets = context.targets;
    // 0x59ec97..0x59eda4: los modos del editor (0x77f3b18) no los prende nadie.
    // 0x59edb4..0x59ee66: el elegido de antes (0x6c8e30), con 0x6c8e3c en 1 (.data).
    constexpr std::int32_t Pool = 400;
    const bool chaosCastle = runtime.map >= 18 && runtime.map <= 23;
    if (runtime.map == 6 || chaosCastle || targets[2] < 0 || targets[2] > Pool)
    {
        targets[2] = -1;
        targets[4] = -1;
    }
    else
        return Core::Unexpected(std::string("0x59ee0b: seguir al elegido de antes es de la CL9.1"));
    targets[0] = -1;
    targets[1] = -1;
    targets[3] = -1;
    if (runtime.ui.mouseOnUi == 0)
    {
        if (!context.altDown)
        {
            const ActorState& hero = context.world.Actors()[runtime.hero];
            const std::uint8_t heroClass = static_cast<std::uint8_t>(hero.characterClass & 7u);
            // 0x59efda..0x59f46e: el Dark Wizard y el Elf leen su skill del registro propio para el orden de las
            // búsquedas, que son de sus olas.
            if (heroClass == 0 || heroClass == 2)
            {
                const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                                        Render::Models::OwnRecordCloseRandoms);
            }
            // 0x59f48d → 0x5e4140: sin la party abierta, el ancho del mundo y los de la party (0x78aba48), que
            // MuRet no tiene.
            GamePanels panels(context.world, context.random);
            if (!panels.Registry().IsOpen(UI::PartyPanel))
                static_cast<void>(panels.Registry().WorldWidth());
        }
        // Las búsquedas: 0x59e700 (los personajes con 0x22, 1 y 4), 0x59e2e0 (el ítem del piso, sin ítem en el
        // cursor) y 0x59eb90 (los objetos). Son de la CL9.1 y la CL9.3: dejan −1.
    }
    // Con el mouse en la UI, 0x59e3b0 (0x59f52a), sin rand(), del elegido.
    // 0x59f52f.
    if (targets[2] == -1)
        targets[4] = -1;
    return {};
}
} // namespace Client::Scenes
