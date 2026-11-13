#include "UI/FriendWindows.h"

#include "Core/Expected.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/LetterModel.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
constexpr std::int32_t TopLeftRect = 1;
// 0x41ed10(0x19, 0, 0) y 0x41ed10(0x1c, 0, 0): quedan en el último sonido; MuRet no tiene audio hasta la CL10.
constexpr std::int32_t ButtonSound = 0x19;
constexpr std::int32_t OpenSound = 0x1c;
// Text.bmd.
constexpr std::size_t FriendsTitleText = 990;
constexpr std::size_t ReconnectTitleText = 1066;
constexpr std::size_t FriendTabText = 1032;
constexpr std::size_t WindowsTabText = 1033;
constexpr std::size_t LettersTabText = 1034;
constexpr std::size_t AddButtonText = 1018;
constexpr std::size_t DeleteButtonText = 1019;
constexpr std::size_t ChatButtonText = 1020;
constexpr std::size_t WriteButtonText = 1015;
constexpr std::size_t WindowsButtonText = 1025;
constexpr std::size_t SecondLetterButtonText = 1027;
constexpr std::size_t ThirdLetterButtonText = 1010;
constexpr std::size_t FourthLetterButtonText = 1011;
constexpr std::size_t PlaceText = 1022;
constexpr std::size_t TypeTheNameText = 1023;
constexpr std::size_t ReallyDeleteText = 1024;
constexpr std::size_t StateButtonText = 1035;
constexpr std::size_t GoOfflineText = 1036;
constexpr std::size_t ConfirmText = 228;
constexpr std::size_t CancelText = 229;
// 0x460970: el ancho de un nombre de 10 (s_ZZZZZZZZZZ_006c7ad4) y el margen.
constexpr std::string_view WidestName = "ZZZZZZZZZZ";
constexpr std::int32_t ColumnMargin = 8;
// 0x4a2280: en (0x32, 0x32) de 0xfa × 0xaa, que no se achica de 0xfa × 0x96; cada pestaña desde (0, 0x15) del área de
// adentro, con su ancho y 0x13 menos de alto.
constexpr std::int32_t FriendsPlace = 0x32;
constexpr std::int32_t FriendsWidth = 0xfa;
constexpr std::int32_t FriendsHeight = 0xaa;
constexpr std::int32_t FriendsMinHeight = 0x96;
constexpr std::int32_t TabTop = 0x15;
constexpr std::int32_t TabHeightCut = -0x13;
// 0x49c8b0 y 0x49e5c0: la lista cuelga de 0x16 desde abajo con 0x27 menos de alto; los botones de 0x32 × 0xe, a 0x11
// desde abajo.
constexpr std::int32_t ListBottom = 0x16;
constexpr std::int32_t ListHeightCut = -0x27;
constexpr std::int32_t TabButtonWidth = 0x32;
constexpr std::int32_t TabButtonHeight = 0xe;
constexpr std::int32_t TabButtonY = 0x11;
constexpr std::array<std::int32_t, 4> TabButtonX{2, 0x35, 0x68, 0x9b};
// 0x463190: +0x130 de la lista de las cartas.
constexpr std::int32_t LetterListHeaders = 0x19;
constexpr std::size_t SenderColumnText = 1028;
constexpr std::size_t DateColumnText = 1029;
constexpr std::size_t SubjectColumnText = 1030;
// 0x464510: la marca de un renglón de la lista de las cartas.
constexpr std::int32_t LetterMarkWidth = 0xc;
// 0x4a3030: los tres encabezados de 0x35 × 0x13 en y + 2 del área de adentro; el botón del estado, con 0x10 más que
// su texto, a 0x10 del borde derecho y en y + 4, de 0x14 de alto.
constexpr std::int32_t HeaderWidth = 0x35;
constexpr std::int32_t HeaderHeight = 0x13;
constexpr std::int32_t HeaderY = 2;
constexpr std::array<std::int32_t, FriendsTabCount> HeaderX{0, 0x35, 0x6a};
constexpr std::int32_t StateButtonPad = 0x10;
constexpr std::int32_t StateButtonY = 4;
constexpr std::int32_t StateButtonHeight = 0x14;
// 0x49dcf0: los encabezados de las columnas de la lista de amigos, de 0x13 de alto.
constexpr std::int32_t ColumnHeight = 0x13;
// 0x4a35a0: la caja en (0x32, 0x32) de 0x96 × 100 con la barra, el borde y el arrastre; el EDIT de 0x50 × 0xe en
// (0x1e, 0xe) con el tope de 10 y el filtro 8; los botones de 0x32 × 0x14 en (0x12, 0x28) y (0x49, 0x28).
constexpr std::int32_t AddBoxPlace = 0x32;
constexpr std::int32_t AddBoxWidth = 0x96;
constexpr std::int32_t AddBoxHeight = 100;
constexpr std::int32_t AddBoxFlags = TitleFlag | BorderFlag | DragFlag;
constexpr std::int32_t EditWidth = 0x50;
constexpr std::int32_t EditHeight = 0xe;
constexpr std::int32_t EditX = 0x1e;
constexpr std::int32_t EditY = 0xe;
constexpr std::int32_t AddNameFilter = 8;
constexpr std::int32_t AddBoxButtonWidth = 0x32;
constexpr std::int32_t AddBoxButtonHeight = 0x14;
constexpr std::int32_t ConfirmX = 0x12;
constexpr std::int32_t CancelX = 0x49;
constexpr std::int32_t AddBoxButtonY = 0x28;
// Los comandos de los botones.
constexpr std::int32_t FirstCommand = 1;
constexpr std::int32_t SecondCommand = 2;
constexpr std::int32_t ThirdCommand = 3;
constexpr std::int32_t FourthCommand = 4;
// 0x461f00 y 0x462bd0: el renglón de 13 de alto, 3 más arriba que su y.
constexpr std::int32_t RowHeight = 13;
constexpr std::int32_t RowLift = 3;
// 0x49d0b0: un amigo con un estado mayor que 0xfc no está.
constexpr std::uint8_t LastOnlineState = 0xfc;
// 0x4a2f70: la F.
constexpr std::uint8_t FriendsKey = 0x46;

OldWindow* FriendsWindowOf(OldWindowManager& manager)
{
    if (manager.friends == 0)
        return nullptr;
    return FindTheOldWindow(manager, manager.friends);
}

std::size_t RowCount(const OldWindow& tab)
{
    if (tab.kind == OldWindowKind::FriendTab)
        return tab.tab.friends.size();
    if (tab.kind == OldWindowKind::WindowsTab)
        return tab.tab.windows.size();
    return tab.tab.letters.size();
}

// 0x45abc0 y 0x47a9d0 sobre la lista de una pestaña: la cola entera.
void DrainTheList(OldWindowManager& manager, OldWindow& tab)
{
    OldList& list = tab.tab.list;
    while (!list.base.queue.empty())
    {
        list.base.current = list.base.queue.front();
        list.base.queue.pop_front();
        HandleTheListMessage(list, RowCount(tab));
        Steps::PlaceAgainstTheParent(manager, list.base);
    }
}

// 0x45aa50 sobre la lista de una pestaña.
void PostToTheList(OldWindowManager& manager, OldWindow& tab, std::int32_t type)
{
    tab.tab.list.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    DrainTheList(manager, tab);
}

// La cola de un botón con su paso con 1 (0x45aa80): 0x45abc0 con cada mensaje.
void DrainTheButton(OldWindowManager& manager, OldButton& button)
{
    while (!button.base.queue.empty())
    {
        button.base.current = button.base.queue.front();
        button.base.queue.pop_front();
        Steps::PlaceAgainstTheParent(manager, button.base);
    }
}

// 0x45aa50 sobre el EDIT: su vtable + 0x28 es 0x45a640, que da 0.
void PostToTheEdit(OldWindowManager& manager, OldEdit& edit, std::int32_t type)
{
    edit.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!edit.base.queue.empty())
    {
        edit.base.current = edit.base.queue.front();
        edit.base.queue.pop_front();
        Steps::PlaceAgainstTheParent(manager, edit.base);
    }
}

// 0x4682a0(0), la vtable + 0x30 del EDIT: SetFocus y la lista de las flechas en él. El IME de 0x4673a0 (con el foco en
// la ventana del juego y sin filtro) MuRet no lo tiene.
void FocusTheEdit(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals)
{
    globals.keyboardFocus = ChatFocus::OldWindow;
    manager.focusedEdit = edit.base.id;
    globals.focusedList = edit.base.id;
}

// 0x45a210 de una lista (0x460970, 0x462010 y 0x463190): cuelga de su borde de abajo; lo demás, como OldList.
OldList MakeTheList(OldWindowManager& manager)
{
    OldList list;
    list.base = Steps::MakeTheBase(manager);
    list.base.rectMode = 2;
    return list;
}

// Una pestaña: el marco (0x491ca0), la lista y los botones, en el orden de sus campos.
OldWindow MakeTheTab(OldWindowManager& manager, OldWindowKind kind, std::size_t buttons)
{
    OldWindow tab = Steps::MakeTheFrame(manager);
    tab.kind = kind;
    tab.tab.list = MakeTheList(manager);
    for (std::size_t at = 0; at < buttons; ++at)
        tab.tab.buttons.push_back(Steps::MakeTheButton(manager));
    return tab;
}

// El init de una pestaña (0x49c8b0, 0x49e5c0 y 0x4a06b0): sin banderas, en (0x32, 0x32) de 0xfa × 0xaa, la lista y
// los botones desde abajo.
void InitTheTab(OldWindow& tab, const std::string& title, std::int32_t parent, const std::array<std::size_t, 4>& labels,
                const OldWindowTexts& texts)
{
    OldWindowBase& base = tab.base;
    tab.frame.title = title;
    base.parent = parent;
    base.flags = 0;
    base.x = FriendsPlace;
    base.y = FriendsPlace;
    base.width = FriendsWidth;
    base.height = FriendsHeight;
    tab.frame.minWidth = FriendsWidth;
    tab.frame.minHeight = FriendsMinHeight;
    tab.frame.maxWidth = 0;
    tab.frame.maxHeight = 0;
    OldList& list = tab.tab.list;
    list.base.parent = base.id;
    list.base.placeMode = 2;
    list.base.placeX = 0;
    list.base.placeY = ListBottom;
    list.base.sizeMode = 3;
    list.base.sizeX = 0;
    list.base.sizeY = ListHeightCut;
    for (std::size_t at = 0; at < tab.tab.buttons.size(); ++at)
    {
        OldButton& button = tab.tab.buttons[at];
        button.base.width = TabButtonWidth;
        button.base.height = TabButtonHeight;
        button.command = static_cast<std::int32_t>(at) + 1;
        button.label = texts.At(labels[at]);
        button.base.parent = base.id;
        button.base.placeMode = 2;
        button.base.placeX = TabButtonX[at];
        button.base.placeY = TabButtonY;
    }
}

// 0x49cad0: el nombre y el estado del elegido de la lista de amigos, o ninguno.
const FriendEntry* SelectedFriend(OldWindow& tab)
{
    const std::optional<std::size_t> at = SelectedInTheList(tab.tab.list, tab.tab.friends.size());
    if (!at || *at >= tab.tab.friends.size())
        return nullptr;
    return &tab.tab.friends[*at];
}

// 0x49e740: el id del elegido de la lista de ventanas, o 0.
std::int32_t SelectedWindow(OldWindow& tab)
{
    const std::optional<std::size_t> at = SelectedInTheList(tab.tab.list, tab.tab.windows.size());
    if (!at || *at >= tab.tab.windows.size())
        return 0;
    return tab.tab.windows[*at].id;
}

// 0x49bd60: la lista con lo del modelo y el lugar (desde 1) del último con el nombre, o 0.
std::int32_t CopyTheModel(OldWindowManager& manager, OldWindow& tab, std::string_view name)
{
    tab.tab.friends.clear();
    std::int32_t found = 0;
    std::int32_t at = 0;
    for (const FriendEntry& entry : manager.friendModel.entries)
    {
        tab.tab.friends.push_back(entry);
        ++at;
        if (SameFriendName(entry.name, name))
            found = at;
    }
    return found;
}

// 0x49ddd0.
void RefreshTheTab(OldWindowManager& manager, OldWindow& tab)
{
    std::string name;
    if (tab.tab.list.selection > 0)
    {
        if (const FriendEntry* selected = SelectedFriend(tab); selected != nullptr)
            name = FriendName(selected->name);
    }
    tab.tab.list.refreshed = 1;
    const std::int32_t at = CopyTheModel(manager, tab, name);
    SelectInTheList(tab.tab.list, tab.tab.friends.size(), at);
    ScrollTheList(tab.tab.list, tab.tab.friends.size(), 0);
}

Core::Expected<void, std::string> Cut(std::string what)
{
    return Core::Unexpected(std::move(what));
}

Core::Expected<std::int32_t, std::string> CreateFromAStep(OldWindowManager& manager, const OldWindowRequest& request,
                                                          const OldWindowGlobals& globals,
                                                          const OldWindowStepView& view, std::string_view where)
{
    if (view.texts == nullptr || view.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": la fábrica de las ventanas viejas sin los textos ni la medida");
    return CreateTheOldWindow(manager, request, *view.texts, *view.measure, &globals, &view);
}

Core::Expected<void, std::string> Send(const OldWindowStepView& view, FriendSend packet, std::string_view where)
{
    if (!view.send)
        return Core::Unexpected(std::string(where) + ": el paquete de los amigos sin el cable");
    view.send(packet);
    return {};
}

// 0x49dc37..0x49dc62, la cola de los casos 1, 2 y 4 de 0x49d0b0 (el 4, la de escribir una carta): con el id que dio
// 0x48ee60 (0, sin caja), la busca (0x490a00) y le manda el 8 y el 9, cada uno con su paso con 1 (0x45aa50).
Core::Expected<void, std::string> PlaceTheNewBox(OldWindowManager& manager, std::int32_t id,
                                                 const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (id == 0)
        return {};
    OldWindow* box = FindTheOldWindow(manager, id);
    if (box == nullptr)
        return {};
    if (auto placed = Steps::PostAndStep(manager, *box, PlaceMessage, globals, view); !placed.HasValue())
        return placed;
    return Steps::PostAndStep(manager, *box, SizeMessage, globals, view);
}

// 0x461f00 (los amigos) y 0x462bd0 (las ventanas), la vtable + 0x88: el renglón bajo el mouse. Da 1: el paso sigue.
std::int32_t HoverTheRow(OldWindowManager& manager, OldWindow& tab, std::int32_t row, std::size_t at,
                         const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldList& list = tab.tab.list;
    const std::size_t size = RowCount(tab);
    const auto width =
        static_cast<std::int32_t>(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0);
    if (!Steps::MouseIn(view, list.base.x, ListRowY(list, size, row) - RowLift, width, RowHeight, TopLeftRect))
        return 1;
    const auto toggle = [](std::int32_t& selected) { selected = (selected + 1) % 2; };
    if (tab.kind == OldWindowKind::LettersTab)
    {
        // 0x464510, la vtable + 0x88 de la lista de las cartas: el clic en la marca (0xc por 0xd al borde) la da vuelta
        // en la copia y en la carta del modelo (0x4a01a0); el clic elige el renglón. El doble clic, salvo sobre la
        // marca, es el 0xd al padre.
        if (globals.leftClick)
        {
            if (Steps::MouseIn(view, list.base.x, ListRowY(list, size, row) - RowLift, LetterMarkWidth, RowHeight,
                               TopLeftRect))
            {
                toggle(tab.tab.letters[at].selected);
                if (LetterEntry* letter = FindALetter(manager.letterModel, tab.tab.letters[at].index);
                    letter != nullptr)
                    letter->selected = tab.tab.letters[at].selected;
            }
            SelectInTheList(list, size, list.scroll + 1 + row);
            globals.leftClick = false;
        }
        if (globals.doubleClick && list.base.x + LetterMarkWidth < view.mouseX)
        {
            PostToTheWindow(manager, list.base.parent, ChosenRowMessage, list.base.id, 0);
            globals.doubleClick = false;
        }
        return 1;
    }
    if (tab.kind == OldWindowKind::WindowsTab)
    {
        if (globals.leftClick)
        {
            SelectInTheList(list, size, list.scroll + 1 + row);
            toggle(tab.tab.windows[at].selected);
            PostToTheWindow(manager, list.base.parent, ChosenRowMessage, list.base.id, 0);
            globals.leftClick = false;
        }
        return 1;
    }
    if (globals.leftClick)
    {
        SelectInTheList(list, size, list.scroll + 1 + row);
        toggle(tab.tab.friends[at].selected);
        globals.leftClick = false;
    }
    if (globals.doubleClick)
    {
        PostToTheWindow(manager, list.base.parent, ChosenRowMessage, list.base.id, 0);
        globals.doubleClick = false;
    }
    if (globals.rightClick)
    {
        SelectInTheList(list, size, list.scroll + 1 + row);
        toggle(tab.tab.friends[at].selected);
        globals.rightClick = false;
        if (view.whisperTo)
            view.whisperTo(FriendName(tab.tab.friends[at].name));
    }
    return 1;
}

// 0x45aa80 sobre la lista de una pestaña: la cola (0x47a9d0 y 0x45abc0), el mouse y 0x47b0f0 con su renglón.
void StepTheList(OldWindowManager& manager, OldWindow& tab, std::int32_t arg, const OldWindowGlobals& globals,
                 const OldWindowStepView& view)
{
    DrainTheList(manager, tab);
    if (arg == 1)
        return;
    OldList& list = tab.tab.list;
    if (!Steps::ClaimTheMouse(manager, list.base, globals, view))
        return;
    const OldListGlobals listGlobals{.leftClick = globals.leftClick,
                                     .wheel = globals.wheel,
                                     .capture = globals.capture,
                                     .focusedList = globals.focusedList,
                                     .mouseOnUi = globals.mouseOnUi,
                                     .sound = globals.sound,
                                     .drawn = manager.drawn == 1,
                                     .modal = manager.modal};
    const OldListView listView{.mouseX = view.mouseX, .mouseY = view.mouseY, .keyDown = [&view](std::uint8_t key) {
                                   return view.keyDown && view.keyDown(key);
                               }};
    static_cast<void>(StepTheListMouse(list, RowCount(tab), listGlobals, listView, [&](std::int32_t row, std::size_t at)
                                       { return HoverTheRow(manager, tab, row, at, globals, view); }));
}

// 0x45aa80 sobre el EDIT: la cola, el mouse y 0x468d50, que es el cursor, la selección y las flechas de un EDIT de
// varios renglones (P-C181).
void StepTheEdit(OldWindowManager& manager, OldEdit& edit, std::int32_t arg, const OldWindowGlobals& globals,
                 const OldWindowStepView& view)
{
    while (!edit.base.queue.empty())
    {
        edit.base.current = edit.base.queue.front();
        edit.base.queue.pop_front();
        Steps::PlaceAgainstTheParent(manager, edit.base);
    }
    if (arg == 1)
        return;
    static_cast<void>(Steps::ClaimTheMouse(manager, edit.base, globals, view));
}

// 0x4a37e0: el texto del EDIT (GetText de 0xff), el EDIT vacío y, con algo escrito, el 11 a la pestaña con el texto
// (el buffer de 0x100 de malloc) y el 4 al administrador para cerrarla.
void ConfirmTheName(OldWindowManager& manager, OldWindow& box)
{
    std::string name(box.add.edit.text.Value());
    box.add.edit.text.Clear();
    if (name.empty())
        return;
    if (OldWindow* owner = FindTheOldWindow(manager, box.add.owner); owner != nullptr)
        owner->base.queue.push_back(OldMessage{.type = NameMessage, .a = box.base.id, .b = 1, .text = std::move(name)});
    PostToTheManager(manager, CloseMessage, box.base.id, 0);
}

// 0x4a2be0, la vtable + 0x28 de la de amigos. Da 0.
Core::Expected<void, std::string> HandleTheFriendsWindowMessage(OldWindowManager& manager, OldWindow& window,
                                                                const OldWindowGlobals& globals,
                                                                const OldWindowStepView& view)
{
    const OldMessage& message = window.base.current;
    if (message.type == RaisedMessage)
    {
        globals.focusedList = window.tabs[static_cast<std::size_t>(window.friends.currentTab)].tab.list.base.id;
        return {};
    }
    // La respuesta a "¿salir de línea?": C4 00 y +0xe0 en 1. 0x4a7280 cierra los chats con un amigo (hito 10).
    if (message.type == AnswerMessage && message.b == 1 && manager.offlineSent == 0)
    {
        if (auto sent = Send(view, FriendSend{.kind = FriendSendKind::State, .value = 0, .name = {}}, "0x4a2be0");
            !sent.HasValue())
            return sent;
        manager.offlineSent = 1;
    }
    return {};
}

// 0x49d0b0, la vtable + 0x28 de la pestaña de amigos. Da 0.
Core::Expected<void, std::string> HandleTheFriendTabMessage(OldWindowManager& manager, OldWindow& tab,
                                                            const OldWindowGlobals& globals,
                                                            const OldWindowStepView& view)
{
    OldMessage& message = tab.base.current;
    // El doble clic sobre un amigo es el botón del chat.
    if (message.type == ChosenRowMessage)
    {
        globals.sound = ButtonSound;
        message.type = CommandMessage;
        message.a = ThirdCommand;
    }
    if (message.type == NameMessage)
    {
        // El nombre de la caja de agregar: C1 0D C1 y free del buffer.
        if (message.b == 0)
            return {};
        return Send(view, FriendSend{.kind = FriendSendKind::Add, .value = 0, .name = FriendName(message.text)},
                    "0x49d0b0(11)");
    }
    if (message.type == AnswerMessage)
    {
        // "¿Borrar?" con sí: C1 0D C3 con el elegido.
        if (message.b != 1)
            return {};
        const FriendEntry* selected = SelectedFriend(tab);
        if (selected == nullptr)
            return {};
        return Send(view, FriendSend{.kind = FriendSendKind::Delete, .value = 0, .name = selected->name},
                    "0x49d0b0(12)");
    }
    if (message.type != CommandMessage || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case FirstCommand:
    {
        // Agregar: la caja (0x48ee60(4)) con el texto 1023, de la pestaña; su id a +0xf0.
        const std::string title = view.texts != nullptr ? view.texts->At(TypeTheNameText) : std::string();
        auto created = CreateFromAStep(
            manager,
            OldWindowRequest{.kind = AddFriendCase, .x = -1, .y = -1, .data = title, .aux = tab.base.id, .flags = 0},
            globals, view, "0x49d0b0(1)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        manager.letter = created.Value();
        return PlaceTheNewBox(manager, created.Value(), globals, view);
    }
    case SecondCommand:
    {
        // Borrar: "<nombre><1024>" en una caja modal de dos botones (0x48ee60(5)) que le responde a la pestaña.
        const FriendEntry* selected = SelectedFriend(tab);
        if (selected == nullptr)
            return {};
        const std::string question =
            selected->name + (view.texts != nullptr ? view.texts->At(ReallyDeleteText) : std::string());
        auto created = CreateFromAStep(
            manager,
            OldWindowRequest{
                .kind = ModalQuestionCase, .x = -1, .y = -1, .data = question, .aux = tab.base.id, .flags = 0},
            globals, view, "0x49d0b0(2)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        return PlaceTheNewBox(manager, created.Value(), globals, view);
    }
    case ThirdCommand:
    {
        // El chat con el elegido, si está en línea (0x4a6430, 0x4a6af0 y 0x4a6910, C1 CA; UI/LetterWindows).
        const FriendEntry* selected = SelectedFriend(tab);
        if (selected == nullptr || selected->state > LastOnlineState)
            return {};
        return Steps::ChatWithTheFriend(manager, selected->name, view);
    }
    case FourthCommand:
    {
        // 0x49dbd4: la ventana de escribir con el nombre del elegido (UI/LetterWindows) y la cola 0x49dc37.
        auto opened = Steps::WriteToTheFriend(manager, SelectedFriend(tab), globals, view);
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return PlaceTheNewBox(manager, opened.Value(), globals, view);
    }
    default:
        return {};
    }
}

// 0x49ea50, la vtable + 0x28 de la pestaña de las ventanas. Da 0.
Core::Expected<void, std::string> HandleTheWindowsTabMessage(OldWindowManager& manager, OldWindow& tab,
                                                             const OldWindowGlobals& globals,
                                                             const OldWindowStepView& view)
{
    const OldMessage& message = tab.base.current;
    if (message.type == CommandMessage)
    {
        // Esconder todas menos la de amigos, sin anotarlas para F6.
        if (manager.modal == 0 && message.a == FirstCommand)
            return HideTheOldWindows(manager, 1, 0, globals, &view);
        return {};
    }
    if (message.type != ChosenRowMessage)
        return {};
    const std::int32_t id = SelectedWindow(tab);
    const OldWindow* chosen = FindTheOldWindow(manager, id);
    // 0x49ea7f: 0x45a9c0 sobre lo que dio 0x490a00, sin mirar si es NULL.
    if (chosen == nullptr)
        return Cut("0x49ea7f: el renglón de una ventana que ya no está (0x45a9c0 sobre NULL)");
    // La de arriba a la vista se minimiza; si no, se sube.
    if (chosen->base.state != HiddenWindow && SelectedWindow(tab) == TopOldWindow(manager))
    {
        PostToTheManager(manager, MinimizeMessage, SelectedWindow(tab), 0);
        return {};
    }
    globals.sound = ButtonSound;
    PostToTheManager(manager, RaiseMessage, SelectedWindow(tab), 0);
    ForgetTheHiddenWindows(manager);
    globals.leftHeld = false;
    return {};
}

// 0x4a3850, la vtable + 0x28 de la caja de agregar. Da 0.
Core::Expected<void, std::string> HandleTheAddBoxMessage(OldWindowManager& manager, OldWindow& box,
                                                         const OldWindowGlobals& globals)
{
    const OldMessage& message = box.base.current;
    if (message.type == RaisedMessage)
        FocusTheEdit(manager, box.add.edit, globals);
    else if (message.type == EnterMessage || (message.type == CommandMessage && message.a == FirstCommand))
        ConfirmTheName(manager, box);
    else if (message.type == CommandMessage && message.a == SecondCommand)
        PostToTheManager(manager, CloseMessage, box.base.id, 0);
    return {};
}

// 0x4a3030, la vtable + 0x58 de la de amigos: los encabezados de las pestañas y el botón del estado.
Core::Expected<void, std::string> AfterTheFriendsWindowMove(OldWindowManager& manager, OldWindow& window,
                                                            const OldWindowGlobals& globals,
                                                            const OldWindowStepView& view)
{
    if (window.base.state == ResizedWindow)
        return {};
    std::optional<std::int32_t> header;
    for (std::size_t at = 0; at < HeaderX.size(); ++at)
    {
        if (Steps::MouseIn(view, Steps::ClientX(window, HeaderX[at]), Steps::ClientY(window, HeaderY), HeaderWidth,
                           HeaderHeight, TopLeftRect))
        {
            header = static_cast<std::int32_t>(at);
            break;
        }
    }
    if (!header)
    {
        window.friends.hoverTab = window.friends.currentTab;
        if (view.measure == nullptr || view.texts == nullptr)
            return Cut("0x4a3030: el botón del estado sin la medida de su texto");
        const std::string label = view.texts->At(StateButtonText);
        const Render::Text::TextExtent extent = view.measure->measure.Measure(view.measure->dcFont, label);
        const std::int32_t width = Render::Text::DesignX(extent.width, view.measure->screen);
        const std::int32_t x = Steps::ClientX(window, 0) + Steps::ClientWidth(window) - StateButtonPad - width;
        if (!Steps::MouseIn(view, x, Steps::ClientY(window, StateButtonY), width + StateButtonPad, StateButtonHeight,
                            TopLeftRect))
            return {};
        if (!globals.releasedClick)
            return {};
        globals.sound = ButtonSound;
        if (manager.offlineSent != 1)
        {
            // "¿Salir de línea?": la respuesta vuelve a la ventana como el 12 (0x4a2be0).
            const std::string question = view.texts->At(GoOfflineText);
            auto created = CreateFromAStep(
                manager,
                OldWindowRequest{
                    .kind = ModalQuestionCase, .x = -1, .y = -1, .data = question, .aux = window.base.id, .flags = 0},
                globals, view, "0x4a3030");
            if (!created.HasValue())
                return Core::Unexpected(std::move(created).Error());
            globals.releasedClick = false;
            return {};
        }
        if (auto sent = Send(view, FriendSend{.kind = FriendSendKind::State, .value = 1, .name = {}}, "0x4a3030");
            !sent.HasValue())
            return sent;
        manager.offlineSent = 0;
        globals.releasedClick = false;
        return {};
    }
    window.friends.hoverTab = *header;
    if (!globals.leftHeld)
        return {};
    const bool changed = window.friends.currentTab != *header;
    if (changed)
        globals.sound = ButtonSound;
    window.friends.currentTab = *header;
    globals.focusedList = window.tabs[static_cast<std::size_t>(*header)].tab.list.base.id;
    globals.leftHeld = false;
    // 0x4a23c0 vuelve a poner la lista de los amigos en 0x5762ec0 (Q-C883).
    if (auto settled = Steps::SettleTheFriendsKinds(manager, window, globals, view); !settled.HasValue())
        return settled;
    if (!changed)
        return {};
    for (const std::size_t at :
         {std::size_t{FriendTabNumber}, std::size_t{WindowsTabNumber}, std::size_t{LettersTabNumber}})
    {
        if (auto settled = Steps::SettleTheFriendsKinds(manager, window.tabs[at], globals, view); !settled.HasValue())
            return settled;
    }
    return {};
}

// 0x49dcf0, la vtable + 0x58 de la pestaña de amigos: los encabezados de las columnas ordenan por nombre o por estado.
void AfterTheFriendTabMove(OldWindowManager& manager, OldWindow& tab, const OldWindowGlobals& globals,
                           const OldWindowStepView& view)
{
    if (!globals.leftHeld)
        return;
    const OldList& list = tab.tab.list;
    const std::int32_t x = Steps::ClientX(tab, 0);
    const std::int32_t y = Steps::ClientY(tab, 0);
    if (Steps::MouseIn(view, x, y, list.nameWidth, ColumnHeight, TopLeftRect))
    {
        if (manager.friendModel.sortMode != ByNameSort)
        {
            globals.sound = ButtonSound;
            SortTheFriends(manager.friendModel, ByNameSort);
            RefreshTheTab(manager, tab);
        }
        globals.leftHeld = false;
        return;
    }
    if (!Steps::MouseIn(view, x + list.nameWidth, y, list.placeWidth, ColumnHeight, TopLeftRect))
        return;
    if (manager.friendModel.sortMode != ByStateSort)
    {
        globals.sound = ButtonSound;
        SortTheFriends(manager.friendModel, ByNameSort);
        SortTheFriends(manager.friendModel, ByStateSort);
        RefreshTheTab(manager, tab);
    }
    globals.leftHeld = false;
}

// Los 8, el 9 dos veces y el 0xe de 0x49ca40, 0x49e690 y 0x4a0830: los botones y la lista contra la pestaña.
void SettleTheTab(OldWindowManager& manager, OldWindow& tab)
{
    for (OldButton& button : tab.tab.buttons)
        Steps::PostAndSettle(manager, button, PlaceMessage);
    PostToTheList(manager, tab, PlaceMessage);
    PostToTheList(manager, tab, SizeMessage);
    PostToTheList(manager, tab, SizeMessage);
    PostToTheList(manager, tab, ToTheEndMessage);
}

// El 8 o el 9 de 0x45aa50 sobre una pestaña sin los globales del paso: su vtable + 0x28 no hace nada con ellos
// (0x49d0b0, 0x49ea50 y 0x4a11b0 dan 0) y su paso con 1 solo vacía las colas de sus hijas. Lo que no es 8 ni 9 queda
// en la cola para el próximo paso (P-C186).
void PlaceTheTabForTheDraw(OldWindowManager& manager, OldWindow& tab, std::int32_t type)
{
    std::deque<OldMessage> rest;
    tab.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!tab.base.queue.empty())
    {
        tab.base.current = tab.base.queue.front();
        tab.base.queue.pop_front();
        if (tab.base.current.type != PlaceMessage && tab.base.current.type != SizeMessage)
        {
            rest.push_back(tab.base.current);
            continue;
        }
        Steps::PlaceAgainstTheParent(manager, tab.base);
    }
    tab.base.queue = std::move(rest);
    DrainTheList(manager, tab);
    for (OldButton& button : tab.tab.buttons)
        DrainTheButton(manager, button);
}
} // namespace

namespace Steps
{
bool IsAFriendsKind(OldWindowKind kind)
{
    return kind == OldWindowKind::FriendsWindow || kind == OldWindowKind::FriendTab ||
           kind == OldWindowKind::WindowsTab || kind == OldWindowKind::LettersTab ||
           kind == OldWindowKind::AddFriendBox;
}

Core::Expected<void, std::string> HandleTheFriendsMessage(OldWindowManager& manager, OldWindow& window,
                                                          const OldWindowGlobals& globals,
                                                          const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::FriendsWindow:
        return HandleTheFriendsWindowMessage(manager, window, globals, view);
    case OldWindowKind::FriendTab:
        return HandleTheFriendTabMessage(manager, window, globals, view);
    case OldWindowKind::WindowsTab:
        return HandleTheWindowsTabMessage(manager, window, globals, view);
    case OldWindowKind::LettersTab:
        return HandleTheLettersTabMessage(manager, window, globals, view);
    case OldWindowKind::AddFriendBox:
        return HandleTheAddBoxMessage(manager, window, globals);
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheFriendsChildren(OldWindowManager& manager, OldWindow& window, std::int32_t arg,
                                                         const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::FriendsWindow:
    {
        // 0x4a2f70: la pestaña a la vista, si no se la arrastra ni cambia de tamaño; después la F la cierra.
        const std::int32_t current = window.friends.currentTab;
        if (window.base.state != DraggedWindow && window.base.state != ResizedWindow && current >= 0 &&
            current < static_cast<std::int32_t>(FriendsTabCount))
        {
            if (auto stepped =
                    StepTheWindow(manager, window.tabs[static_cast<std::size_t>(current)], arg, globals, view);
                !stepped.HasValue())
                return stepped;
        }
        if (!view.chatOpen && LastOldWindow(manager) == window.base.id && view.keyEdge && view.keyEdge(FriendsKey))
        {
            const OldWindow* self = FindTheOldWindow(manager, window.base.id);
            if (self != nullptr && self->base.state == ShownWindow)
                PostToTheManager(manager, CloseMessage, window.base.id, 0);
        }
        return {};
    }
    case OldWindowKind::FriendTab:
    case OldWindowKind::WindowsTab:
        // 0x49dc90 y 0x49eb20: la lista y los botones.
        StepTheList(manager, window, arg, globals, view);
        for (OldButton& button : window.tab.buttons)
            StepTheButton(manager, button, arg, globals, view);
        return {};
    case OldWindowKind::LettersTab:
        // 0x4a2010: la lista y los cuatro botones.
        StepTheList(manager, window, arg, globals, view);
        for (OldButton& button : window.tab.buttons)
            StepTheButton(manager, button, arg, globals, view);
        return {};
    case OldWindowKind::AddFriendBox:
        // 0x4a38a0: los dos botones y el EDIT.
        for (OldButton& button : window.add.buttons)
            StepTheButton(manager, button, arg, globals, view);
        StepTheEdit(manager, window.add.edit, arg, globals, view);
        return {};
    default:
        return {};
    }
}

Core::Expected<void, std::string> AfterTheFriendsMove(OldWindowManager& manager, OldWindow& window,
                                                      const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (window.kind == OldWindowKind::FriendsWindow)
        return AfterTheFriendsWindowMove(manager, window, globals, view);
    // 0x49eb50 y 0x4a38e0 no hacen nada; 0x4a2070, en UI/LetterWindows.
    if (window.kind == OldWindowKind::FriendTab)
        AfterTheFriendTabMove(manager, window, globals, view);
    if (window.kind == OldWindowKind::LettersTab)
        AfterTheLettersTabMove(manager, window, globals, view);
    return {};
}

Core::Expected<void, std::string> SettleTheFriendsKinds(OldWindowManager& manager, OldWindow& window,
                                                        const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::FriendsWindow:
    {
        // 0x4a23c0: el 8 y el 9 a las tres (+0xb8, +0x4d0 y +0x738), su vtable + 0x30 y la lista de los amigos a
        // 0x5762ec0.
        constexpr std::array<std::size_t, FriendsTabCount> Order{FriendTabNumber, WindowsTabNumber, LettersTabNumber};
        for (const std::int32_t type : {PlaceMessage, SizeMessage})
        {
            for (const std::size_t at : Order)
            {
                if (auto stepped = PostAndStep(manager, window.tabs[at], type, globals, view); !stepped.HasValue())
                    return stepped;
            }
        }
        for (const std::size_t at : Order)
        {
            if (auto settled = SettleTheFriendsKinds(manager, window.tabs[at], globals, view); !settled.HasValue())
                return settled;
        }
        globals.focusedList = window.tabs[FriendTabNumber].tab.list.base.id;
        return {};
    }
    case OldWindowKind::FriendTab:
    case OldWindowKind::WindowsTab:
    case OldWindowKind::LettersTab:
        SettleTheTab(manager, window);
        return {};
    case OldWindowKind::AddFriendBox:
        // 0x4a3700: los botones y el EDIT, el foco al EDIT y +0x9c en 1.
        for (OldButton& button : window.add.buttons)
            PostAndSettle(manager, button, PlaceMessage);
        PostToTheEdit(manager, window.add.edit, PlaceMessage);
        FocusTheEdit(manager, window.add.edit, globals);
        window.frame.editField = 1;
        return {};
    default:
        return {};
    }
}

Core::Expected<OldWindow, std::string> MakeTheFriendsWindow(OldWindowManager& manager, const OldWindowRequest& request,
                                                            const OldWindowTexts& texts,
                                                            const OldWindowMeasure& measure)
{
    OldWindow window = MakeTheFrame(manager);
    window.kind = OldWindowKind::FriendsWindow;
    window.tabs.resize(FriendsTabCount);
    // Los campos en orden: +0xb8 (amigos, 0x48fa60), +0x4d0 (ventanas, 0x48fc80) y +0x738 (cartas, 0x48fd90; su lista
    // y sus botones son del hito 10). Cada una sobre el marco (0x491ca0) con la vtable de las pestañas (0x6b9e80), su
    // lista (0x460970, 0x462010 o 0x463190) y sus botones (0x45ad80).
    window.tabs[FriendTabNumber] = MakeTheTab(manager, OldWindowKind::FriendTab, 4);
    window.tabs[WindowsTabNumber] = MakeTheTab(manager, OldWindowKind::WindowsTab, 1);
    window.tabs[LettersTabNumber] = MakeTheTab(manager, OldWindowKind::LettersTab, 4);
    // 0x460970: los anchos de las dos columnas de la lista de amigos.
    OldList& friendList = window.tabs[FriendTabNumber].tab.list;
    friendList.nameWidth =
        Render::Text::DesignX(measure.measure.Measure(measure.dcFont, WidestName).width, measure.screen) + ColumnMargin;
    friendList.placeWidth =
        Render::Text::DesignX(measure.measure.Measure(measure.dcFont, texts.At(PlaceText)).width, measure.screen) +
        ColumnMargin;
    // 0x463190: +0x130 (el ancho de la columna de la marca) en 0x19 y +0x134..+0x13c, los anchos de los textos 1028,
    // 1029 y 1030 como los de 0x460970.
    OldList& letterList = window.tabs[LettersTabNumber].tab.list;
    letterList.headers = LetterListHeaders;
    for (const auto& [width, text] :
         {std::pair{&letterList.nameWidth, SenderColumnText}, std::pair{&letterList.placeWidth, DateColumnText},
          std::pair{&letterList.subjectWidth, SubjectColumnText}})
        *width = Render::Text::DesignX(measure.measure.Measure(measure.dcFont, texts.At(text)).width, measure.screen) +
                 ColumnMargin;
    // 0x4a2280.
    OldWindowBase& base = window.base;
    window.frame.title = std::string(request.data);
    base.parent = request.aux;
    base.x = FriendsPlace;
    base.y = FriendsPlace;
    base.width = FriendsWidth;
    base.height = FriendsHeight;
    window.frame.maxWidth = 0;
    window.frame.maxHeight = 0;
    window.frame.minWidth = FriendsWidth;
    window.frame.minHeight = FriendsMinHeight;
    InitTheTab(window.tabs[FriendTabNumber], texts.At(FriendTabText), base.id,
               {AddButtonText, DeleteButtonText, ChatButtonText, WriteButtonText}, texts);
    window.tabs[FriendTabNumber].tab.list.headers = 1;
    // 0x49ddd0 desde 0x49c8b0: la lista con el modelo.
    RefreshTheTab(manager, window.tabs[FriendTabNumber]);
    InitTheTab(window.tabs[WindowsTabNumber], texts.At(WindowsTabText), base.id, {WindowsButtonText, 0, 0, 0}, texts);
    // 0x4a06b0: 0x4a0960 copia las cartas del modelo. Los botones, como los de la de amigos (*inferido*).
    InitTheTab(window.tabs[LettersTabNumber], texts.At(LettersTabText), base.id,
               {WriteButtonText, SecondLetterButtonText, ThirdLetterButtonText, FourthLetterButtonText}, texts);
    RefreshTheLetters(manager, window.tabs[LettersTabNumber]);
    for (OldWindow& tab : window.tabs)
    {
        // 0x4908d0: al segundo mapa.
        manager.tabs.insert_or_assign(tab.base.id, base.id);
        tab.base.placeMode = 0;
        tab.base.placeX = 0;
        tab.base.placeY = TabTop;
        tab.base.sizeMode = 3;
        tab.base.sizeX = 0;
        tab.base.sizeY = TabHeightCut;
    }
    return window;
}

OldWindow MakeTheAddFriendBox(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts)
{
    OldWindow box = MakeTheFrame(manager);
    box.kind = OldWindowKind::AddFriendBox;
    box.add.owner = 0;
    box.add.edit.base = MakeTheBase(manager);
    for (OldButton& button : box.add.buttons)
        button = MakeTheButton(manager);
    // 0x4a35a0.
    OldWindowBase& base = box.base;
    box.frame.title = std::string(request.data);
    base.parent = 0;
    box.add.owner = request.aux;
    base.x = AddBoxPlace;
    base.y = AddBoxPlace;
    base.width = AddBoxWidth;
    base.height = AddBoxHeight;
    box.frame.minWidth = AddBoxWidth;
    box.frame.minHeight = AddBoxHeight;
    box.frame.maxWidth = 0;
    box.frame.maxHeight = 0;
    base.flags = AddBoxFlags;
    OldEdit& edit = box.add.edit;
    edit.base.width = EditWidth;
    edit.base.height = EditHeight;
    edit.base.parent = base.id;
    edit.filter = AddNameFilter;
    // 0x4a3645 y 0x4a365e: la fuente normal (+0x34 con 0x5763554) y el fondo en 0 (+0x44(0, 0, 0, 0)): transparente.
    edit.backgroundColor = 0;
    edit.base.placeMode = 0;
    edit.base.placeX = EditX;
    edit.base.placeY = EditY;
    edit.base.state = ShownWindow;
    const auto setUp = [&base, &texts](OldButton& button, std::int32_t command, std::size_t label, std::int32_t x)
    {
        button.command = command;
        button.label = texts.At(label);
        button.base.parent = base.id;
        button.base.placeMode = 0;
        button.base.placeX = x;
        button.base.placeY = AddBoxButtonY;
        button.base.width = AddBoxButtonWidth;
        button.base.height = AddBoxButtonHeight;
    };
    setUp(box.add.buttons[0], FirstCommand, ConfirmText, ConfirmX);
    setUp(box.add.buttons[1], SecondCommand, CancelText, CancelX);
    return box;
}

void AddTheWindowRow(OldWindowManager& manager, std::int32_t id, std::string_view title)
{
    OldWindow* friends = FriendsWindowOf(manager);
    if (friends == nullptr)
        return;
    // 0x4624b0, la vtable + 0x94 de la lista: adelante, con el título en 0x40.
    OldWindow& tab = friends->tabs[WindowsTabNumber];
    OldList& list = tab.tab.list;
    std::deque<WindowRow>& rows = tab.tab.windows;
    const std::size_t end = title.find('\0');
    rows.push_front(WindowRow{
        .selected = 0, .id = id, .title = std::string(title.substr(0, std::min(end, std::size_t{0x40}))), .aux = 0});
    SelectInTheList(list, rows.size(), 0);
    if (list.scroll != 0)
        ++list.scroll;
    const auto count = static_cast<std::int32_t>(rows.size());
    if (list.rows <= count && count - list.scroll < list.rows)
        list.scroll = count - list.rows;
    // Con uno o más, el primero elegido y el corrimiento en 0: lo de arriba no queda (Q-C887).
    SelectInTheList(list, rows.size(), 1);
    list.scroll = 0;
}

void ForgetTheWindowRow(OldWindowManager& manager, std::int32_t id)
{
    OldWindow* friends = FriendsWindowOf(manager);
    if (friends == nullptr)
        return;
    // 0x462c70, la vtable + 0x98: el primero con el id; después la vtable + 0x34 con 0.
    OldWindow& tab = friends->tabs[WindowsTabNumber];
    std::deque<WindowRow>& rows = tab.tab.windows;
    if (const auto found = std::ranges::find(rows, id, &WindowRow::id); found != rows.end())
        rows.erase(found);
    ScrollTheList(tab.tab.list, rows.size(), 0);
}

void FillTheWindowsTab(OldWindowManager& manager)
{
    OldWindow* friends = FriendsWindowOf(manager);
    if (friends == nullptr)
        return;
    // La vtable + 0x2c de la lista (0x47d320): vacía, marca todos los renglones en 0 (+0x48, 0x47da60: sin renglones,
    // nada), elige el 0 (+0x58, 0x47de10, como 0x47aef0) y vuelve el corrimiento (+0x100) a 0.
    OldTabPart& tab = friends->tabs[WindowsTabNumber].tab;
    tab.windows.clear();
    SelectInTheList(tab.list, 0, 0);
    tab.list.scroll = 0;
    for (const auto& [id, window] : manager.windows)
    {
        if (id == manager.friends || window.base.state == MinimizedWindow)
            continue;
        AddTheWindowRow(manager, id, window.frame.title.value_or(std::string()));
    }
}

void SaveTheFriendsPlace(OldWindowManager& manager, const OldWindow& window)
{
    manager.friendsX = window.base.x;
    manager.friendsY = window.base.y;
    manager.friendsWidth = window.base.width;
    manager.friendsHeight = window.base.height;
    manager.friendsStretched = window.frame.stretched;
    manager.friendsSavedY = window.frame.savedY;
    manager.friendsSavedHeight = window.frame.savedHeight;
    manager.lastTab = window.friends.currentTab;
}
} // namespace Steps

Core::Expected<bool, std::string> OpenTheFriendsWindow(OldWindowManager& manager, std::int32_t x, std::int32_t y,
                                                       const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (manager.friends != 0)
    {
        // 0x491318..0x49137f: oculta (3), a la vista con los sonidos; a la vista (0), se suelta (0x4914e0); con otro
        // estado, nada. 0x45a9c0 lee el estado de lo que dio 0x490a00 sin mirar si es NULL.
        const OldWindow* friends = FindTheOldWindow(manager, manager.friends);
        if (friends == nullptr)
            return Core::Unexpected(std::string("0x49131f: 0x45a9c0 sobre la ventana de amigos que ya no está"));
        const std::int32_t state = friends->base.state;
        if (state == HiddenWindow)
        {
            Steps::ShowTheWindow(manager, manager.friends, true);
            // 0x41ed10(0x19) y 0x41ed10(0x1c): queda el último.
            globals.sound = ButtonSound;
            globals.sound = OpenSound;
            return false;
        }
        if (state == ShownWindow)
        {
            if (auto dropped = DropTheFriendsWindow(manager); !dropped.HasValue())
                return Core::Unexpected(std::move(dropped).Error());
        }
        return false;
    }
    if (view.texts == nullptr || view.measure == nullptr)
        return Core::Unexpected(std::string("0x491080: la ventana de amigos sin los textos ni la medida"));
    const std::string title = view.texts->At(static_cast<std::size_t>(manager.titleText));
    const bool first = manager.friendsWidth == 0;
    OldWindowRequest request{.kind = FriendsCase,
                             .x = first ? x : manager.friendsX,
                             .y = first ? y : manager.friendsY,
                             .data = title,
                             .aux = 0,
                             .flags = 0};
    auto created = CreateTheOldWindow(manager, request, *view.texts, *view.measure, &globals, &view);
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    PostToTheManager(manager, RaiseMessage, manager.friends, 0);
    if (first)
    {
        Steps::FillTheWindowsTab(manager);
        globals.sound = ButtonSound;
        globals.sound = OpenSound;
        return true;
    }
    OldWindow* friends = FindTheOldWindow(manager, manager.friends);
    if (friends == nullptr)
        return false;
    // La vtable + 8 (0x45a9f0): el tamaño de antes; y lo estirado.
    friends->base.width = manager.friendsWidth;
    friends->base.height = manager.friendsHeight;
    friends->frame.savedY = manager.friendsSavedY;
    friends->frame.stretched = manager.friendsStretched;
    friends->frame.savedHeight = manager.friendsSavedHeight;
    Steps::FillTheWindowsTab(manager);
    if (auto settled = Steps::SettleTheWindow(manager, *friends, globals, view); !settled.HasValue())
        return Core::Unexpected(std::move(settled).Error());
    globals.sound = ButtonSound;
    globals.sound = OpenSound;
    if (auto stepped = StepTheOldWindows(manager, globals, view); !stepped.HasValue())
        return Core::Unexpected(std::move(stepped).Error());
    return true;
}

void RefreshTheFriendTab(OldWindowManager& manager)
{
    if (OldWindow* friends = FriendsWindowOf(manager); friends != nullptr)
        RefreshTheTab(manager, friends->tabs[FriendTabNumber]);
}

void MarkTheFriendList(OldWindowManager& manager, std::int32_t received, const OldWindowTexts& texts)
{
    manager.listReceived = received;
    const std::int32_t title =
        received == 1 ? static_cast<std::int32_t>(FriendsTitleText) : static_cast<std::int32_t>(ReconnectTitleText);
    if (manager.titleText == title)
        return;
    manager.titleText = title;
    if (OldWindow* friends = FriendsWindowOf(manager); friends != nullptr)
        friends->frame.title = texts.At(static_cast<std::size_t>(title));
}

void RememberTheFriendRequest(OldWindowManager& manager, std::int32_t box, std::string_view name)
{
    if (OldWindow* window = FindTheOldWindow(manager, box); window != nullptr)
        window->box.friendName = FriendName(name);
}

namespace
{
OldEdit* FocusedEdit(OldWindowManager& manager)
{
    for (auto& [id, window] : manager.windows)
    {
        if (window.kind == OldWindowKind::AddFriendBox && window.add.edit.base.id == manager.focusedEdit)
            return &window.add.edit;
        // CL9 hito 10: el EDIT del chat con un amigo y los tres de la ventana de escribir una carta.
        if (window.kind == OldWindowKind::ChatWindow && window.chat.edit.base.id == manager.focusedEdit)
            return &window.chat.edit;
        if (window.kind != OldWindowKind::LetterWriteWindow)
            continue;
        for (OldEdit* edit : {&window.letter.to, &window.letter.subject, &window.letter.text})
        {
            if (edit->base.id == manager.focusedEdit)
                return edit;
        }
    }
    return nullptr;
}
} // namespace

bool HasAnOldEditFocused(const OldWindowManager& manager, ChatFocus focus)
{
    return focus == ChatFocus::OldWindow && manager.focusedEdit != 0;
}

void TypeInTheOldEdit(OldWindowManager& manager, std::string_view typed)
{
    if (OldEdit* edit = FocusedEdit(manager); edit != nullptr)
        edit->text.Type(typed);
}

void BackspaceInTheOldEdit(OldWindowManager& manager)
{
    if (OldEdit* edit = FocusedEdit(manager); edit != nullptr)
        edit->text.Backspace();
}

void PressEnterInTheOldEdit(OldWindowManager& manager)
{
    // 0x467b10: con +0x150 de la lista de ventanas en 1 (F5), Enter es de ella.
    if (manager.windowList.list.armed == 1)
        return;
    OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    PostToTheWindow(manager, edit->base.parent, EnterMessage, 0, 0);
}

void SettleTheFriendsForTheDraw(OldWindowManager& manager, OldWindow& window)
{
    const bool moving = window.base.state == DraggedWindow || window.base.state == ResizedWindow;
    if (window.kind == OldWindowKind::AddFriendBox)
    {
        // 0x4a3750.
        if (!moving)
            return;
        for (OldButton& button : window.add.buttons)
            Steps::PostAndSettle(manager, button, PlaceMessage);
        PostToTheEdit(manager, window.add.edit, PlaceMessage);
        return;
    }
    if (window.kind != OldWindowKind::FriendsWindow)
        return;
    constexpr std::array<std::size_t, FriendsTabCount> Order{FriendTabNumber, WindowsTabNumber, LettersTabNumber};
    // 0x4a2620: el 8 a las tres y su estado en 4; cambiando de tamaño, también el 9 y el estado en 1.
    if (moving)
    {
        for (const std::size_t at : Order)
            PlaceTheTabForTheDraw(manager, window.tabs[at], PlaceMessage);
        for (const std::size_t at : Order)
            window.tabs[at].base.state = DraggedWindow;
        if (window.base.state == ResizedWindow)
        {
            for (const std::size_t at : Order)
                PlaceTheTabForTheDraw(manager, window.tabs[at], SizeMessage);
            for (const std::size_t at : Order)
                window.tabs[at].base.state = ResizedWindow;
        }
    }
    // El dibujo de la pestaña a la vista (0x49cc20, 0x4a0a30 o 0x49e7f0): arrastrada o cambiando de tamaño, el 8 a la
    // lista y a los botones y, cambiando de tamaño, el 9 a la lista.
    const std::int32_t current = window.friends.currentTab;
    if (current < 0 || current >= static_cast<std::int32_t>(FriendsTabCount))
        return;
    OldWindow& tab = window.tabs[static_cast<std::size_t>(current)];
    if (tab.base.state != DraggedWindow && tab.base.state != ResizedWindow)
        return;
    PostToTheList(manager, tab, PlaceMessage);
    for (OldButton& button : tab.tab.buttons)
        Steps::PostAndSettle(manager, button, PlaceMessage);
    if (tab.base.state == ResizedWindow)
        PostToTheList(manager, tab, SizeMessage);
}
} // namespace Client::UI
