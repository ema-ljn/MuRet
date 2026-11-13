#pragma once

#include "UI/OldWindowBase.h"
#include "UI/TextField.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

// Los controles de las ventanas viejas que la CL8 dejó sin portar (CL9 hito 9; 06 § HUD y ventanas, "Las ventanas
// viejas"): la plantilla de lista de 0x45adf0..0x4a7280 y el EDIT de 0x4674c0.
//
// La lista es una plantilla de la STL de MSVC6 con un std::deque de elementos: el registro del chat (0x45b620,
// elementos de 0x11c, ya portado en UI/ChatLogWindow), la lista de amigos (0x460970, 0x14), la de las ventanas
// (0x462010, 0x4c), la de las cartas (0x463190, 0x138) y la del chat con un amigo (0x3f0). Las instancias son iguales
// byte a byte salvo el tamaño del elemento (0x47b0f0, 0x47e010, 0x47f7b0 y 0x47c870 son el mismo paso del mouse;
// 0x461830 y 0x4625c0, el mismo cambio de renglones). MuRet guarda los elementos en su dueño y la lista recibe cuántos
// hay; lo que cada instancia hace con un renglón (el clic, el dibujo) es del que la usa.
//
// Las de la CL9.4 (P-C216, *verificado* contra el ASM: las mismas instrucciones que la de amigos, salvo los
// desplazamientos y el tamaño del elemento), por el lugar en la vtable. La de las cartas (0x6b92a0 y 0x6b9340): + 0x28
// 0x47f090 (= 0x47a9d0), + 0x34 0x47efd0 (= 0x47a910), + 0x3c 0x472340 (= 0x4720c0), + 0x40 0x472350 (= 0x4720d0),
// + 0x44 0x47f060 (= 0x47a9a0), + 0x4c 0x47f2a0 (= 0x47abe0), + 0x50 0x47f350 (= 0x47ac90), + 0x5c 0x47f5e0
// (= 0x47af20), + 0x60 0x47f640 (= 0x47af80), + 0x64 0x47f690 (= 0x47afd0), + 0x6c 0x47fea0 (= 0x47b7e0) y + 0x70
// 0x480080 (= 0x47b9c0). La del chat con un amigo, de 0x3f0 (0x6b93d4 y 0x6b9474): 0x47c150, 0x47c090, 0x472200,
// 0x472210, 0x47c120, 0x47c2c0 (+ 0x48, = 0x47ab40), 0x47c360, 0x47c410, 0x472220 (+ 0x54, = 0x4720e0), 0x47c670,
// 0x47c6a0, 0x47c700, 0x47c750, 0x47cf60 y 0x47d140, en los mismos lugares. Lo propio de cada una (*inferido* por la
// vtable y lo que llaman, sin compararlo instrucción por instrucción): las cartas, + 0x2c 0x47eac0 y + 0x94..+ 0x98
// 0x4636b0 y 0x463960 (copiar la deque de las cartas), + 0x40 0x463d50 (los renglones); el chat, + 0x2c
// 0x47bba0 (con 0x47bf00, el += de su iterador) y + 0x90..+ 0x98 0x465270, 0x464ae0 y 0x465450 (agregar un renglón):
// los reemplaza el std::deque de la ventana (OldLetterPart, OldChatPart). Los dibujos (+ 0x14 0x464d90 y 0x47bf90,
// + 0x7c..+ 0x80 0x464e90 y 0x465770, + 0x9c 0x464190 y 0x465710) son de P-C213.
namespace Client::UI
{
// La lista (0x45e2e0 y sus derivadas), sobre la ventana base: el rectángulo cuelga de su borde de abajo (+0x78 en 2,
// 0x47bb80).
struct OldList
{
    OldWindowBase base;
    // +0xbc: toma las flechas del teclado (0x47b0f0) y el 0xe elige el último (0x47a9d0). 1 en las de la CL9.4.
    std::int32_t keyboard = 1;
    // +0xc0: cuántos pasos lleva apretada una flecha del teclado.
    std::int32_t keyRepeat = 0;
    // +0xc4: el elegido, contando desde 1; 0, ninguno.
    std::int32_t selection = 0;
    // +0xc8 y +0xf8: con 1, la cuenta es +0xf8 y no el tamaño del deque (0x4720e0). Las listas de la CL9.4 los dejan en
    // 0 (0x460970, 0x462010).
    std::int32_t fixedCount = 0;
    std::int32_t fixedCountValue = 0;
    // +0xfc.
    std::int32_t maxEntries = 0x96;
    // +0x100: cuántos se saltean antes del primero que se ve.
    std::int32_t scroll = 0;
    // +0x104: los renglones que muestra.
    std::int32_t rows = 6;
    // +0x108..+0x118: la barra (0x47b9c0).
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbWidth = 13.0f;
    float thumbHeight = 0.0f;
    // +0x11c: dónde agarró la barra el mouse, desde su borde de arriba.
    float dragOffset = 0.0f;
    // +0x120 y +0x124: cuántos pasos lleva apretada una flecha de la barra o la pista. Con 16, repite en cada paso.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0x128: con 1, el 9 conserva lo que se ve abajo al cambiar los renglones. 0 en las de la CL9.4.
    std::int32_t keepBottom = 0;
    // +0x12c: 1, la barra de las listas de las ventanas (bordes de 0xc); 0, la del registro del chat.
    std::int32_t barMode = 1;
    // +0x130: el dibujo de los encabezados (0x461890); la pestaña de amigos la pone en 1 (0x49c8b0).
    std::int32_t headers = 0;
    // +0x134 y +0x138 de la lista de amigos: el ancho de "ZZZZZZZZZZ" y el del texto 1022, + 8 (0x460970).
    std::int32_t nameWidth = 0;
    std::int32_t placeWidth = 0;
    // +0x13c de la lista de las cartas (0x463190): el ancho del texto 1030, + 8. En ella, +0x130 es el ancho de la
    // columna de la marca (0x19) y +0x134 y +0x138, los de los textos 1028 y 1029 (CL9 hito 10).
    std::int32_t subjectWidth = 0;
    // +0x144 de la lista de amigos: 0x49ddd0 la pone en 1 antes de copiar el modelo. Nadie la lee en el hito 9
    // (*estático*: no hay otra escritura ni lectura de +0x144 en la vtable 0x6b90cc).
    std::int32_t refreshed = 0;
};

// 0x4720e0, la vtable + 0x54: los que tiene (+0xa8), o +0xf8 con +0xc8 en 1. 0x4722c0, la de la lista de ventanas
// (0x6b916c + 0x54 y 0x6b920c + 0x54), es la misma. 0x4722b0 (+0x104 = renglones), la + 0x40 de la vtable intermedia
// de la lista de ventanas (0x6b920c, que 0x462010 pone en 0x4620f1 y cambia por 0x6b916c en 0x462170 sin llamarla),
// está muerta: la lista armada tiene la de 0x4625c0 (*estático*).
[[nodiscard]] std::int32_t ListCount(const OldList& list, std::size_t size);

// 0x47a910, la vtable + 0x34: con menos que los renglones (sin signo), nada; si no, corre `lines` hacia el principio
// (negativo, hacia el final) y lo acota a [0, cuenta − renglones].
void ScrollTheList(OldList& list, std::size_t size, std::int32_t lines);

// 0x461830 y 0x4625c0, la vtable + 0x40: menos renglones que los que había y que la cuenta los pone y corre uno; si
// no, más que la cuenta vuelve el corrimiento a 0, y los pone.
void SetTheListRows(OldList& list, std::size_t size, std::int32_t rows);

// 0x47a9a0, la vtable + 0x44: 3 renglones por paso, al menos 3.
void GrowTheListRows(OldList& list, std::int32_t steps);

// 0x47aef0, la vtable + 0x58: el elegido, hasta la cuenta.
void SelectInTheList(OldList& list, std::size_t size, std::int32_t selection);

// 0x47af20 y 0x47af80, la vtable + 0x5c y + 0x60: vacía, la elección en 0; con uno elegido, `steps` más (hasta la
// cuenta) o menos (hasta 1).
void SelectTheNext(OldList& list, std::size_t size, std::int32_t steps);
void SelectThePrevious(OldList& list, std::size_t size, std::int32_t steps);

// 0x47afd0, la vtable + 0x64: el lugar del elegido en el deque, o ninguno (el final) sin elegido o con uno más allá
// del final. Sin elementos, además vuelve la elección a 0.
[[nodiscard]] std::optional<std::size_t> SelectedInTheList(OldList& list, std::size_t size);

// 0x47bb00, la vtable + 0x74: el primero que se ve, el del corrimiento (con +0xc8 en 0, desde el principio).
[[nodiscard]] std::size_t FirstShownInTheList(const OldList& list);

// 0x461c70 y 0x462a00, la vtable + 0x9c: la y del renglón `row` (desde 0, el primero que se ve), contada desde el
// borde de arriba: el primero queda abajo y los demás suben de a 13.
[[nodiscard]] std::int32_t ListRowY(const OldList& list, std::size_t size, std::int32_t row);

// 0x47a9d0, la vtable + 0x28: el 9 (con padre) vuelve a contar los renglones con el alto de antes de medir
// (ftol(h · 1/13 − 0,5)), el 0xe va al final (y elige el último con +0xbc), y el 0xf y el 0x10 suben o bajan la
// elección (con +0xbc) y la dejan a la vista. Siempre da 0: después corre 0x45abc0.
void HandleTheListMessage(OldList& list, std::size_t size);

// 0x47b9c0, la vtable + 0x70: los bordes de la pista y el tamaño y el lugar del cursor de la barra, como 0x47a230 del
// registro del chat con la cuenta de la lista.
void PlaceTheListBar(OldList& list, std::size_t size);

// Los globales que lee y escribe el paso del mouse de una lista.
struct OldListGlobals
{
    // 0x7d366d4: el flanco del clic izquierdo.
    bool& leftClick;
    // 0x7d366d8: la rueda en pasos de 120.
    std::int32_t& wheel;
    // 0x5762eb4: la ventana que agarró el mouse.
    std::int32_t& capture;
    // 0x5762ec0: la lista que toma las flechas del teclado.
    std::int32_t& focusedList;
    // 0x775857c.
    std::uint8_t& mouseOnUi;
    // 0x41ed10: el último sonido.
    std::optional<std::int32_t>& sound;
    // +0x40 del administrador (0x5763514): 1 después del dibujo. Las cuentas de las flechas y de la pista de una lista
    // con padre solo suben con él (0x47b2b0, 0x47b427).
    bool drawn = false;
    // 0x5762ebc: la caja modal. Con una, el paso no mira los renglones (0x47b71b).
    std::int32_t modal = 0;
};

// Lo que el paso lee del juego.
struct OldListView
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // GetAsyncKeyState(vk) con el byte alto en 0x80: la tecla abajo, sin flanco.
    std::function<bool(std::uint8_t)> keyDown;
};

// La vtable + 0x88 de cada instancia (0x461f00, 0x462bd0): el renglón `row` con el elemento `at` del deque. Devuelve
// lo que el paso hace con la cuenta: negativo la corre de más, 0 repite el renglón y positivo sigue.
using OldListRowHover = std::function<std::int32_t(std::int32_t row, std::size_t at)>;

// 0x47b0f0 (y sus iguales), la vtable + 0x20: las flechas del teclado (con +0xbc y la lista con el foco), la rueda,
// las flechas y la pista de la barra, agarrar la barra o el borde, el arrastre, y con la caja modal cerrada el mouse
// sobre cada renglón que se ve. Devuelve 1 si el mouse estaba sobre la lista (0x47bb80).
[[nodiscard]] bool StepTheListMouse(OldList& list, std::size_t size, const OldListGlobals& globals,
                                    const OldListView& view, const OldListRowHover& hover);

// El EDIT de las ventanas viejas, 0x4674c0 (vtable 0x6b9550): un EDIT de Windows hijo de la ventana del juego, con su
// subclase 0x467b10. MuRet guarda el texto con el tope de EM_LIMITTEXT (0x467f30) y deja afuera lo que el original
// dibuja con el DC del EDIT (la selección, las flechas y la barra de 0x468d50: P-C86); el cursor, al final del texto
// (0x468320, P-C181).
struct OldEdit
{
    OldWindowBase base;
    // +0x4c: con 1 solo números, con 2 letras y números en mayúscula (0x467b10). La caja de agregar un amigo, 8, que no
    // filtra y le pone al dibujo el negro de fondo (0x468477).
    std::int32_t filter = 0;
    TextField text{10, false, TextField::Charset::Latin1};
    // +0x80: los dibujos (0x468320 suma uno al final de cada uno); el cursor se ve con el resto de 24 menor que 12.
    std::int32_t draws = 0;
    // +0x9c y +0xa0 (0x4674c0; la vtable + 0x40 y + 0x44): los colores del texto y del fondo del bitmap del EDIT
    // (0x468320), como los lee la CL8 (UI/GameHud, 0x4691e0). Los píxeles del medio (+0xa4, 0xff969696) los pone el
    // dibujo del texto de MuRet.
    std::uint32_t textColor = 0xffffffffu;
    std::uint32_t backgroundColor = 0xff000000u;
    // +0xac: el alto de "Q" en píxeles (0x6c7af4), que 0x468320 mide la primera vez (0 hasta entonces).
    float caretHeight = 0.0f;
    // CL9 hito 10, el EDIT de varios renglones (UI/OldEditLines.h). +0xc4: 1 con ES_MULTILINE | ES_AUTOVSCROLL |
    // WS_VSCROLL (0x4675e0, la vtable + 0x38, antes de crearlo: solo el cuerpo de la carta, 0x4980ef).
    std::int32_t multiline = 0;
    // +0xc8 y +0xcc: las cuentas de las flechas y de la pista de 0x468d50, como las de la lista.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0xd0: los renglones que entran, ftol(h · ry / cy de "1") al crearlo (0x467fe0, con +0xc4 en 1; si no, 0).
    std::int32_t visibleLines = 0;
    // +0xd4..+0xe4: la barra de 0x468970 (el ancho, 13; la pista de y + 9 a y + h − 9; el alto y la y del cursor) y
    // +0xe8, el mouse menos la y del cursor al agarrarlo (0x468d50).
    float barWidth = 0.0f;
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbHeight = 0.0f;
    float thumbY = 0.0f;
    float dragOffset = 0.0f;
    // MuRet: lo que el EDIT de Windows guarda y el original le pide con mensajes. El primer renglón a la vista
    // (GetScrollPos de SB_VERT, EM_SCROLL y EM_LINESCROLL), el ancho del área del texto en píxeles (el de la ventana
    // de 0x467fe0 menos la barra de Windows, *inferido*) y si el último cambio del texto pide mostrar el cursor
    // (ES_AUTOVSCROLL).
    std::int32_t firstLine = 0;
    std::int32_t textWidth = 0;
    bool followCaret = false;
};
} // namespace Client::UI
