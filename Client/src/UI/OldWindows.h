#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/ChatInput.h"
#include "UI/OldWindowBase.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// Las ventanas viejas (CL8 hito 7; 06 § HUD y ventanas, "Las ventanas viejas"): el administrador de 0x5763514
// (0x48e470, 0xf4 bytes), las ventanas con marco de 0x491ca0, la caja de mensaje de la vtable 0x6b9cb4, sus botones
// (0x45ad80) y la lista de ventanas de [0x5763524]. El original las arma sobre la ventana base 0x45a210 (vtable
// 0x6b8d6c) y despacha por vtable; MuRet las guarda como datos (OldWindow, con su clase en `kind`) y cada función de
// la vtable es una función de acá con la dirección del método de cada clase. Los std::map, std::list y std::deque de
// la STL de MSVC6 del original son los de la STL de MuRet, con el mismo orden.
//
// En la CL8 nadie arma una ventana vieja: los que llaman a la fábrica (0x48ee60) son de la CL9.4 (la ventana de
// amigos, las cartas y los paquetes del chat). El administrador queda vacío y la lista de ventanas, oculta; los tests
// abren una caja de mensaje a mano.
namespace Client::UI
{
// +0x4c del marco (0x492500 y 0x492ac0): la barra del título con la cruz, el borde, la esquina para el tamaño, el
// arrastre por la barra y los dos botones de la barra (el de minimizar, que manda el 2, y el de estirar, el 3).
// 0x491ca0 pone todas (0x3f); la caja de mensaje, 0xb (0x4a38f0); la lista de ventanas, ninguna (0x4a48b0).
inline constexpr std::int32_t TitleFlag = 0x1;
inline constexpr std::int32_t BorderFlag = 0x2;
inline constexpr std::int32_t ResizeFlag = 0x4;
inline constexpr std::int32_t DragFlag = 0x8;
inline constexpr std::int32_t MinimizeFlag = 0x10;
inline constexpr std::int32_t StretchFlag = 0x20;

// 0x48ee60: los casos de la fábrica. El 0 es un marco solo; el 5 y el 8 son cajas modales (0x5762ebc), el 9 y el 10
// no (van a la lista de +0xb8); el 5 y el 9 tienen dos botones, el 8 y el 10 uno. Los demás (1 y 2, el chat con un
// amigo; 3, la ventana de amigos; 4, agregar un amigo; 6 y 7, las cartas) son de la CL9.4.
inline constexpr std::int32_t FrameCase = 0;
inline constexpr std::int32_t ModalQuestionCase = 5;
inline constexpr std::int32_t ModalNoticeCase = 8;
inline constexpr std::int32_t QuestionCase = 9;
inline constexpr std::int32_t NoticeCase = 10;
// El bit 0 del último argumento de 0x48ee60: no correr la cascada sobre las que están en el mismo lugar.
inline constexpr std::int32_t KeepPlaceFlag = 0x1;

// +0x40 de [0x5763524]: el id de la lista de ventanas. *Inferido*, como el del registro del chat (el 9): el que sigue
// a él en WinMain (0x4bb786). Solo importan el orden y que sean distintos (P-C97).
inline constexpr std::int32_t WindowListId = 10;

// Lo que agrega el marco de 0x491ca0 (vtable 0x6b9f9c).
struct OldFrame
{
    // +0x7c y +0x80: el mouse cuando empezó el arrastre o el cambio de tamaño.
    std::int32_t grabX = 0;
    std::int32_t grabY = 0;
    // +0x84: la esquina del cambio de tamaño: 0x87 abajo a la derecha, 0x13b arriba a la izquierda.
    std::int32_t resizeCorner = 0;
    // +0x88..+0x94: el tamaño mínimo y el máximo (0, sin máximo).
    std::int32_t minWidth = 100;
    std::int32_t minHeight = 100;
    std::int32_t maxWidth = 0;
    std::int32_t maxHeight = 0;
    // +0x98: el título (strdup de 0x4920e0), o ninguno.
    std::optional<std::string> title;
    // +0x9c: el chat con un amigo, con su EDIT (CL9.4); 0 en las demás.
    std::int32_t editField = 0;
    // +0xa0: el botón de la barra bajo el mouse (3, la cruz; 2, estirar; 1, minimizar), del último paso.
    std::int32_t hotButton = 0;
    // +0xa4, +0xa8 y +0xac: estirada de arriba hasta la barra (0x4930f0: y en 0 y 0x1b0 de alto), con la y y el alto
    // de antes.
    std::int32_t stretched = 0;
    std::int32_t savedY = 0;
    std::int32_t savedHeight = 0;
};

// Lo que agrega la caja de mensaje (vtable 0x6b9cb4, 0x3d4 bytes).
struct OldMessageBox
{
    // +0xb0: la ventana a la que avisa la respuesta (el mensaje 12), 0 ninguna; −1, la respuesta a un pedido de
    // amistad, que va por la red (CL9.4).
    std::int32_t owner = 0;
    // +0xb4: con 1, un solo botón (los casos 8 y 10).
    std::int32_t oneButton = 0;
    // +0xb8 y +0x1b8: el texto cortado en 2 renglones de 0x7d (0x459c70).
    std::array<std::string, 2> lines;
    // +0x2c4 y +0x34c: los dos botones. Con un solo botón, el segundo existe igual: sin padre, en (0, 0).
    std::array<OldButton, 2> buttons;
};

// Lo que agrega la lista de ventanas de [0x5763524] (vtable 0x6ba074). Sus dos colas de ids (+0xb4, los chats con un
// amigo que agrega 0x4a4990, y +0x118, el orden de F5) solo las llena la CL9.4: en la CL8 están vacías y MuRet no
// las tiene.
struct OldWindowListPart
{
    // +0x100: el alto de un renglón en 640 × 480, que el dibujo mide la primera vez (GetTextExtentPointA de "0").
    float rowHeight = 0.0f;
    // +0x104 y +0x108: el borde de abajo y el alto de un renglón con el margen (0x4a48b0).
    std::int32_t bottom = 0x1b3;
    std::int32_t rowStep = 0x12;
    // +0x10c y +0x110: el parpadeo del elegido y lo que suma por dibujo (0,25 con F5, −0,25 al ocultarla).
    float blink = 0.0f;
    float blinkStep = 0.0f;
    // +0x150: F5 la mostró con 1 (Enter elige; 0x4a4e20).
    std::int32_t armed = 0;
};

enum class OldWindowKind : std::uint8_t
{
    // 0x491ca0, vtable 0x6b9f9c.
    Frame,
    // vtable 0x6b9cb4.
    MessageBox,
    // vtable 0x6ba074.
    WindowList,
};

struct OldWindow
{
    OldWindowKind kind = OldWindowKind::Frame;
    OldWindowBase base;
    OldFrame frame;
    OldMessageBox box;
    OldWindowListPart list;
};

// El administrador, *(0x5763514), y los globales de las ventanas viejas que solo usa él.
struct OldWindowManager
{
    // +0x4c: las ventanas por id. El segundo mapa (+0x5c) solo lo llena la CL9.4 (0x4908d0, desde 0x4a2280): MuRet
    // no lo tiene.
    std::map<std::int32_t, OldWindow> windows;
    // +0x80: los ids de abajo hacia arriba; la última es la de arriba.
    std::list<std::int32_t> order;
    // +0x08..+0x3c: la cola de mensajes del administrador y el que atiende.
    std::deque<OldMessage> queue;
    OldMessage current;
    // +0x40: 1 después del dibujo (0x490390), 0 después del paso (0x490490). Nadie lo lee en la CL8.
    std::int32_t drawn = 0;
    // +0x44: la que tiene el foco.
    std::int32_t focus = 0;
    // +0x48: la ventana de amigos (CL9.4): 0 en la CL8.
    std::int32_t friends = 0;
    // +0xa8: F6 escondió las ventanas.
    std::int32_t hiddenByF6 = 0;
    // +0xac: los ids que escondió F6.
    std::list<std::int32_t> hidden;
    // +0xb8: las cajas de mensaje que no son modales (0x4917e0, 0x491840).
    std::list<std::int32_t> boxes;
    // +0xf0: la ventana de una carta (CL9.4); 0x490200 la vuelve a 0.
    std::int32_t letter = 0;
    // 0x5762ebc: la caja modal. Con una, la fábrica no arma otra.
    std::int32_t modal = 0;
    // 0x5762eb0: el último id que dio 0x459ef0.
    std::int32_t lastId = WindowListId;
    // [0x5763524]: la lista de ventanas, que WinMain arma aparte (no está en el mapa).
    OldWindow windowList = MakeTheWindowList();

    // 0x491ca0 en 0x4bb786, con la vtable 0x6ba074 y 0x4a48b0: arriba de la barra, en (0x246, 0x1b3), 0x34 de ancho y
    // 0 de alto, sin banderas y oculta.
    [[nodiscard]] static OldWindow MakeTheWindowList();
};

// Los globales del paso: 0x7d36874 (el izquierdo abajo), 0x7d366d4 (el flanco del clic), 0x7d366ec (el flanco del
// soltado, que prende WM_LBUTTONUP), 0x775857c, 0x5762eb8 (la de abajo del mouse), 0x5762eb4 (la que agarró el
// mouse), 0x5762ec4 (el botón apretado), el foco del teclado (GetFocus y SetFocus con [0x5763544]) y el último
// sonido (0x41ed10).
struct OldWindowGlobals
{
    bool& leftHeld;
    bool& leftClick;
    bool& releasedClick;
    std::uint8_t& mouseOnUi;
    std::int32_t& hovered;
    std::int32_t& capture;
    std::int32_t& pressedButton;
    ChatFocus& keyboardFocus;
    std::optional<std::int32_t>& sound;
};

// Lo que el paso lee del juego.
struct OldWindowStepView
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x553520(vk): el flanco de una tecla, con el latch que comparten todos los lectores.
    std::function<bool(std::uint8_t)> keyEdge;
};

// Los textos de Text.bmd que usa la caja de mensaje: el título y el botón de un solo botón (228), el título de dos
// (991) y los dos botones (1037 y 1038).
inline constexpr std::size_t NoticeText = 228;
inline constexpr std::size_t QuestionText = 991;
inline constexpr std::size_t YesText = 1037;
inline constexpr std::size_t NoText = 1038;
struct OldWindowTexts
{
    std::string notice;
    std::string question;
    std::string yes;
    std::string no;
};

// Lo que mide 0x459c70 al cortar el texto de una caja: GetTextExtentPointA sobre el DC con su fuente.
struct OldWindowMeasure
{
    const Render::Text::ITextMeasure& measure;
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
    App::PixelSize screen{};
};

// Lo que pide 0x48ee60(caso, x, y, dato, aux, banderas): x en −1 es 0 e y en −1, 0x14c. El dato es el título del marco
// (0x4920a0) o el texto de la caja (0x4a38f0); aux, el padre del marco o el dueño de la caja.
struct OldWindowRequest
{
    std::int32_t kind = FrameCase;
    std::int32_t x = -1;
    std::int32_t y = -1;
    std::string_view data;
    std::int32_t aux = 0;
    std::int32_t flags = 0;
};

// 0x490a00: la ventana del id, o ninguna.
[[nodiscard]] OldWindow* FindTheOldWindow(OldWindowManager& manager, std::int32_t id);
// 0x490aa0.
[[nodiscard]] bool HasTheOldWindow(const OldWindowManager& manager, std::int32_t id);
// 0x490880: la de arriba, salteando la de amigos; 0 sin ninguna o con la de amigos sola.
[[nodiscard]] std::int32_t TopOldWindow(const OldWindowManager& manager);
// El id del final de la lista (+0x84 → anterior → +8), o 0 con la lista vacía: lo que miran 0x490490, 0x492500 y
// 0x4a43c0.
[[nodiscard]] std::int32_t LastOldWindow(const OldWindowManager& manager);

// 0x459f70 sobre el administrador y 0x490b40 (a la ventana del id, si está).
void PostToTheManager(OldWindowManager& manager, std::int32_t type, std::int32_t a, std::int32_t b);
void PostToTheWindow(OldWindowManager& manager, std::int32_t id, std::int32_t type, std::int32_t a, std::int32_t b);

// 0x48ee60: arma la ventana, la ubica (con la cascada salvo KeepPlaceFlag), la agrega al mapa y arriba de la lista y
// corre su vtable + 0x30. Devuelve su id, o 0 si no la armó (una modal con otra modal, o la cascada sin lugar).
[[nodiscard]] Core::Expected<std::int32_t, std::string> CreateTheOldWindow(OldWindowManager& manager,
                                                                           const OldWindowRequest& request,
                                                                           const OldWindowTexts& texts,
                                                                           const OldWindowMeasure& measure);

// 0x636e3c: el paso de la lista de ventanas, su vtable + 0x18 (0x45aa80) con 0, antes de 0x490490.
[[nodiscard]] Core::Expected<void, std::string>
StepTheWindowList(OldWindowManager& manager, const OldWindowGlobals& globals, const OldWindowStepView& view);

// 0x490490: la modal que ya no está, Esc, F5, F6, la de amigos oculta, el paso de cada ventana de arriba hacia abajo y
// los mensajes del administrador.
[[nodiscard]] Core::Expected<void, std::string>
StepTheOldWindows(OldWindowManager& manager, const OldWindowGlobals& globals, const OldWindowStepView& view);

// 0x490200: saca la ventana del mapa, de la lista y de las cajas, y la suelta.
[[nodiscard]] Core::Expected<void, std::string> DestroyTheOldWindow(OldWindowManager& manager, std::int32_t id);

// 0x4906a0(esconder, con la de amigos): esconde (state 3) o vuelve a mostrar las que escondió F6.
[[nodiscard]] Core::Expected<void, std::string> HideTheOldWindows(OldWindowManager& manager, std::int32_t hide,
                                                                  std::int32_t withFriends,
                                                                  const OldWindowGlobals& globals);

// 0x4914e0: suelta la ventana de amigos, si hay.
[[nodiscard]] Core::Expected<void, std::string> DropTheFriendsWindow(OldWindowManager& manager);

// 0x45aa50(8, 0, 0) sobre los dos botones de una caja: los vuelve a ubicar contra ella (0x4a3a50 y 0x4a3a90).
void SettleTheButtons(OldWindowManager& manager, OldWindow& box);

// 0x4a5e10: oculta la lista de ventanas si está a la vista.
void HideTheWindowList(OldWindow& list);
} // namespace Client::UI
