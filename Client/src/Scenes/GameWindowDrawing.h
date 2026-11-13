#pragma once

#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"

#include <cstdint>
#include <string_view>

// Las primitivas que comparten las ventanas del paso que son marco (CL8 hito 6, 08 P-C92): la party (0x5f0be0) y los
// comandos (0x40a650). En 640 × 480, con el estado de GL que dejan y los globales de los textos (0x6c8e5c, 0x6c8e64,
// la fuente del DC y 0x77f31b8/0x77f31bc) en HudState.
namespace Client::Scenes
{
struct GameHudContext;
}

namespace Client::Scenes::WindowDrawing
{
// 0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa, con el color y la
// mezcla que haya.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float du,
          float dv);

// 0x5ee220(x, y, 0), y la copia que tiene 0x5f0be0: Interface\Item_Back01.jpg (0x104) de 190 × 256 en (x, y) y
// Item_Back02.jpg (0x105) de 190 × 177 en y + 256 (0x6b8840, la suma de la x87 guardada en float), las dos con du =
// 0,7421875 y dv = 1 y 0,69140625.
void DrawTheWindowBack(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y);

// SelectObject sobre el DC de los textos (0x57634f4): la fuente con la que miden y dibujan los que siguen.
void SelectFont(GameHudContext& context, Render::Text::FontKind font);

// 0x554180(x, y, texto, 0, 0, 0): el modo 1 desde x, con la fuente del DC y los colores de 0x6c8e5c y 0x6c8e64. Un
// texto vacío no se dibuja, pero se mide igual: su tamaño en 640 × 480 queda en 0x77f31b8 y 0x77f31bc.
void DrawText(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// 0x554180(x, y, texto, 120 · W / 640, 1, 3), el título de una ventana: 0x467060 con el modo 2 centra el texto en una
// caja de 120 · W / 640 píxeles (la división sin signo de 0x5f0cd6..0x5f0cef) que empieza en _ftol(x · rx), con el
// fondo en toda la caja (Render::Text::CenterInBox e ITextDrawer::DrawColumns, Q-C710), y deja en 0x77f31b8 el ancho
// recortado a la caja. Un texto vacío mide el "0" y dibuja solo el fondo.
void DrawTitle(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// Lo mismo con una caja de `designWidth` · W / 640 píxeles: 0x554180(x, y, texto, k · W / 640, 1, ·), que no lee el
// último argumento (las tiendas personales, 0x5f3030, con 150 y 52).
void DrawCenteredInBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                       std::uint32_t designWidth);

// 0x623690(x, y, texto): x − (cx · 640 / W) / 2 y 0x554180(…, 0, 0, 0).
void DrawCenteredText(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// El botón de cerrar de las ventanas (0x5f11c2 en la party, 0x40a78e en los comandos y 0x5f74a3 en el personaje):
// glColor3f(1, 1, 1) y Interface\exit_01.jpg (0x118) de 24 × 24 en (x + 25, y + 395), sumas de la x87 guardadas en
// float, con du = dv = 0,75. Devuelve si el mouse está adentro (x87: izquierda ≤ mouse < izquierda + 24).
[[nodiscard]] bool DrawTheCloseButton(GameHudContext& context, std::int32_t x, std::int32_t y);

// Lo que sigue con el mouse sobre el botón de cerrar: la fuente normal, los colores −1 y 0xff000000 y la ayuda
// (0x554340) en (_ftol(x + 25), _ftol(y + 395) − 13), que deja la fuente normal y los colores −1 y 0.
void DrawTheCloseHelp(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help);
} // namespace Client::Scenes::WindowDrawing
