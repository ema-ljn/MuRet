#pragma once

#include "App/WindowState.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace Client::Render
{
class SpriteBatch;
}

// Las piezas del dibujo de las ventanas viejas que comparten sus clases (CL8 hito 7 y CL9 hito 9; 06 § HUD y
// ventanas): las primitivas de 0x621aa0 y 0x621b60 con la paleta de 0x491960, el texto de 0x5542f0, el botón, los
// encabezados de pestañas, la casilla, la lista de la plantilla (0x47a810 y sus iguales) con su barra y su renglón
// elegido, y el EDIT. Las usan UI/OldWindowDraw.cpp (el marco, la caja y la lista de ventanas) y
// UI/FriendWindowsDraw.cpp (la ventana de amigos), y son para las ventanas del chat con un amigo y de las cartas
// (hito 10). Todo en 640 × 480.
namespace Client::UI::OldDraw
{
// _ftol (0x69fd3c): trunca hacia el 0.
[[nodiscard]] std::int32_t Truncate(double value);

// rx y ry (0x5762ea8 y 0x5762eac): W / 640 y H / 480 en float. Sin pantalla, 1.
[[nodiscard]] double RatioX(App::PixelSize screen);
[[nodiscard]] double RatioY(App::PixelSize screen);

[[nodiscard]] float ToFloat(std::int32_t value);

// glColor4f(g, g, g, 1).
void Gray(Render::SpriteBatch& sprites, float gray);

// 0x459ec0: glColor4f(1, 1, 1, 1) y glEnable(GL_TEXTURE_2D), después de los quads sin textura.
void WhiteWithTexture(Render::SpriteBatch& sprites);

// 0x491960(índice, alfa) con OldWindowPalette: un índice fuera de los 19 no cambia el color.
void SetThePalette(Render::SpriteBatch& sprites, std::int32_t index, float alpha);

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
          float v, float du, float dv);

// 0x5542f0(x, y, texto, 0, 1, 0) → 0x465950: el texto con la fuente del DC y los colores de 0x6c8e5c; uno vacío no
// se dibuja. Devuelve lo que deja en la x87 0x467060: el alto del texto en 640 × 480 (cy / ry), la mitad si empieza
// con '\n', o 0 sin texto.
double DrawText(const OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// 0x459f00(x, y, w, h, 1) con el mouse del dibujo.
[[nodiscard]] bool MouseIn(const OldWindowDrawView& view, std::int32_t x, std::int32_t y, std::int32_t width,
                           std::int32_t height);

// 0x45b2e0, la vtable + 0x14 del botón: win_button.tga del gris del mouse y el texto centrado. Apagado, en rojo y sin
// volver a la mezcla opaca.
void DrawTheButton(const OldWindowDrawContext& context, const OldButton& button, const OldWindowDrawView& view);

// 0x4a2460(x, y, w, h, cuántos, el de la vista): `count` encabezados de w × h, uno al lado del otro desde x. El de la
// vista, con la raya de arriba y los costados (el 2 de la paleta) y el relleno del 5, un punto adentro; los demás,
// un punto más abajo, con la raya de arriba, el costado derecho y la de abajo, y el relleno del 6 dos más abajo.
void DrawTheTabHeaders(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, std::int32_t width,
                       std::int32_t height, std::int32_t count, std::int32_t current);

// 0x463090(x, y, marcada): sin mezcla, el marco de 9 × 9 con el 0 de la paleta; con la mezcla normal, la marca
// (win_check.tga, 0x64a) de 5 × 5 en (x + 2, y + 2). Deja la mezcla normal y el blanco con textura.
void DrawTheCheckbox(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool checked);

// 0x461890 y 0x462620, la vtable + 0x7c de la lista de amigos y de la de ventanas (byte a byte iguales): ubica la
// barra (+0x70, 0x47b9c0), las flechas de win_scrollbar.jpg (0x649) en el borde derecho, dadas vuelta con el clic
// sobre ellas, los dos filos de la pista (el 2 de la paleta) y el cursor en tres quads: con menos que los renglones,
// toda la pista.
void DrawTheListBar(Render::SpriteBatch& sprites, OldList& list, std::size_t size, const OldWindowDrawView& view);

// La vtable + 0x7c, + 0x80 y + 0x84 de una instancia de la lista, como las llama su dibujo.
struct OldListDrawing
{
    // +0x7c: antes de los renglones (la barra, en las de la CL9).
    std::function<void()> head;
    // +0x80(renglón): el renglón `row` (desde 0, el primero que se ve) con el elemento `at` del deque. Lo que devuelve
    // corre los renglones como en el paso: negativo saltea, 0 repite el renglón y positivo sigue.
    std::function<std::int32_t(std::int32_t row, std::size_t at)> row;
    // +0x84: después (0x472110 y 0x4722f0, un ret, en las de la CL9).
    std::function<void()> tail;
};

// 0x47a810, la vtable + 0x14 de la lista (0x47d730 de la de ventanas y 0x47eed0 de la de cartas son iguales salvo el
// tamaño del elemento): +0x7c, el primero que se ve (+0x74, 0x47bb00), glColor3f(1, 1, 1), la fuente normal en el DC
// (0x5763554) y los renglones hasta los que muestra o hasta el final, y +0x84.
void DrawTheOldList(const OldWindowDrawContext& context, const OldList& list, std::size_t size,
                    const OldListDrawing& drawing);

// El principio de 0x461cd0 y 0x462a60 (la vtable + 0x80): con la mezcla normal, el renglón elegido (+0x68 igual al
// corrimiento + renglón + 1) va sobre una franja blanca de 13 de alto, 3 más arriba que su y, con alfa 0,7 si la lista
// tiene las flechas del teclado (0x5762ec0) y 0,3 si no, y el texto en negro; los demás, en 0xffc8dce6. El fondo
// del texto en 0.
void DrawTheChosenRow(const OldWindowDrawContext& context, const OldList& list, std::size_t size, std::int32_t row,
                      std::int32_t focusedList);

// 0x468320, la vtable + 0x14 del EDIT, con lo que MuRet tiene de él (P-C181): la caja del bitmap del EDIT en el color
// de +0xa0 (sin +0xc4, el ancho del texto más +0xa8 + 1,5, hasta el del EDIT, y su alto; con +0xc4, todo el ancho y,
// con menos renglones que +0xd0, el alto de los renglones), el texto en el de +0x9c con la fuente normal (con +0xc4,
// los renglones de UI/OldEditLines.h desde el primero a la vista), y el cursor: la columna que está más de
// GetCaretPos y menos de GetCaretPos + +0xa8 (0x468805..0x46881d: con 1,5, una sola) y las filas del alto de "Q"
// (+0xac), al final del texto, prendido 12 de cada 24 dibujos con el foco de Windows; con +0xc4 en 1, la barra
// (0x468970) con el mouse de `view`; +0x80 cuenta los dibujos. Con +0x4c & 8, antes, el negro de su ancho y del alto
// de "Q". Sin la contraseña (+0xb0, con '*' y la fuente 0x5763558), que ninguno de los de las ventanas viejas usa; el
// bitmap de más de 256 píxeles, que el original parte en dos texturas, es el mismo dibujo.
void DrawTheOldEdit(const OldWindowDrawContext& context, OldEdit& edit, bool focused,
                    const OldWindowDrawView& view = {});

// 0x468970: la barra del EDIT de varios renglones. Pone +0xd4..+0xe4 (13 de ancho, la pista de y + 9 a y + h − 9 y el
// cursor de pista · min(+0xd0 / renglones, 1) en pista · min(GetScrollPos / renglones, 1)); las flechas de
// win_scrollbar.jpg en x + w − 15, y − 4 e y + h − 9, dadas vuelta con el clic sobre ellas; con la mezcla normal, los
// dos filos de la pista (el 2 de la paleta); y sin mezcla, el cursor en tres quads.
void DrawTheEditBar(const OldWindowDrawContext& context, OldEdit& edit, std::int32_t lineCount,
                    const OldWindowDrawView& view);
} // namespace Client::UI::OldDraw
