#include "Scenes/GameUi.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Character/LevelUpPoint.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterPicking.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameCommandsWindow.h"
#include "Scenes/GameDialogSteps.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMoveWindow.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GamePartyWindow.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/NpcWindowSteps.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WarehouseKeypad.h"
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
#include "UI/SlideBanner.h"
#include "World/ItemState.h"

#include <array>
#include <cmath>
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
// 0x5e455a y 0x5e4573: la ventana del personaje, 0xbe × 0x1b1 desde 0x78aba14 y 0x78aba18.
constexpr std::int32_t CharacterWindowWidth = 0xbe;
constexpr std::int32_t CharacterWindowHeight = 0x1b1;
// La clase 4, el Dark Lord: cinco botones de puntos (0x5e4598) y las órdenes de la mascota con Shift (0x420e30).
constexpr std::uint32_t DarkLordClass = 4;
// 0x4246ea..0x424709: el botón de la mascota, fijo en la pantalla.
constexpr std::int32_t PetButtonLeft = 0x217;
constexpr std::int32_t PetButtonRight = 0x22f;
constexpr std::int32_t PetButtonTop = 0x18b;
constexpr std::int32_t PetButtonBottom = 0x1a3;
// 0x42474c: 0x723911 en 2, la ventana de la mascota.
constexpr std::uint8_t PetWindowOpen = 2;
// 0x5aca1b..0x5aca24: F1 da la vuelta por 0, 1 y 2 (idiv por 3).
constexpr std::int32_t FunctionHelpModes = 3;
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

// 0x553520(vk).
bool KeyEdge(UI::GameUiState& ui, const Input::InputState& input, GameKey key)
{
    return UI::LatchTheKey(ui.keyLatches[key.virtualKey], input.IsDown(key.key));
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

// ---------------------------------------------------------------------------------------------------------------
// El paso del inventario abierto (0x5ee1ac..0x5ee1ca) y el del ítem del cursor (0x5d8d50), CL8 hito 4. Leen la esquina
// que dejó su dibujo (0x78a5a14 y 0x78a5a10). Levantar, soltar y equipar están en Scenes/InventoryMoves.

// La lista del asedio en el modo 3, que con 0x6689d0 se suma a las ventanas con grilla.
constexpr std::int32_t SiegeListWithInventory = 3;
// 0x449c00: la caja del entrenador.
constexpr std::int32_t TrainerWithBox = 3;
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
// 0x5c4b04..0x5c4b22: Ctrl con Q, W o E pone el ítem bajo el mouse en esa ranura rápida, con su nivel.
constexpr std::array<char, 3> CtrlQuickSlotLetters{'Q', 'W', 'E'};
constexpr GameKey CtrlKey{Key::Ctrl, 0x11};
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t CellSize = 20;
constexpr std::uint8_t HoveredMark = 2;

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
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
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
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
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

// Lo que hizo el ítem de la grilla bajo el mouse: con levantarlo, 0x5c4110 sale (0x5c5884).
enum class GridHover : std::uint8_t
{
    Stayed,
    PickedUp,
};

// Una grilla de 0x5c4110(x, y, grilla, columnas, filas, tipo): sus casillas, su forma, el tipo (1 la tienda de NPC,
// que compra; 0 las demás), el lugar que anota el ítem bajo el mouse y la grilla de donde sale lo que se levanta.
struct GridView
{
    std::span<World::ItemState> cells;
    std::int32_t columns = 0;
    std::int32_t rows = 0;
    std::uint8_t kind = 0;
    UI::HoveredItem::Place place = UI::HoveredItem::Place::None;
    CursorGrid grid = CursorGrid::None;
};

// 0x5c4110 con el tipo 1 (0x5e57af, la tienda): comprar.
constexpr std::uint8_t ShopGridKind = 1;
// 0x5aed40: el lugar del ítem para su ayuda en la grilla del almacén (1) y en la caja del caos (4).
constexpr std::int32_t WarehouseHelpPlace = 1;
constexpr std::int32_t ChaosBoxHelpPlace = 4;
// 0x5c4b6e: con la tienda, 0x78abb28 en 6.
constexpr std::int32_t ShopHelpPlace = 6;

// 0x5c4b64..0x5c510f: el ítem bajo el mouse en la grilla de la tienda. 0x78abb28 en 6 y 0x78abb2c en la casilla; con
// el clic, sin el modo de reparar y sin otra compra esperando (0x5767950), el clic gastado, el precio de 0x54d0f0 en
// el modo 0 del ítem de arriba a la izquierda, el C1 04 32 con su lugar en C3 y el precio en 0x5767950, hasta el 32.
Core::Expected<GridHover, std::string> BuyTheGridItem(const GameUiContext& context, const GridView& grid,
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
    const std::int32_t topLeft = hovered.gridRow * grid.columns + hovered.gridColumn;
    if (topLeft >= static_cast<std::int32_t>(grid.cells.size()))
        return Core::Unexpected(std::format("0x5c4bc4: el ítem de arriba a la izquierda ({}) cae fuera de la grilla de "
                                            "la tienda",
                                            topLeft));
    const Data::Tables::ItemTable* items = ServicesOf(context).items;
    if (items == nullptr)
        return Core::Unexpected(std::string("0x5c4bc4: el precio de 0x54d0f0 sin Item.bmd"));
    const PetPriceLevels pets{.raven = runtime.ravenLevel, .horse = runtime.darkHorseLevel};
    auto price = ItemPrice(grid.cells[static_cast<std::size_t>(topLeft)], *items, ShopGridPrice, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    context.random.Discard(C3SendRandoms);
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::string("0x5c4bdc: el C1 04 32 sin la sesión"));
    if (auto sent = context.sendNpcWindow(Protocol::NpcWindows::WriteBuy(static_cast<std::uint8_t>(topLeft & 0xff)));
        !sent.HasValue())
        return Core::Unexpected(std::move(sent).Error());
    npc.buyLock = static_cast<std::int32_t>(price.Value());
    return GridHover::Stayed;
}

// 0x5c41f4..0x5c4b3b y 0x5c52b1..0x5c5894: el ítem de la casilla `index` bajo el mouse. La marca 2 en su huella, el
// ítem del cartel (0x78abaa8, su copia en 0x787a518 y su lugar), la esquina de su ayuda y, con la tienda, comprar;
// si no, según las tiendas personales, la reparación y los clics, levantarlo, usarlo, 0x5aed40 y Ctrl con Q, W o E.
Core::Expected<GridHover, std::string> HoverTheGridItem(const GameUiContext& context, UI::PanelRegistry& panels,
                                                        const GridView& grid, std::size_t index, std::int32_t left,
                                                        std::int32_t top)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const World::ItemState& hovered = grid.cells[index];
    const auto cut = [](std::string_view what) { return Core::Unexpected(std::string(what)); };
    // 0x5c41f4..0x5c4284: el ancho y el alto de Item.bmd, y la marca desde la columna y la fila del ítem, sin mirar los
    // bordes: un ítem puesto entra entero.
    const auto footprint = ItemFootprintOf(ServicesOf(context).items, hovered.id, "0x5c4110 (0x5c420a)");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    const std::int32_t column = hovered.gridColumn;
    const std::int32_t row = hovered.gridRow;
    for (std::int32_t y = row; y < row + footprint.Value().height; ++y)
    {
        for (std::int32_t x = column; x < column + footprint.Value().width; ++x)
        {
            const std::int32_t marked = y * grid.columns + x;
            if (marked >= static_cast<std::int32_t>(grid.cells.size()))
                return Core::Unexpected(std::format("0x5c425e: la marca de la casilla {} cae fuera de la grilla "
                                                    "(Q-C200)",
                                                    marked));
            grid.cells[static_cast<std::size_t>(marked)].warning = HoveredMark;
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
    if (grid.kind != 0)
        return BuyTheGridItem(context, grid, index);
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
        return cut("0x5c5110: poner el ítem de la grilla en la tienda personal es de la CL9.4");
    if (shopOpen() && ui.rightClick && !repairing)
        return cut("0x5c5242: el clic derecho sobre el ítem con la tienda personal abierta es de la CL9.4");
    if (repairing)
    {
        if (IsNotRepaired(hovered.id) || !runtime.movement.pendingClick)
            return GridHover::Stayed;
        // 0x5c4472..0x5c4a89: el clic consumido y el C1 05 34 con el lugar de la esquina del ítem (fila · columnas +
        // columna + 12) y 0x78abb10; después sigue con la casilla de al lado (0x5c4b3b).
        runtime.movement.pendingClick = false;
        const auto position = static_cast<std::uint8_t>((row * grid.columns + column + FirstGridSlot) & 0xff);
        if (auto sent = SendRepair(context.random, context.sendNpcWindow, position,
                                   static_cast<std::uint8_t>(runtime.field78abb10 & 0xff));
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return GridHover::Stayed;
    }
    // 0x5c4a8e..0x5c4a9f y 0x5c52b1..0x5c5894: levantarlo.
    if (runtime.movement.pendingClick && runtime.movement.gateRequestBlocked == 0)
    {
        auto picked = grid.place == UI::HoveredItem::Place::InventoryCell
                          ? PickUpFromTheGrid(MovesOf(context), index)
                          : PickUpFromOtherGrid(MovesOf(context), grid.grid, index);
        if (!picked.HasValue())
            return Core::Unexpected(picked.Error());
        // 0x5c5361..0x5c5390: el comercio solo mira la grilla de 0x78a92a8, pero IsOpen(0x20) saca su rand().
        static_cast<void>(panels.IsOpen(UI::TradePanel));
        runtime.lastSoundId = ItemMoveSound;
        return GridHover::PickedUp;
    }
    // 0x5c4aa5..0x5c4aac.
    if (ui.rightClick)
        return cut("0x5c5895: usar el ítem de la grilla (el C1 .. 26) es de la CL9.3");
    // 0x5c4ab7: 0x5aed40(grilla, casilla): el inventario con el 12, el almacén (con 0x40 abierto, sin rand()) y la caja
    // del caos sin él.
    if (grid.place == UI::HoveredItem::Place::InventoryCell)
    {
        ui.field78abb28 = 0;
        ui.field78abb2c = cell + FirstGridSlot;
    }
    else if (grid.grid == CursorGrid::ChaosBox)
    {
        ui.field78abb28 = ChaosBoxHelpPlace;
        ui.field78abb2c = cell;
    }
    else if (panels.IsOpen(UI::WarehousePanel))
    {
        ui.field78abb28 = WarehouseHelpPlace;
        ui.field78abb2c = cell;
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

// 0x5c4110(x, y, grilla, columnas, filas, tipo). Sale con la espera, la traba de C3 24 o un ítem en el cursor; si no,
// recorre las casillas por filas y, con el mouse sobre una con un tipo, lo de ese ítem.
Core::Expected<void, std::string> StepTheGrid(const GameUiContext& context, UI::PanelRegistry& panels,
                                              const GridView& grid, std::int32_t left, std::int32_t top)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field5767954 > 0 || runtime.ui.field78abb39 != 0 || runtime.cursorItemCount > 0)
        return {};
    for (std::int32_t row = 0; row < grid.rows; ++row)
    {
        const std::int32_t cellTop = top + row * CellSize;
        for (std::int32_t column = 0; column < grid.columns; ++column)
        {
            const std::int32_t cellLeft = left + column * CellSize;
            const auto index = static_cast<std::size_t>(row * grid.columns + column);
            if (context.mouseX < cellLeft || context.mouseX >= cellLeft + CellSize || context.mouseY < cellTop ||
                context.mouseY >= cellTop + CellSize || grid.cells[index].id == EmptyOwnItemId)
                continue;
            const auto hovered = HoverTheGridItem(context, panels, grid, index, left, top);
            if (!hovered.HasValue())
                return Core::Unexpected(hovered.Error());
            if (hovered.Value() == GridHover::PickedUp)
                return {};
        }
    }
    return {};
}

// 0x5c4110(x + 15, y + 200, 0x78a9bb0, 8, 8, 0): la grilla del inventario.
Core::Expected<void, std::string> StepTheInventoryGrid(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const GridView grid{.cells = context.world.Inventory().cells,
                        .columns = static_cast<std::int32_t>(InventoryColumns),
                        .rows = static_cast<std::int32_t>(InventoryColumns),
                        .kind = 0,
                        .place = UI::HoveredItem::Place::InventoryCell,
                        .grid = CursorGrid::Inventory};
    return StepTheGrid(context, panels, grid, runtime.hud.inventoryX + 15, runtime.hud.inventoryY + 200);
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
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const std::int32_t buttons = (hero.characterClass & 7u) == DarkLordClass ? 5 : 4;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        if (context.world.OwnCharacter().levelUpPoints != 0)
        {
            for (std::int32_t button = 0; button < buttons; ++button)
            {
                const std::int32_t top = y + 0x73 + button * 0x3c;
                if (!InRect(context, x + 0x7d, top, x + 0x95, top + 0x18) || !movement.pendingClick)
                    continue;
                movement.pendingClick = false;
                if (auto sent = SendRaw(
                        context, Protocol::Character::WriteLevelUpPoint(static_cast<std::uint8_t>(button)), "0x5e494b");
                    !sent.HasValue())
                    return sent;
                runtime.lastSoundId = ButtonSound;
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

// 0x424130 → 0x424230: con el personaje cerrado, 0x723911 en 0. Abierto, la primera vez el pedido del caballo oscuro
// (C1 06 A9 01 00 08, Q-C16) y 0x723911 en 1; el botón de la mascota abre o cierra el panel 0x400. Con 0x723911 en 2
// (la respuesta del A9, que MuRet no lee), la ventana de la mascota es de la CL9.2. 0x416660, desde 0x424130 con 2,
// no hace nada.
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
        const Protocol::Pet::PetInfoRequest horse{Protocol::Pet::DarkHorsePet, Protocol::Pet::InventoryPlace,
                                                  Protocol::Pet::PetSlot};
        if (auto sent = SendRaw(context, Protocol::Pet::WritePetInfoRequest(horse), "0x424610"); !sent.HasValue())
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
        return Core::Unexpected(std::string("0x424759: la ventana de la mascota (0x723911 en 2) es de la CL9.2"));
    return {};
}

// 0x5e571c..0x5e5910: las grillas de las otras ventanas, la caja del entrenador (0x44ad10) y el cartel del set
// (0x40e6c0).
Core::Expected<void, std::string> StepTheOtherGrids(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::PanelState& state = runtime.panels;
    const auto cut = [](std::string_view what) { return Core::Unexpected(std::string(what)); };
    if (panels.IsOpen(UI::TradePanel))
        return cut("0x5e574e: las grillas del comercio (0x78a5a20 y 0x78a92a8) son de la CL9.4");
    // 0x5e577d..0x5e589c: 0x5c4110 desde la esquina de la ventana del NPC, la grilla de la tienda con el tipo 1 y la
    // del almacén con el 0, de 8 × 15 en (x + 0xf, y + 0x32), y la caja del caos de 8 × 4 en (x + 0xf, y + 0x6e)
    // con la mezcla fuera de 1.
    NpcWindowState& npc = runtime.npcWindows;
    const std::int32_t npcLeft = runtime.hud.npcWindowX + 0xf;
    const GridView npcGrid{.cells = npc.grid,
                           .columns = NpcGridColumns,
                           .rows = static_cast<std::int32_t>(NpcGridCellCount) / NpcGridColumns,
                           .kind = 0,
                           .place = UI::HoveredItem::Place::NpcGridCell,
                           .grid = CursorGrid::NpcGrid};
    if (panels.IsOpen(UI::NpcShopPanel))
    {
        GridView shop = npcGrid;
        shop.kind = ShopGridKind;
        if (auto hovered = StepTheGrid(context, panels, shop, npcLeft, runtime.hud.npcWindowY + 0x32);
            !hovered.HasValue())
            return hovered;
    }
    if (panels.IsOpen(UI::WarehousePanel))
    {
        if (auto hovered = StepTheGrid(context, panels, npcGrid, npcLeft, runtime.hud.npcWindowY + 0x32);
            !hovered.HasValue())
            return hovered;
    }
    if (panels.IsOpen(UI::ChaosBoxPanel) && npc.chaosMixState != 1)
    {
        const GridView chaos{.cells = npc.chaosBox,
                             .columns = NpcGridColumns,
                             .rows = ChaosBoxRows,
                             .kind = 0,
                             .place = UI::HoveredItem::Place::ChaosBoxCell,
                             .grid = CursorGrid::ChaosBox};
        if (auto hovered = StepTheGrid(context, panels, chaos, npcLeft, runtime.hud.npcWindowY + 0x6e);
            !hovered.HasValue())
            return hovered;
    }
    else if (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory)
        return cut("0x5e582b: 0x6689d0, la marca del asedio del héroe, es de la CL9.5");
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return cut("0x5e58ea: la grilla de la tienda personal (0x78ab0b8) es de la CL9.4");
    if (state.delegated.trainer == TrainerWithBox)
        return cut("0x44ad10: la grilla de la caja del entrenador (0x723c80) es de la CL9.3");
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
    const auto cut = [](std::string_view what) { return Core::Unexpected(std::string(what)); };
    // 0x5e4ce0..0x5e4f2c: el mouse sobre la ventana es de la UI, y sobre la del NPC abierta (la tienda, con su
    // rand(), el almacén y la caja del caos), en la esquina de 0x78aba28, también.
    if (InRect(context, x, y, x + 0xbe, y + 0x1b1))
        runtime.ui.mouseOnUi = 1;
    const std::int32_t npcX = runtime.hud.npcWindowX;
    const std::int32_t npcY = runtime.hud.npcWindowY;
    const auto overTheNpcWindow = [&context, &runtime, npcX, npcY]
    {
        if (InRect(context, npcX, npcY, npcX + 0xbe, npcY + 0x1b1))
            runtime.ui.mouseOnUi = 1;
    };
    if (panels.IsOpen(UI::NpcShopPanel))
        overTheNpcWindow();
    if (panels.IsOpen(UI::WarehousePanel))
        overTheNpcWindow();
    if (panels.IsOpen(UI::ChaosBoxPanel))
        overTheNpcWindow();
    else if (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory)
        return cut("0x5e4dea: 0x6689d0, la marca del asedio del héroe, es de la CL9.5");
    if (panels.IsOpen(UI::TradePanel))
        return cut("0x5e4e34: el comercio con el inventario es de la CL9.4");
    if (panels.IsOpen(UI::EventNpcPanel) || panels.IsOpen(UI::SecondEventNpcPanel))
        return cut("0x5e4e81: las ventanas de los NPC de evento (0x1000 y 0x4000) son de la CL9.5");
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return cut("0x5e4ee2: las tiendas personales con el inventario son de la CL9.4");
    // 0x5e4f31..0x5e5128: los doce lugares del equipo, cada uno con su acceso, y +0x44 en 1 con un tipo.
    for (World::ItemState& item : world.OwnCharacter().equipment)
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        item.warning = item.id == EmptyOwnItemId ? std::uint8_t{0} : std::uint8_t{1};
    }
    // 0x5e512f..0x5e5202: lo mismo en las grillas: el inventario, la de la tienda y del almacén (0x78a70b4..) y la
    // caja del caos (0x7d2d3c4..). Del comercio (que conserva el 99), 0x78a92a8 y la tienda personal MuRet no tiene
    // nada todavía (CL9.4); 0x449c60 hace lo mismo con la caja del entrenador (0x723c80, CL9.3).
    const auto warn = [](std::span<World::ItemState> cells)
    {
        for (World::ItemState& cell : cells)
            cell.warning = cell.id == EmptyOwnItemId ? std::uint8_t{0} : std::uint8_t{1};
    };
    warn(world.Inventory().cells);
    warn(runtime.npcWindows.grid);
    warn(runtime.npcWindows.chaosBox);
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
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
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
    const auto cut = [](std::string_view what) { return Core::Unexpected(std::string(what)); };
    if (panels.IsOpen(UI::EventNpcPanel))
        return cut("0x5e594c: el paso de la ventana del NPC de evento 0x1000 (0x5e5920) es de la CL9.5");
    if (panels.IsOpen(UI::SecondEventNpcPanel))
        return cut("0x5e5f2e: el paso de la ventana del NPC de evento 0x4000 (0x5e5920) es de la CL9.5");
    if (auto chaos = StepTheChaosBox(context, panels); !chaos.HasValue())
        return chaos;
    if (auto warehouse = StepTheWarehouse(context, panels); !warehouse.HasValue())
        return warehouse;
    if (panels.IsOpen(UI::TradePanel))
        return cut("0x5ece0d: el paso del comercio (0x5ecde0) es de la CL9.4");
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return cut("0x5ea2bd: el paso de las tiendas personales (0x5ea290) es de la CL9.4");
    return StepTheNpcShop(context, panels);
}

// 0x5ee140: los paneles con paso propio, sin diálogo y sin el menú de Esc.
// 0x5d8d50: el ítem del cursor, desde 0x5ee1de con 0x78abaac en 0. La caja del caos (0x5fd120 y 0x5fd4b0, la fase 3)
// y la lista del asedio con el inventario cortan al principio (sin rand()); con la traba de C3 24, nada. Después, lo
// que espera el diálogo del ítem del cursor (0x78abb14, Scenes::StepTheCursorChoice) y, con un ítem, soltarlo en la
// grilla del inventario (0x5ca250), en la del almacén o en la caja del caos y, con el clic fuera de ellas, venderlo
// en la tienda de NPC o tirarlo al piso. 0x78aba60 en 2 (0x5dc791, CL9.4) nadie lo escribe en MuRet.
Core::Expected<void, std::string> StepTheCursorItem(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::PanelState& state = runtime.panels;
    const auto cut = [](std::string_view what) { return Core::Unexpected(std::string(what)); };
    // 0x5d8d72..0x5d8ddf.
    if (panels.IsOpen(UI::ChaosBoxPanel))
        return cut("0x5d8db6: con la caja del caos abierta, el valor de la caja (0x5fd120) y el tipo de la mezcla "
                   "(0x5fd4b0 y 0x5fd4f0) son de la fase 3 del hito 6 (08 P-C192)");
    if (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory)
        return cut("0x5d8dad: 0x6689d0, la marca del asedio del héroe, es de la CL9.5");
    // 0x5d8ddf..0x5d8dee.
    if (runtime.ui.field78abb39 != 0)
        return {};
    // 0x5d8dee..0x5dc798.
    const auto chose = StepTheCursorChoice(context);
    if (!chose.HasValue())
        return Core::Unexpected(chose.Error());
    if (chose.Value())
        return {};
    // 0x5deac5..0x5deacc.
    if (runtime.cursorItemCount <= 0)
        return {};
    // 0x5dead2..0x5deb0e: 0x5ca250(x + 15, y + 200, 0x78a9bb0, 8, 8, 0).
    const InventoryMoveContext moves = MovesOf(context);
    const auto inGrid = DropOnTheInventoryGrid(moves, GridDrop{.mouseX = context.mouseX,
                                                               .mouseY = context.mouseY,
                                                               .left = runtime.hud.inventoryX + 15,
                                                               .top = runtime.hud.inventoryY + 200,
                                                               .inventoryOpen = panels.IsOpen(UI::InventoryPanel)});
    if (!inGrid.HasValue())
        return Core::Unexpected(inGrid.Error());
    // 0x5deb11..0x5dec41: las otras grillas.
    if (panels.IsOpen(UI::TradePanel))
        return cut("0x5deb1c: soltar el ítem del cursor en el comercio (0x78a5a20) es de la CL9.4");
    const std::int32_t npcLeft = runtime.hud.npcWindowX + 0xf;
    bool inOtherGrid = false;
    if (panels.IsOpen(UI::WarehousePanel))
    {
        // 0x5deb57: 0x5ca250(x + 0xf, y + 0x32, 0x78a7070, 8, 0xf, 2).
        const auto dropped = DropOnOtherGrid(moves, OtherGridDrop{.mouseX = context.mouseX,
                                                                  .mouseY = context.mouseY,
                                                                  .left = npcLeft,
                                                                  .top = runtime.hud.npcWindowY + 0x32,
                                                                  .grid = CursorGrid::NpcGrid});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inOtherGrid = dropped.Value();
    }
    if (panels.IsOpen(UI::ChaosBoxPanel) && runtime.npcWindows.chaosMixState != 1)
    {
        // 0x5debe4: 0x5ca250(x + 0xf, y + 0x6e, 0x7d2d380, 8, 4, 3).
        const auto dropped = DropOnOtherGrid(moves, OtherGridDrop{.mouseX = context.mouseX,
                                                                  .mouseY = context.mouseY,
                                                                  .left = npcLeft,
                                                                  .top = runtime.hud.npcWindowY + 0x6e,
                                                                  .grid = CursorGrid::ChaosBox});
        if (!dropped.HasValue())
            return Core::Unexpected(dropped.Error());
        inOtherGrid = inOtherGrid || dropped.Value();
    }
    else if (panels.IsOpen(UI::CastleSiegeListPanel) && state.delegated.castleSiegeListMode == SiegeListWithInventory)
        return cut("0x5debbc: 0x6689d0, la marca del asedio del héroe, es de la CL9.5");
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return cut("0x5dec19: soltar el ítem del cursor en la tienda personal (0x78ab0b8) es de la CL9.4");
    // 0x5dec41: 0x44aca0, la caja del entrenador.
    if (state.delegated.trainer == TrainerWithBox)
        return cut("0x44aca9: soltar el ítem del cursor en la caja del entrenador (0x723c80) es de la CL9.3");
    // 0x5dec46..0x5deca1: el clic, consumido, y nada más si quedó en una grilla.
    if (!runtime.movement.pendingClick)
        return {};
    runtime.movement.pendingClick = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    if (inGrid.Value() || inOtherGrid)
        return {};
    // 0x5deca7..0x5df448: la tienda de NPC, con su rand(); con el mouse en su grilla, venderlo.
    const auto sold = SellTheCursorItem(context, panels);
    if (!sold.HasValue())
        return Core::Unexpected(sold.Error());
    if (sold.Value())
        return {};
    return DropOnTheGround(moves,
                           GroundDrop{.mouseX = context.mouseX, .inventoryOpen = panels.IsOpen(UI::InventoryPanel)});
}

Core::Expected<void, std::string> StepThePanels(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    const UI::DelegatedPanels& delegated = runtime.panels.delegated;
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
    // 0x44b4e0: el NPC de la caja con 0x724580 fuera de 0 y 4.
    if (delegated.trainer != 0 && delegated.trainer != 4)
        return Core::Unexpected(std::string("0x44b4e0: el paso del NPC de la caja 0x723c80 es de la CL9.3"));
    // 0x44dd50 y 0x44f810: el arma de asedio y la puerta.
    if (delegated.siegeWeapon != 0 || delegated.siegeGate != 0)
        return Core::Unexpected(std::string("0x44dd50: el paso del arma y de la puerta del asedio es de la CL9.5"));
    // 0x5ee1de: 0x5d8d50, el ítem del cursor.
    if (ui.field78abaac == 0)
    {
        if (auto cursor = StepTheCursorItem(context, panels); !cursor.HasValue())
            return cursor;
    }
    // 0x5e6520, con 0x78abb00 distinto de 0.
    if (runtime.panels.field78abb00 != 0)
        return Core::Unexpected(std::string("0x5e6520: 0x78abb00 es de la CL9.5"));
    // 0x5e74f0, el panel 0x2000.
    if (panels.IsOpen(UI::Panel2000))
        return Core::Unexpected(std::string("0x5e74f0: el paso del panel 0x2000 es de la CL9"));
    // 0x40a450, los comandos ([0x6e592c]), que empieza con su +4.
    if (auto commands = StepTheCommandsWindow(context); !commands.HasValue())
        return commands;
    // 0x41b5a0, el quest: con el personaje cerrado escribe +0x21390 y +0x21394 de su objeto, que MuRet no tiene
    // ni lee; con +0x21383 en 1 o 2, su ventana.
    if (delegated.quest == 1 || delegated.quest == 2)
        return Core::Unexpected(std::string("0x41b5a0: el paso del quest es de la CL9.1"));
    // 0x5ee212: 0x424130.
    return StepThePetButton(context, panels);
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
    GamePanels panels(context.world, context.random, GamePanelTables{.texts = context.texts});
    panels.LendTheWindowClose(context.requests);
    // 0x623e9b..0x623ec2: con la de amigos abierta, Esc es de las ventanas viejas (0x490490); el flanco queda en
    // [ebp − 0x7d] para los diálogos de sí y no.
    const bool escape = !panels.Registry().IsOpen(UI::FriendsPanel) && KeyEdge(ui, context.input, EscapeKey);
    if (escape)
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
            runtime.lastSoundId = ButtonSound;
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
    GamePanels gamePanels(context.world, context.random,
                          GamePanelTables{.items = context.inventory != nullptr ? context.inventory->items : nullptr,
                                          .itemSetTables = context.itemSetTables,
                                          .texts = context.texts});
    gamePanels.LendThePartyList(context.world, context.requests);
    gamePanels.LendTheNpcWindows(context.world, context.random,
                                 context.inventory != nullptr ? *context.inventory : InventoryServices{},
                                 context.sendNpcWindow);
    gamePanels.LendTheWindowClose(context.requests);
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
                                                    "de la CL9.3"));
        }
    }
    // 0x5ac9ce..0x5aca42: F1 pasa por las ayudas (0x77f3b08): de 3 o 4 (la del ítem o la ventana /move) a 0 y si no
    // al siguiente de 0, 1 y 2. Apaga el cartel del conjunto (+0x9305 de [0x6eed3c]) y pide el 0x19.
    if (KeyEdge(ui, input, F1Key))
    {
        std::int32_t& mode = runtime.panels.overlayMode;
        mode = mode == UI::ItemHelpMode || mode == UI::MoveOverlayMode ? 0 : (mode + 1) % FunctionHelpModes;
        context.world.ItemSets().byte9305 = 0;
        runtime.lastSoundId = ButtonSound;
    }
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
    if (targets[4] == NoTarget || mouse || actors[runtime.hero].dying != 0)
        targets[2] = NoTarget;
}

// 0x59efb9..0x59f48d: el orden de las dos búsquedas de personajes sin Alt. El Dark Wizard y el Elf leen la skill de su
// lugar (+0x425 del héroe) en el registro propio, abierto; con 0x1a a 0x1c, primero los de la máscara 1.
std::array<std::uint8_t, 2> CharacterSearchOrder(const FramePickingContext& context)
{
    constexpr std::uint8_t Monsters = 0x22;
    constexpr std::uint8_t Players = 1;
    const WorldRuntime& runtime = context.world.Runtime();
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const std::uint8_t heroClass = static_cast<std::uint8_t>(hero.characterClass & 7u);
    if (heroClass != 0 && heroClass != 2)
        return {Monsters, Players};
    std::uint8_t skill = 0;
    {
        const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
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
    const auto pick = [&context](std::uint8_t mask) { return PickTheCharacter(context.world, mask, context.ray); };
    // 0x59ec97..0x59ee48: los modos del editor (0x77f3b18) no los prende nadie.
    KeepTheChosen(context);
    // 0x59ef16..0x59ef29.
    targets[0] = NoTarget;
    targets[1] = NoTarget;
    targets[3] = NoTarget;
    // 0x59e3b0 (0x59f52a), con el mouse en la UI y en los saltos de abajo, solo recorre la party (0x78aba48), que está
    // vacía en MuRet (hito 7): no hace nada. 0x59e2e0 (el ítem del piso, sin ítem en el cursor, 0x7879fbc) y 0x59eb90
    // (los objetos) son de la CL9.3: dejan −1.
    if (runtime.ui.mouseOnUi != 0)
    {
        ForgetTheOther(targets);
        return {};
    }
    if (context.altDown)
    {
        // 0x59ef46..0x59efb4: el ítem del piso, los de la máscara 4, los de 0x22 y 1, y los objetos.
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
        ForgetTheOther(targets);
        return {};
    }
    const std::array<std::uint8_t, 2> order = CharacterSearchOrder(context);
    // 0x59f48d → 0x5e4140: con la party cerrada, el ancho del mundo (0x5bb8d0) y la party (0x78aba48), vacía: da 0.
    GamePanels panels(context.world, context.random);
    if (!panels.Registry().IsOpen(UI::PartyPanel))
        static_cast<void>(panels.Registry().WorldWidth());
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
        }
    }
    ForgetTheOther(targets);
    return {};
}
} // namespace Client::Scenes
