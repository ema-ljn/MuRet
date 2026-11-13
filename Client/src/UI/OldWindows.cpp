#include "UI/OldWindows.h"

#include "Core/Expected.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/ChatLogWindow.h"
#include "UI/LetterWindows.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowSteps.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <string>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x459f00: el rectángulo desde arriba a la izquierda.
constexpr std::int32_t TopLeftRect = 1;
// 0x41ed10(0x19, 0, 0): el sonido de los botones.
constexpr std::int32_t ButtonSound = 0x19;
// 0x553520: Esc, F5, F6 y Enter.
constexpr std::uint8_t EscapeKey = 0x1b;
constexpr std::uint8_t F5Key = 0x74;
constexpr std::uint8_t F6Key = 0x75;
constexpr std::uint8_t EnterKey = 0x0d;
// La pantalla de 640 × 480 que acotan el arrastre y el cambio de tamaño (0x492ac0) y la cascada (0x48ee60).
constexpr std::int32_t ScreenWidth = 0x280;
constexpr std::int32_t ScreenHeight = 0x1e0;
// 0x492ac0: los botones de la barra son de 9 × 9 en y + 8, la cruz en x + w − 0x10, el de estirar en x + w − 0x1b y
// el de minimizar en el mismo lugar o, con el de estirar, en x + w − 0x26; las esquinas del tamaño, de 7 arriba a la
// izquierda y de 10 abajo a la derecha, y la barra del arrastre, de 0x14 de alto.
constexpr std::int32_t TitleButtonSize = 9;
constexpr std::int32_t TitleButtonY = 8;
constexpr std::int32_t CloseButtonX = 0x10;
constexpr std::int32_t StretchButtonX = 0x1b;
constexpr std::int32_t MinimizeAloneX = 0x1b;
constexpr std::int32_t MinimizeWithStretchX = 0x26;
constexpr std::int32_t TopLeftGrip = 7;
constexpr std::int32_t BottomRightGrip = 10;
constexpr std::int32_t DragBarHeight = 0x14;
// +0x84 del marco: la esquina agarrada.
constexpr std::int32_t BottomRightCorner = 0x87;
constexpr std::int32_t TopLeftCorner = 0x13b;
// 0x48f890..0x48f8e0, la vtable + 0x3c..+0x48 del marco: el área de adentro empieza en (x + 5, y + 0x15) y mide
// w − 10 × h − 0x1a.
constexpr std::int32_t ClientLeft = 5;
constexpr std::int32_t ClientTop = 0x15;
constexpr std::int32_t ClientWidthCut = 10;
constexpr std::int32_t ClientHeightCut = 0x1a;
// 0x4930f0: estirada, la ventana va de y = 0 hasta la barra de abajo.
constexpr std::int32_t StretchedHeight = 0x1b0;
// 0x48ee60: la cascada corre 20 cada vez; sin lugar, vuelve a (r + 1, r + 1) con r = y % 10, y con r = 9 no la arma.
constexpr std::int32_t CascadeStep = 0x14;
constexpr std::int32_t CascadeWrap = 10;
constexpr std::int32_t CascadeGiveUp = 9;
// 0x48ee60: la y por omisión.
constexpr std::int32_t DefaultY = 0x14c;
// 0x4920a0: el marco solo, en (0x32, 0x32) de 0xd5 × 0xaa.
constexpr std::int32_t FramePlace = 0x32;
constexpr std::int32_t FrameWidth = 0xd5;
constexpr std::int32_t FrameHeight = 0xaa;
// 0x491ca0: todas las banderas y el mínimo de 100 × 100.
constexpr std::int32_t AllFrameFlags = 0x3f;
// 0x45ad80: el botón de 0x46 × 0x14.
constexpr std::int32_t ButtonWidth = 0x46;
constexpr std::int32_t ButtonHeight = 0x14;
// 0x4a38f0: la caja en (0x32, 0x32) de 0x96 × 100, que no se achica de ahí, con la barra, el borde y el arrastre;
// el texto en 2 renglones de 0x7d; los botones de 0x32 × 0x14 en (0x12, 0x28) y (0x49, 0x28) del área de adentro, o
// el único en (0x2d, 0x28).
constexpr std::int32_t BoxPlace = 0x32;
constexpr std::int32_t BoxWidth = 0x96;
constexpr std::int32_t BoxHeight = 100;
constexpr std::int32_t BoxFlags = TitleFlag | BorderFlag | DragFlag;
constexpr int BoxWrapWidth = 0x7d;
constexpr int BoxLines = 2;
constexpr std::int32_t BoxButtonWidth = 0x32;
constexpr std::int32_t BoxButtonHeight = 0x14;
constexpr std::int32_t YesButtonX = 0x12;
constexpr std::int32_t NoButtonX = 0x49;
constexpr std::int32_t OnlyButtonX = 0x2d;
constexpr std::int32_t BoxButtonsY = 0x28;
// 0x4a3b80: los comandos de los dos botones.
constexpr std::int32_t YesCommand = 1;
constexpr std::int32_t NoCommand = 2;
// 0x4a48b0: la lista de ventanas, en (0x246, 0x1b3) de 0x34 de ancho.
constexpr std::int32_t WindowListX = 0x246;
constexpr std::int32_t WindowListY = 0x1b3;
constexpr std::int32_t WindowListWidth = 0x34;
// 0x4a5e10: oculta, el parpadeo baja de a 0,25.
constexpr float WindowListBlinkDown = -0.25f;

} // namespace

namespace Steps
{
bool MouseIn(const OldWindowStepView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
             std::int32_t mode)
{
    return MouseInRect(view.mouseX, view.mouseY, x, y, width, height, mode);
}

bool IsShowing(std::int32_t state)
{
    return state != HiddenWindow && state != MinimizedWindow;
}

std::int32_t NextId(OldWindowManager& manager)
{
    return ++manager.lastId;
}

// 0x45a210.
OldWindowBase MakeTheBase(OldWindowManager& manager)
{
    OldWindowBase base;
    base.id = NextId(manager);
    return base;
}

// 0x45ad80.
OldButton MakeTheButton(OldWindowManager& manager)
{
    OldButton button;
    button.base = MakeTheBase(manager);
    button.base.width = ButtonWidth;
    button.base.height = ButtonHeight;
    return button;
}

// 0x491ca0.
OldWindow MakeTheFrame(OldWindowManager& manager)
{
    OldWindow window;
    window.base = MakeTheBase(manager);
    window.base.flags = AllFrameFlags;
    return window;
}

// 0x48ee60, casos 5, 8, 9 y 10: el marco, +0xb0 y +0xb4, los dos botones (0x45ad80) y la vtable 0x6b9cb4.
OldWindow MakeTheMessageBox(OldWindowManager& manager, std::int32_t oneButton)
{
    OldWindow window = MakeTheFrame(manager);
    window.kind = OldWindowKind::MessageBox;
    window.box.owner = 0;
    window.box.oneButton = oneButton;
    window.box.buttons[0] = MakeTheButton(manager);
    window.box.buttons[1] = MakeTheButton(manager);
    return window;
}

// La vtable + 0x3c..+0x48: 0x48f890..0x48f8e0 en el marco y sus derivadas; 0x48fb40..0x48fb70 en las pestañas de la
// de amigos, sin borde ni barra.
bool HasNoInsets(const OldWindow& window)
{
    return window.kind == OldWindowKind::FriendTab || window.kind == OldWindowKind::WindowsTab ||
           window.kind == OldWindowKind::LettersTab;
}

std::int32_t ClientX(const OldWindow& window, std::int32_t offset)
{
    return window.base.x + offset + (HasNoInsets(window) ? 0 : ClientLeft);
}

std::int32_t ClientY(const OldWindow& window, std::int32_t offset)
{
    return window.base.y + offset + (HasNoInsets(window) ? 0 : ClientTop);
}

std::int32_t ClientWidth(const OldWindow& window)
{
    return window.base.width - (HasNoInsets(window) ? 0 : ClientWidthCut);
}

std::int32_t ClientHeight(const OldWindow& window)
{
    return window.base.height - (HasNoInsets(window) ? 0 : ClientHeightCut);
}

// 0x45abc0, la vtable + 0x24 de todas: el 8 ubica y el 9 mide contra el área de adentro del padre.
void PlaceAgainstTheParent(OldWindowManager& manager, OldWindowBase& base)
{
    const std::int32_t type = base.current.type;
    if ((type != PlaceMessage && type != SizeMessage) || base.parent == 0)
        return;
    const OldWindow* parent = FindTheOldWindow(manager, base.parent);
    if (parent == nullptr)
        return;
    const OldWindow& p = *parent;
    if (type == PlaceMessage)
    {
        switch (base.placeMode)
        {
        case 0:
            base.x = ClientX(p, base.placeX);
            base.y = ClientY(p, base.placeY);
            return;
        case 1:
            base.y = ClientY(p, base.placeY);
            base.x = ClientX(p, -base.placeX) + ClientWidth(p);
            return;
        case 2:
            base.y = ClientY(p, -base.placeY) + ClientHeight(p);
            base.x = ClientX(p, base.placeX);
            return;
        case 3:
            base.y = ClientY(p, -base.placeY) + ClientHeight(p);
            base.x = ClientX(p, -base.placeX) + ClientWidth(p);
            return;
        default:
            return;
        }
    }
    switch (base.sizeMode)
    {
    case 0:
        if (base.sizeX == 0 && base.sizeY == 0)
            return;
        base.width = base.sizeX;
        base.height = base.sizeY;
        return;
    case 1:
        base.width = ClientWidth(p) + base.sizeX;
        base.height = base.sizeY;
        return;
    case 2:
        base.width = base.sizeX;
        base.height = ClientHeight(p) + base.sizeY;
        return;
    case 3:
        base.height = ClientHeight(p) + base.sizeY;
        base.width = ClientWidth(p) + base.sizeX;
        return;
    default:
        return;
    }
}

// 0x45aa80 desde la cola hasta el rectángulo: el mouse sobre la ventana la hace la de abajo si no hay otra (y nadie
// agarró el mouse o lo agarró ella); a la vista, siendo la de abajo o la que agarró, con el izquierdo abajo le pide al
// administrador que suba la de más arriba de su familia (0x490a00 por los padres hasta la que no tiene, y 0x490aa0).
// Devuelve falso donde el original sale con 0 sin la vtable + 0x20.
bool ClaimTheMouse(OldWindowManager& manager, const OldWindowBase& base, const OldWindowGlobals& globals,
                   const OldWindowStepView& view)
{
    if (!MouseIn(view, base.x, base.y, base.width, base.height, base.rectMode))
        return true;
    if (globals.hovered == 0)
    {
        if (globals.capture != 0 && globals.capture != base.id)
            return false;
        globals.hovered = base.id;
    }
    if (base.state != ShownWindow)
        return true;
    if (globals.hovered != base.id && globals.capture != base.id)
        return false;
    if (!globals.leftHeld)
        return true;
    const OldWindowBase* root = &base;
    if (base.parent != 0)
    {
        const OldWindow* parent = FindTheOldWindow(manager, base.parent);
        root = parent != nullptr ? &parent->base : nullptr;
    }
    if (root == nullptr)
        return true;
    while (root->parent != 0)
    {
        const OldWindow* parent = FindTheOldWindow(manager, root->parent);
        if (parent == nullptr)
            return true;
        root = &parent->base;
    }
    if (HasTheOldWindow(manager, root->id))
        PostToTheManager(manager, RaiseMessage, root->id, 0);
    return true;
}

// 0x45b200, la vtable + 0x20 del botón: el clic que empieza sobre él lo aprieta (0x5762ec4) y el soltado sobre él le
// manda su comando al padre (0x490b40 con el 10) con el 0x19; el mouse fuera de él con el botón apretado suelta todo.
void StepTheButtonMouse(OldWindowManager& manager, OldButton& button, const OldWindowGlobals& globals,
                        const OldWindowStepView& view)
{
    OldWindowBase& base = button.base;
    if (base.state == DisabledWindow)
        return;
    if (MouseIn(view, base.x, base.y, base.width, base.height, TopLeftRect))
    {
        globals.mouseOnUi = 1;
        if (globals.leftClick && globals.pressedButton == 0)
        {
            globals.pressedButton = base.id;
            button.pressed = 1;
        }
        if (globals.releasedClick && globals.pressedButton == base.id)
        {
            PostToTheWindow(manager, base.parent, CommandMessage, button.command, 0);
            globals.releasedClick = false;
            globals.sound = ButtonSound;
            button.pressed = 0;
            globals.pressedButton = 0;
        }
        return;
    }
    if (button.pressed != 1)
        return;
    globals.leftHeld = false;
    globals.leftClick = false;
    globals.releasedClick = false;
    globals.pressedButton = 0;
    button.pressed = 0;
}

// 0x45aa80 sobre un botón: la cola (0x45a640 da 0 y atiende 0x45abc0), la vtable + 0x1c (0x403160, nada), el mouse y
// 0x45b200.
void StepTheButton(OldWindowManager& manager, OldButton& button, std::int32_t arg, const OldWindowGlobals& globals,
                   const OldWindowStepView& view)
{
    OldWindowBase& base = button.base;
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        PlaceAgainstTheParent(manager, base);
    }
    if (arg == 1)
        return;
    if (!ClaimTheMouse(manager, base, globals, view))
        return;
    StepTheButtonMouse(manager, button, globals, view);
}

// 0x45aa50 sobre un botón: el mensaje y su paso con 1 (0x45aa80), que atiende la cola y no llega al mouse.
void PostAndSettle(OldWindowManager& manager, OldButton& button, std::int32_t type)
{
    button.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!button.base.queue.empty())
    {
        button.base.current = button.base.queue.front();
        button.base.queue.pop_front();
        PlaceAgainstTheParent(manager, button.base);
    }
}

// La vtable + 0x30: 0x48faf0 (nada) en el marco y en la lista, y 0x4a3a50 en la caja: ubica los dos botones (el 8 y
// su paso con 1) y pone 0x77f3708 en 1, que nadie lee (*estático*; MuRet no lo guarda). Las de amigos, en
// UI/FriendWindows.
Core::Expected<void, std::string> SettleTheWindow(OldWindowManager& manager, OldWindow& window,
                                                  const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (IsAFriendsKind(window.kind))
        return SettleTheFriendsKinds(manager, window, globals, view);
    if (IsALetterKind(window.kind))
        return SettleTheLetterKinds(manager, window, globals, view);
    if (window.kind == OldWindowKind::MessageBox)
        SettleTheButtons(manager, window);
    return {};
}

// 0x4930f0, la vtable + 0x38 del marco y de la caja: estira la ventana de y = 0 hasta la barra, o la vuelve.
Core::Expected<void, std::string> StretchTheWindow(OldWindowManager& manager, OldWindow& window,
                                                   const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    if (frame.stretched == 0)
    {
        frame.savedHeight = base.height;
        frame.savedY = base.y;
        base.y = 0;
        base.height = StretchedHeight;
        if (auto settled = SettleTheWindow(manager, window, globals, view); !settled.HasValue())
            return settled;
        frame.stretched = 1;
        return {};
    }
    base.height = frame.savedHeight;
    base.y = frame.savedY;
    if (auto settled = SettleTheWindow(manager, window, globals, view); !settled.HasValue())
        return settled;
    frame.stretched = 0;
    return {};
}

// 0x4a3b80, la vtable + 0x28 de la caja: el comando de un botón. El 1 (y el 2 con dos botones) le avisa al dueño
// (el 12 con el id y 1 o 0) y le pide al administrador que la cierre. Siempre da 0: después corre 0x45abc0.
Core::Expected<void, std::string> AnswerTheBox(OldWindowManager& manager, OldWindow& window,
                                               const OldWindowStepView& view)
{
    const OldMessage& message = window.base.current;
    if (message.type != CommandMessage)
        return {};
    if (message.a != YesCommand && (message.a != NoCommand || window.box.oneButton != 0))
        return {};
    // 0x4a3bde y 0x4a3fc8: la respuesta a un pedido de amistad (C1 0E C2, 1 o 0, y el nombre de +0x2b8).
    if (window.box.owner == -1)
    {
        if (!view.send)
            return Core::Unexpected(std::string("0x4a3b80: la respuesta a un pedido de amistad (C1 C2) sin el cable"));
        view.send(FriendSend{.kind = FriendSendKind::Answer,
                             .value = static_cast<std::uint8_t>(message.a == YesCommand ? 1 : 0),
                             .name = window.box.friendName});
    }
    else if (window.box.owner != 0)
        PostToTheWindow(manager, window.box.owner, AnswerMessage, window.base.id, message.a == YesCommand ? 1 : 0);
    PostToTheManager(manager, CloseMessage, window.base.id, 0);
    return {};
}

// La vtable + 0x1c: 0x48fb10 (nada) en el marco; 0x4a43c0 en la caja (Enter, siendo la del final de la lista, se
// manda el 10 con 1; y el paso de los dos botones, también el del que no se usa), y 0x4a4e20 en la lista de ventanas
// (con +0x150 en 1, Enter la oculta: sin entradas, sin subir ninguna). Las de amigos, en UI/FriendWindows.
Core::Expected<void, std::string> BeforeTheMouse(OldWindowManager& manager, OldWindow& window, std::int32_t arg,
                                                 const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (IsAFriendsKind(window.kind))
        return StepTheFriendsChildren(manager, window, arg, globals, view);
    if (IsALetterKind(window.kind))
        return StepTheLetterKindsChildren(manager, window, arg, globals, view);
    if (window.kind == OldWindowKind::MessageBox)
    {
        if (LastOldWindow(manager) == window.base.id && view.keyEdge && view.keyEdge(EnterKey))
            window.base.queue.push_back(OldMessage{.type = CommandMessage, .a = YesCommand, .b = 0, .text = {}});
        for (OldButton& button : window.box.buttons)
            StepTheButton(manager, button, arg, globals, view);
        return {};
    }
    if (window.kind == OldWindowKind::WindowList && window.list.armed == 1 && view.keyEdge && view.keyEdge(EnterKey))
    {
        HideTheWindowList(window);
        window.list.armed = 0;
    }
    return {};
}

// La vtable + 0x58: 0x48fb20 y 0x4a4430 (nada) en el marco y en la caja, y 0x495520, 0x49b210 y 0x499830 (ret) en el
// chat y en las de una carta; 0x4a4eb0 en la lista, que solo hace algo a la vista (sus renglones, P-C215). Las de
// amigos, en UI/FriendWindows.
Core::Expected<void, std::string> AfterTheMove(OldWindowManager& manager, OldWindow& window,
                                               const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (IsAFriendsKind(window.kind))
        return AfterTheFriendsMove(manager, window, globals, view);
    if (window.kind == OldWindowKind::WindowList && window.base.state == ShownWindow)
        return Core::Unexpected(
            std::string("0x4a4eb0: la lista de ventanas a la vista (sus renglones) es de la CL9.4"));
    return {};
}

// El cambio de tamaño desde abajo a la derecha (0x492ac0, +0x84 en 0x87): acotado a la pantalla, al mínimo y al
// máximo; el punto agarrado solo sigue al mouse si no se pasó.
void ResizeFromTheBottomRight(OldWindow& window, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    std::int32_t width = 0;
    if (base.width - frame.grabX + base.x + view.mouseX < ScreenWidth + 1)
        width = base.width - frame.grabX + view.mouseX;
    else
        width = ScreenWidth - base.x;
    base.width = width;
    if (width < frame.minWidth)
        base.width = frame.minWidth;
    else if (frame.maxWidth < 1 || width <= frame.maxWidth)
        frame.grabX = view.mouseX;
    else
        base.width = frame.maxWidth;
    if (base.height - frame.grabY + base.y + view.mouseY < ScreenHeight + 1)
        base.height = base.height - frame.grabY + view.mouseY;
    else
        base.height = ScreenHeight - base.y;
    if (base.height < frame.minHeight)
    {
        base.height = frame.minHeight;
        return;
    }
    if (frame.maxHeight > 0 && frame.maxHeight < base.height)
    {
        base.height = frame.maxHeight;
        return;
    }
    frame.grabY = view.mouseY;
}

// El cambio de tamaño desde arriba a la izquierda (+0x84 en 0x13b): cada eje solo si no baja del mínimo ni sale de la
// pantalla por arriba o por la izquierda; el punto agarrado sigue al mouse siempre.
void ResizeFromTheTopLeft(OldWindow& window, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    const std::int32_t right = base.width + frame.grabX;
    const std::int32_t x = base.x - frame.grabX + view.mouseX;
    if (frame.minWidth <= right - view.mouseX && x >= 0)
    {
        base.x = x;
        base.width = right - view.mouseX;
    }
    frame.grabX = view.mouseX;
    const std::int32_t bottom = frame.grabY + base.height;
    const std::int32_t y = base.y + (view.mouseY - frame.grabY);
    if (frame.minHeight <= bottom - view.mouseY && y >= 0)
    {
        base.y = y;
        base.height = bottom - view.mouseY;
    }
    frame.grabY = view.mouseY;
}

// Un botón de la barra del título bajo el mouse (0x492ac0): con el soltado y el mouse sin dueño en la UI, el mensaje
// al administrador; la cruz, solo si la ventana se deja cerrar (la vtable + 0x4c: 0x48f8f0, que da 1, o 0x499840 en la
// de escribir una carta).
Core::Expected<void, std::string> PressTheTitleButton(OldWindowManager& manager, OldWindow& window, std::int32_t button,
                                                      std::int32_t message, const OldWindowGlobals& globals,
                                                      const OldWindowStepView& view)
{
    window.frame.hotButton = button;
    if (globals.mouseOnUi != 0 || !globals.releasedClick)
        return {};
    bool may = true;
    if (message == CloseMessage)
    {
        auto asked = MayCloseTheWindow(manager, window, globals, view);
        if (!asked.HasValue())
            return Core::Unexpected(std::move(asked).Error());
        may = asked.Value();
    }
    if (may)
        PostToTheManager(manager, message, window.base.id, 0);
    globals.releasedClick = false;
    return {};
}

// Agarrar el mouse para arrastrar (4) o para cambiar el tamaño (1) desde una esquina.
void Grab(OldWindow& window, std::int32_t state, std::int32_t corner, const OldWindowGlobals& globals,
          const OldWindowStepView& view)
{
    if (!globals.leftHeld || window.base.state == state || globals.capture != 0)
        return;
    globals.capture = window.base.id;
    window.base.state = state;
    if (state == ResizedWindow)
        window.frame.resizeCorner = corner;
    window.frame.grabX = view.mouseX;
    window.frame.grabY = view.mouseY;
    if (state == ResizedWindow)
        window.frame.stretched = 0;
}

// 0x492ac0, la vtable + 0x20 de las tres: los botones de la barra, las esquinas y la barra del arrastre; después, el
// arrastre y el cambio de tamaño con el izquierdo abajo, o soltarlos.
Core::Expected<void, std::string> StepTheFrameMouse(OldWindowManager& manager, OldWindow& window,
                                                    const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    const std::int32_t flags = base.flags;
    frame.hotButton = 0;
    if (MouseIn(view, base.x, base.y, base.width, base.height, TopLeftRect))
    {
        const std::int32_t buttonY = base.y + TitleButtonY;
        const std::int32_t minimizeX = (flags & StretchFlag) != 0 ? MinimizeWithStretchX : MinimizeAloneX;
        Core::Expected<void, std::string> pressed;
        if ((flags & TitleFlag) != 0 &&
            MouseIn(view, base.x - CloseButtonX + base.width, buttonY, TitleButtonSize, TitleButtonSize, TopLeftRect))
            pressed = PressTheTitleButton(manager, window, 3, CloseMessage, globals, view);
        else if ((flags & StretchFlag) != 0 && MouseIn(view, base.x - StretchButtonX + base.width, buttonY,
                                                       TitleButtonSize, TitleButtonSize, TopLeftRect))
            pressed = PressTheTitleButton(manager, window, 2, StretchMessage, globals, view);
        else if ((flags & MinimizeFlag) != 0 &&
                 MouseIn(view, base.x - minimizeX + base.width, buttonY, TitleButtonSize, TitleButtonSize, TopLeftRect))
            pressed = PressTheTitleButton(manager, window, 1, MinimizeMessage, globals, view);
        else if ((flags & ResizeFlag) != 0 && MouseIn(view, base.x, base.y, TopLeftGrip, TopLeftGrip, TopLeftRect))
            Grab(window, ResizedWindow, TopLeftCorner, globals, view);
        else if ((flags & DragFlag) != 0 && (flags & TitleFlag) != 0 &&
                 MouseIn(view, base.x, base.y, base.width, DragBarHeight, TopLeftRect))
            Grab(window, DraggedWindow, 0, globals, view);
        else if ((flags & ResizeFlag) != 0 &&
                 MouseIn(view, base.x - BottomRightGrip + base.width, base.y - BottomRightGrip + base.height,
                         BottomRightGrip, BottomRightGrip, TopLeftRect))
            Grab(window, ResizedWindow, BottomRightCorner, globals, view);
        else if (globals.capture == base.id && base.state != DraggedWindow && base.state != ResizedWindow)
        {
            globals.leftHeld = false;
            globals.leftClick = false;
        }
        globals.mouseOnUi = 1;
        if (!pressed.HasValue())
            return pressed;
    }
    if (base.state == DraggedWindow)
    {
        if (globals.leftHeld)
        {
            if (globals.hovered == 0)
                globals.hovered = base.id;
            globals.mouseOnUi = 1;
            const std::int32_t x = base.x - frame.grabX + view.mouseX;
            if (x < 0)
                base.x = 0;
            else if (base.x - frame.grabX + base.width + view.mouseX < ScreenWidth + 1)
                base.x = x;
            else
                base.x = ScreenWidth - base.width;
            const std::int32_t y = base.y - frame.grabY + view.mouseY;
            if (y < 0)
                base.y = 0;
            else if (base.y - frame.grabY + base.height + view.mouseY < ScreenHeight + 1)
                base.y = y;
            else
                base.y = ScreenHeight - base.height;
            frame.grabX = view.mouseX;
            frame.grabY = view.mouseY;
            return AfterTheMove(manager, window, globals, view);
        }
        base.state = ShownWindow;
        if (globals.capture == base.id)
            globals.capture = 0;
        return AfterTheMove(manager, window, globals, view);
    }
    if (base.state != ResizedWindow)
        return AfterTheMove(manager, window, globals, view);
    if (globals.leftHeld)
    {
        if (frame.resizeCorner != BottomRightCorner && frame.resizeCorner != TopLeftCorner)
            return AfterTheMove(manager, window, globals, view);
        if (globals.hovered == 0)
            globals.hovered = base.id;
        globals.mouseOnUi = 1;
        if (frame.resizeCorner == BottomRightCorner)
            ResizeFromTheBottomRight(window, view);
        else
            ResizeFromTheTopLeft(window, view);
        return AfterTheMove(manager, window, globals, view);
    }
    base.state = ShownWindow;
    if (globals.capture == base.id)
        globals.capture = 0;
    return AfterTheMove(manager, window, globals, view);
}

// 0x45aa80, la vtable + 0x18 de las tres: la cola (la vtable + 0x28 y, si da 0, 0x45abc0), la vtable + 0x1c, el
// mouse y la vtable + 0x20 (0x492ac0).
Core::Expected<void, std::string> StepTheWindow(OldWindowManager& manager, OldWindow& window, std::int32_t arg,
                                                const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        if (window.kind == OldWindowKind::MessageBox)
        {
            if (auto answered = AnswerTheBox(manager, window, view); !answered.HasValue())
                return answered;
        }
        else if (IsAFriendsKind(window.kind))
        {
            if (auto handled = HandleTheFriendsMessage(manager, window, globals, view); !handled.HasValue())
                return handled;
        }
        else if (IsALetterKind(window.kind))
        {
            if (auto handled = HandleTheLetterKindsMessage(manager, window, globals, view); !handled.HasValue())
                return handled;
        }
        PlaceAgainstTheParent(manager, base);
    }
    if (auto before = BeforeTheMouse(manager, window, arg, globals, view); !before.HasValue())
        return before;
    if (arg == 1)
        return {};
    if (!ClaimTheMouse(manager, base, globals, view))
        return {};
    return StepTheFrameMouse(manager, window, globals, view);
}

// 0x490670(id, mostrar): el estado 0 o el 3.
void ShowTheWindow(OldWindowManager& manager, std::int32_t id, bool show)
{
    if (OldWindow* window = FindTheOldWindow(manager, id); window != nullptr)
        window->base.state = show ? ShownWindow : HiddenWindow;
}

// SetFocus([0x5763544]). En el 1 de 0x490b70, antes, si el foco estaba en uno de los EDIT del chat (o en el del
// diálogo de 0x5763504, hito 6) o la de arriba es un chat con un amigo (+0x9c en 1), 0x467360 guarda y apaga el modo
// del IME, que MuRet no tiene; 0x4906a0 no lo llama.
void FocusTheGame(const OldWindowGlobals& globals)
{
    globals.keyboardFocus = ChatFocus::Game;
}

// El final del 2 y del 4 de 0x490b70: el foco a la nueva del final de la lista, o al juego.
void FocusTheLast(OldWindowManager& manager, const OldWindowGlobals& globals)
{
    if (manager.order.empty() || manager.order.back() == 0)
    {
        manager.focus = 0;
        FocusTheGame(globals);
        return;
    }
    const OldWindow* last = FindTheOldWindow(manager, manager.order.back());
    if (last == nullptr)
        return;
    if (IsShowing(last->base.state))
    {
        PostToTheWindow(manager, manager.order.back(), RaisedMessage, 0, 0);
        return;
    }
    manager.focus = 0;
    FocusTheGame(globals);
}

// 0x45aa50: el mensaje y el paso con 1.
Core::Expected<void, std::string> PostAndStep(OldWindowManager& manager, OldWindow& window, std::int32_t type,
                                              const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    window.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    return StepTheWindow(manager, window, 1, globals, view);
}

// 0x490b70: los mensajes del administrador.
Core::Expected<void, std::string> HandleTheMessage(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                   const OldWindowStepView& view)
{
    const std::int32_t id = manager.current.a;
    switch (manager.current.type)
    {
    case RaiseMessage:
    {
        if (id == 0 || FindTheOldWindow(manager, id) == nullptr)
            return {};
        // 0x490bad..0x490c5a: sin EDIT propio (+0x9c), el foco del teclado a la ventana del juego.
        if (FindTheOldWindow(manager, id)->frame.editField == 0)
            FocusTheGame(globals);
        if (FindTheOldWindow(manager, id)->base.state == HiddenWindow)
            ShowTheWindow(manager, id, true);
        // 0x490c7d..0x490cd8: la sube al final, salvo que ya esté ahí con el foco en otro lado.
        if (LastOldWindow(manager) != id || globals.keyboardFocus == ChatFocus::Game)
        {
            manager.order.remove(id);
            manager.order.push_back(id);
            PostToTheWindow(manager, id, RaisedMessage, 0, 0);
        }
        manager.focus = id;
        // 0x4a60b0(id): deja de esperar en +0x118 de la lista de ventanas; 0x4a5e10.
        std::deque<std::int32_t>& waiting = manager.windowList.list.waiting;
        waiting.erase(std::remove(waiting.begin(), waiting.end(), id), waiting.end());
        HideTheWindowList(manager.windowList);
        return {};
    }
    case MinimizeMessage:
        if (id == 0 || (manager.modal != 0 && manager.friends != 0 && id == manager.friends))
            return {};
        globals.sound = ButtonSound;
        ShowTheWindow(manager, id, false);
        manager.order.remove(id);
        manager.order.push_front(id);
        FocusTheLast(manager, globals);
        return {};
    case StretchMessage:
        if (id == 0)
            return {};
        if (OldWindow* window = FindTheOldWindow(manager, id); window != nullptr)
        {
            if (auto stretched = StretchTheWindow(manager, *window, globals, view); !stretched.HasValue())
                return stretched;
            globals.sound = ButtonSound;
        }
        return {};
    case CloseMessage:
        if (id == 0)
            return {};
        globals.sound = ButtonSound;
        if (auto destroyed = DestroyTheOldWindow(manager, id); !destroyed.HasValue())
            return destroyed;
        FocusTheLast(manager, globals);
        return {};
    case SendBackMessage:
        if (id == 0 || FindTheOldWindow(manager, id) == nullptr || manager.order.empty() || manager.order.front() == id)
            return {};
        manager.order.remove(id);
        manager.order.push_front(id);
        return {};
    default:
        return {};
    }
}
} // namespace Steps

using Steps::ClaimTheMouse;
using Steps::FocusTheGame;
using Steps::HandleTheMessage;
using Steps::IsShowing;
using Steps::MakeTheFrame;
using Steps::MakeTheMessageBox;
using Steps::PostAndSettle;
using Steps::SettleTheWindow;
using Steps::StepTheWindow;

OldWindow OldWindowManager::MakeTheWindowList()
{
    OldWindow list;
    list.kind = OldWindowKind::WindowList;
    list.base.id = WindowListId;
    list.base.flags = 0;
    list.base.x = WindowListX;
    list.base.y = WindowListY;
    list.base.width = WindowListWidth;
    list.base.height = 0;
    list.base.state = HiddenWindow;
    list.list = OldWindowListPart{};
    return list;
}

OldWindow* FindTheOldWindow(OldWindowManager& manager, std::int32_t id)
{
    if (const auto found = manager.windows.find(id); found != manager.windows.end())
        return &found->second;
    const auto tab = manager.tabs.find(id);
    if (tab == manager.tabs.end())
        return nullptr;
    const auto owner = manager.windows.find(tab->second);
    if (owner == manager.windows.end())
        return nullptr;
    for (OldWindow& candidate : owner->second.tabs)
    {
        if (candidate.base.id == id)
            return &candidate;
    }
    return nullptr;
}

std::string OldWindowTexts::At(std::size_t index) const
{
    return lookup ? lookup(index) : std::string();
}

bool HasTheOldWindow(const OldWindowManager& manager, std::int32_t id)
{
    return manager.windows.contains(id);
}

std::int32_t TopOldWindow(const OldWindowManager& manager)
{
    if (manager.order.empty())
        return 0;
    const std::int32_t last = manager.order.back();
    if (last != manager.friends)
        return last;
    if (manager.order.size() == 1)
        return 0;
    return *std::next(manager.order.rbegin());
}

std::int32_t LastOldWindow(const OldWindowManager& manager)
{
    return manager.order.empty() ? 0 : manager.order.back();
}

void PostToTheManager(OldWindowManager& manager, std::int32_t type, std::int32_t a, std::int32_t b)
{
    manager.queue.push_back(OldMessage{.type = type, .a = a, .b = b, .text = {}});
}

void PostToTheWindow(OldWindowManager& manager, std::int32_t id, std::int32_t type, std::int32_t a, std::int32_t b)
{
    if (OldWindow* window = FindTheOldWindow(manager, id); window != nullptr)
        window->base.queue.push_back(OldMessage{.type = type, .a = a, .b = b, .text = {}});
}

Core::Expected<std::int32_t, std::string>
CreateTheOldWindow(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts,
                   const OldWindowMeasure& measure, const OldWindowGlobals* globals, const OldWindowStepView* view)
{
    // 0x48ee93: con 0x6c7b8c en 0 no arma nada; está en 1 en la .data y nadie lo escribe.
    OldWindow window;
    switch (request.kind)
    {
    case FrameCase:
        window = MakeTheFrame(manager);
        break;
    case FriendsCase:
    {
        // Una sola: con +0x48 da 0.
        if (manager.friends != 0)
            return 0;
        if (globals == nullptr || view == nullptr)
            return Core::Unexpected(std::string("0x48ee60(3): la ventana de amigos sin el paso de las ventanas"));
        auto made = Steps::MakeTheFriendsWindow(manager, request, texts, measure);
        if (!made.HasValue())
            return Core::Unexpected(std::move(made).Error());
        window = std::move(made).Value();
        manager.friends = window.base.id;
        // +0xb0: con una carta nueva (+0x144 de la lista de ventanas), la pestaña de las cartas; si no, la de antes.
        window.friends.currentTab = manager.windowList.list.newLetter == 1 ? 1 : manager.lastTab;
        // 0x4a6420(0).
        manager.windowList.list.newLetter = 0;
        if (manager.listReceived == 0)
        {
            if (!view->send)
                return Core::Unexpected(std::string("0x48ee60(3): el pedido de la lista de amigos (C1 03 C0) sin el "
                                                    "cable"));
            view->send(FriendSend{.kind = FriendSendKind::ListRequest, .value = 0, .name = {}});
        }
        break;
    }
    case AddFriendCase:
    {
        if (manager.modal != 0)
            return 0;
        if (globals == nullptr || view == nullptr)
            return Core::Unexpected(
                std::string("0x48ee60(4): la caja de agregar un amigo sin el paso de las ventanas"));
        window = Steps::MakeTheAddFriendBox(manager, request, texts);
        Steps::AddTheWindowRow(manager, window.base.id, request.data);
        manager.modal = window.base.id;
        break;
    }
    case ModalQuestionCase:
    case ModalNoticeCase:
    case QuestionCase:
    case NoticeCase:
    {
        const bool modal = request.kind == ModalQuestionCase || request.kind == ModalNoticeCase;
        if (modal && manager.modal != 0)
            return 0;
        const bool oneButton = request.kind == ModalNoticeCase || request.kind == NoticeCase;
        window = MakeTheMessageBox(manager, oneButton ? 1 : 0);
        // 0x49e6e0(id, texto) sobre +0x4d0 de la ventana de amigos: la anota en su lista.
        Steps::AddTheWindowRow(manager, window.base.id, request.data);
        if (modal)
            manager.modal = window.base.id;
        else if (std::ranges::find(manager.boxes, window.base.id) == manager.boxes.end())
            manager.boxes.push_back(window.base.id);
        break;
    }
    case ChatCase:
    case CalledChatCase:
    case LetterReadCase:
    case LetterWriteCase:
    {
        // 0x48eee4, 0x48f41f y 0x48f4ee (la de escribir, sin una caja modal). Las cuatro se anotan en la pestaña de
        // las ventanas de la de amigos (0x48f5cd).
        if (request.kind == LetterWriteCase && manager.modal != 0)
            return 0;
        auto made = Steps::MakeTheLetterKind(manager, request, texts, measure, view);
        if (!made.HasValue())
            return Core::Unexpected(std::move(made).Error());
        window = std::move(made).Value();
        Steps::AddTheWindowRow(manager, window.base.id, request.data);
        break;
    }
    default:
        // 0x48ee9b: más de 10 (sin signo), ninguna.
        return 0;
    }
    std::int32_t x = request.x == -1 ? 0 : request.x;
    std::int32_t y = request.y == -1 ? DefaultY : request.y;
    // La vtable + 0x2c: 0x4920a0 en el marco y 0x4a38f0 en la caja (0x4a2280 y 0x4a35a0, en Steps::MakeThe...).
    OldWindowBase& base = window.base;
    if (window.kind == OldWindowKind::Frame)
    {
        window.frame.title = std::string(request.data);
        base.parent = request.aux;
        base.x = FramePlace;
        base.y = FramePlace;
        base.width = FrameWidth;
        base.height = FrameHeight;
    }
    else if (window.kind == OldWindowKind::MessageBox)
    {
        OldMessageBox& box = window.box;
        window.frame.title = box.oneButton == 0 ? texts.question : texts.notice;
        box.owner = request.aux;
        base.parent = 0;
        auto wrapped =
            WrapByWidth(request.data, measure.measure, measure.dcFont, measure.screen, BoxWrapWidth, BoxLines, 0);
        if (!wrapped.HasValue())
            return Core::Unexpected(std::move(wrapped).Error());
        for (std::size_t line = 0; line < box.lines.size(); ++line)
            box.lines[line] = line < wrapped.Value().lines.size() ? wrapped.Value().lines[line] : std::string();
        base.x = BoxPlace;
        base.y = BoxPlace;
        base.width = BoxWidth;
        base.height = BoxHeight;
        window.frame.minWidth = BoxWidth;
        window.frame.minHeight = BoxHeight;
        window.frame.maxWidth = 0;
        window.frame.maxHeight = 0;
        base.flags = BoxFlags;
        const auto setUp =
            [&base](OldButton& button, std::int32_t command, const std::string& label, std::int32_t placeX)
        {
            button.command = command;
            button.label = label;
            button.base.parent = base.id;
            button.base.placeMode = 0;
            button.base.placeX = placeX;
            button.base.placeY = BoxButtonsY;
            button.base.width = BoxButtonWidth;
            button.base.height = BoxButtonHeight;
        };
        if (box.oneButton == 0)
        {
            setUp(box.buttons[0], YesCommand, texts.yes, YesButtonX);
            setUp(box.buttons[1], NoCommand, texts.no, NoButtonX);
        }
        else
            setUp(box.buttons[0], YesCommand, texts.notice, OnlyButtonX);
    }
    // 0x48f606..0x48f6b8: la cascada, por el mapa en el orden de los ids. Al correrse vuelve al primero y el ++ del
    // final de la vuelta lo saltea (Q-C571).
    if ((request.flags & KeepPlaceFlag) == 0)
    {
        for (auto it = manager.windows.begin(); it != manager.windows.end(); ++it)
        {
            const OldWindowBase& other = it->second.base;
            if (other.x != x || other.y != y)
                continue;
            if (base.width + x + CascadeStep <= ScreenWidth)
                x += CascadeStep;
            if (base.height + y + CascadeStep <= ScreenHeight)
                y += CascadeStep;
            if (base.width + x + CascadeStep > ScreenWidth && base.height + y + CascadeStep > ScreenHeight)
            {
                const std::int32_t rest = y % CascadeWrap;
                if (rest == CascadeGiveUp)
                    return 0;
                y = rest + 1;
                x = y;
            }
            it = manager.windows.begin();
        }
    }
    base.x = x;
    base.y = y;
    const std::int32_t id = base.id;
    OldWindow& placed = manager.windows.insert_or_assign(id, std::move(window)).first->second;
    manager.order.push_back(id);
    if (Steps::IsAFriendsKind(placed.kind))
    {
        if (auto settled = SettleTheWindow(manager, placed, *globals, *view); !settled.HasValue())
            return Core::Unexpected(std::move(settled).Error());
    }
    else if (Steps::IsALetterKind(placed.kind))
    {
        // 0x48f810..0x48f847: los chats a +0xb4 de la lista de ventanas (0x4a4990), la vtable + 0x30 y, en el 1, el
        // foco al EDIT.
        if (request.kind == ChatCase || request.kind == CalledChatCase)
            AddTheChatToTheList(manager, id);
        if (globals == nullptr || view == nullptr)
            return Core::Unexpected(std::string("0x48ee60: el chat o una carta sin el paso de las ventanas"));
        if (auto settled = SettleTheWindow(manager, placed, *globals, *view); !settled.HasValue())
            return Core::Unexpected(std::move(settled).Error());
        if (request.kind == ChatCase || request.kind == LetterWriteCase)
            Steps::FocusTheChatEdit(manager, placed, *globals);
    }
    else if (placed.kind == OldWindowKind::MessageBox)
        SettleTheButtons(manager, placed);
    return id;
}

Core::Expected<void, std::string> StepTheWindowList(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                    const OldWindowStepView& view)
{
    return StepTheWindow(manager, manager.windowList, 0, globals, view);
}

Core::Expected<void, std::string> StepTheOldWindows(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                    const OldWindowStepView& view)
{
    if (manager.modal != 0 && FindTheOldWindow(manager, manager.modal) == nullptr)
        manager.modal = 0;
    // 0x4904bc: el flanco de Esc se lee siempre; cierra la del final de la lista si está a la vista y se deja cerrar
    // (la vtable + 0x4c).
    if (view.keyEdge && view.keyEdge(EscapeKey) && !manager.order.empty() && manager.order.back() != 0)
    {
        OldWindow* last = FindTheOldWindow(manager, manager.order.back());
        if (last != nullptr && IsShowing(last->base.state))
        {
            auto may = Steps::MayCloseTheWindow(manager, *last, globals, view);
            if (!may.HasValue())
                return Core::Unexpected(std::move(may).Error());
            if (may.Value())
                PostToTheManager(manager, CloseMessage, LastOldWindow(manager), 0);
        }
    }
    // 0x490538..0x49055c: F5 con el foco en la ventana del juego, 0x4a5be0(1) sobre la lista de ventanas: sin chats con
    // un amigo (+0xdc), nada; con ellos, la muestra (P-C215).
    if (globals.keyboardFocus == ChatFocus::Game && view.keyEdge && view.keyEdge(F5Key) &&
        !manager.windowList.list.chats.empty())
        return Core::Unexpected(std::string("0x4a5be0(1): F5 con los chats con un amigo, la lista de ventanas a la "
                                            "vista (0x4a4eb0, 0x4a4e20) no está portada (P-C215)"));
    // 0x490561..0x4905a2: F6 esconde las ventanas, o las vuelve a mostrar si escondió alguna.
    if (view.keyEdge && view.keyEdge(F6Key) && (manager.hiddenByF6 == 0 || !manager.hidden.empty()))
    {
        manager.hiddenByF6 = (manager.hiddenByF6 + 1) % 2;
        if (auto hidden = HideTheOldWindows(manager, manager.hiddenByF6, 1, globals, &view); !hidden.HasValue())
            return hidden;
    }
    // 0x4905a2..0x4905bf: la de amigos oculta se suelta (0x4914e0); al volver a mostrar, 0x4906a0 la abre (Q-C448).
    if (manager.friends > 0)
    {
        const OldWindow* friends = FindTheOldWindow(manager, manager.friends);
        if (friends != nullptr && friends->base.state == HiddenWindow)
        {
            if (auto dropped = DropTheFriendsWindow(manager); !dropped.HasValue())
                return dropped;
        }
    }
    // 0x4905c4: de arriba hacia abajo, cada una a la vista corre su paso con 0 (solo las del primer mapa).
    for (auto it = manager.order.rbegin(); it != manager.order.rend(); ++it)
    {
        OldWindow* window = FindTheOldWindow(manager, *it);
        if (window == nullptr || !IsShowing(window->base.state))
            continue;
        if (auto stepped = StepTheWindow(manager, *window, 0, globals, view); !stepped.HasValue())
            return stepped;
    }
    while (!manager.queue.empty())
    {
        manager.current = manager.queue.front();
        manager.queue.pop_front();
        if (auto handled = HandleTheMessage(manager, globals, view); !handled.HasValue())
            return handled;
    }
    manager.drawn = 0;
    return {};
}

Core::Expected<void, std::string> DestroyTheOldWindow(OldWindowManager& manager, std::int32_t id)
{
    if (manager.friends == id)
    {
        if (manager.modal != 0)
            return {};
        // 0x49022a..0x490270: guarda el lugar de la ventana de amigos para la próxima.
        if (const OldWindow* friends = FindTheOldWindow(manager, manager.friends); friends != nullptr)
            Steps::SaveTheFriendsPlace(manager, *friends);
        manager.friends = 0;
    }
    if (manager.letter == id)
        manager.letter = 0;
    const auto found = manager.windows.find(id);
    if (found == manager.windows.end())
        return {};
    // 0x491840. El destructor del chat sale de la sala (0x493a70) y el de leer suelta su carta (0x4916b0).
    manager.boxes.remove(id);
    if (auto forgotten = Steps::ForgetTheLetterKind(manager, found->second); !forgotten.HasValue())
        return forgotten;
    manager.windows.erase(found);
    manager.order.remove(id);
    if (manager.order.empty())
        manager.focus = 0;
    // 0x49e710(id) sobre la pestaña de las ventanas de la de amigos.
    Steps::ForgetTheWindowRow(manager, id);
    // 0x4a4b30(id): sale de los chats de +0xb4 de la lista de ventanas.
    std::deque<std::int32_t>& chats = manager.windowList.list.chats;
    chats.erase(std::remove(chats.begin(), chats.end(), id), chats.end());
    if (manager.modal == id)
        manager.modal = 0;
    return {};
}

Core::Expected<void, std::string> HideTheOldWindows(OldWindowManager& manager, std::int32_t hide,
                                                    std::int32_t withFriends, const OldWindowGlobals& globals,
                                                    const OldWindowStepView* view)
{
    std::int32_t shown = 0;
    for (auto& [id, window] : manager.windows)
    {
        if ((withFriends != 1 && id == manager.friends) || id == manager.modal)
            continue;
        if (hide == 1)
        {
            if (window.base.state != ShownWindow)
                continue;
            if (withFriends == 1)
                manager.hidden.push_back(id);
            window.base.state = HiddenWindow;
            continue;
        }
        for (const std::int32_t hiddenId : manager.hidden)
        {
            if (hiddenId != id)
                continue;
            ++shown;
            window.base.state = ShownWindow;
        }
    }
    if (hide != 0)
    {
        manager.focus = 0;
        FocusTheGame(globals);
        return {};
    }
    // 0x490608: quedó en la lista una que ya no está (la de amigos que soltó 0x4914e0, Q-C448): 0x491080 abre otra.
    const auto count = static_cast<std::int32_t>(manager.hidden.size());
    if (count != shown && count - shown >= 0)
    {
        if (view == nullptr || !view->openFriends)
            return Core::Unexpected(std::string("0x491080: abrir la ventana de amigos (0x186, 0x106) sin el juego"));
        if (auto opened = view->openFriends(); !opened.HasValue())
            return opened;
    }
    if (shown > 0 && TopOldWindow(manager) != 0)
        PostToTheManager(manager, RaiseMessage, TopOldWindow(manager), 0);
    manager.hidden.clear();
    return {};
}

Core::Expected<void, std::string> DropTheFriendsWindow(OldWindowManager& manager)
{
    if (manager.friends == 0)
        return {};
    return DestroyTheOldWindow(manager, manager.friends);
}

void ForgetTheHiddenWindows(OldWindowManager& manager)
{
    manager.hiddenByF6 = 0;
    manager.hidden.clear();
}

void ResetTheOldWindows(OldWindowManager& manager)
{
    // 0x48ea5f..0x48eb43: +0x48 en 0, los dos mapas (soltando cada ventana con su destructor: el de leer saca su carta
    // de +0x94; el del chat no tiene sala en MuRet, P-C211, y no corta), la lista del orden, 0x4a4480 (los chats de
    // +0xb4 y +0x118 de la lista de ventanas), lo que escondió F6 y las cajas.
    manager.friends = 0;
    for (const auto& [id, window] : manager.windows)
        static_cast<void>(Steps::ForgetTheLetterKind(manager, window));
    manager.windows.clear();
    manager.tabs.clear();
    manager.order.clear();
    manager.windowList.list.chats.clear();
    manager.windowList.list.waiting.clear();
    ForgetTheHiddenWindows(manager);
    manager.boxes.clear();
    manager.modal = 0;
    manager.lastTab = 0;
    manager.listReceived = 1;
    manager.titleText = 990;
    manager.letter = 0;
    manager.offlineSent = 0;
    // Los destructores de las ventanas sueltan sus EDIT, y con ellos el foco.
    manager.focusedEdit = 0;
    // 0x48eb7d: +0x48 ya está en 0, así que 0x4a23b0 sobre la de amigos no corre.
}

void SettleTheButtons(OldWindowManager& manager, OldWindow& box)
{
    for (OldButton& button : box.box.buttons)
        PostAndSettle(manager, button, PlaceMessage);
}

void HideTheWindowList(OldWindow& list)
{
    if (list.base.state != ShownWindow)
        return;
    list.base.state = HiddenWindow;
    list.base.height = 0;
    list.base.y = list.list.bottom;
    list.list.blinkStep = WindowListBlinkDown;
    list.list.armed = 0;
}
} // namespace Client::UI
