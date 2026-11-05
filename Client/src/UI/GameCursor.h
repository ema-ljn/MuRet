#pragma once

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "UI/Widget.h"

// El cursor del juego (0x5ac2d0): el original esconde el de Windows (ShowCursor(FALSE) en WinMain,
// 0x4ba238, y en el WndProc, 0x4b3327) y dibuja el suyo al final de la UI de cada escena: el login
// (0x635560), la selección (0x634980) y el juego (0x6376d7). Contrato: 06 § Lo demás del login.
namespace Client::UI
{
// Interface\Cursor.tga y Interface\CursorPush.tga, que sube 0x61ccb0 (0x61ccf9 y 0x61cd12) con
// GL_LINEAR y GL_CLAMP.
inline constexpr Render::TextureSlot CursorSlot{2};
inline constexpr Render::TextureSlot CursorPushSlot{3};

// La posición del mouse que escribe el WndProc (0x4b358f..0x4b35c8) y lee el cursor: x en 0x7d3682c e
// y en 0x7d36828, LOWORD · 640 / W y HIWORD · 480 / H, en enteros sin signo y sin acotar (Q-C47). De
// `input`, que llega en píxeles del framebuffer: el píxel de abajo (el −0,5 es el −1) y sus 16 bits
// bajos, como el LOWORD y el HIWORD del lParam. Fuera de la ventana el original no recibe el movimiento
// (no captura el mouse); SDL sí, con un botón abajo, y entonces el cursor sigue afuera (08, P-C27 g). Una
// posición que no es un número o una pantalla vacía dan 0.
[[nodiscard]] Point MouseIn640x480(const Input::InputState& input, App::PixelSize screen);

// 0x5ac7da: el cursor de 24 × 24 en (x − 2, y − 2) de 640 × 480, escalado en tamaño y en posición, entero
// y sin alfa; con el botón izquierdo abajo (0x7d36874, que en el login y la selección solo escriben el
// WM_LBUTTONDOWN y el WM_LBUTTONUP), el CursorPush (0x5ac76e). Los demás cursores (4 a 0xa, 0x65a y
// 0x68d) son del juego: los eligen 0x6c8e28 a 0x6c8e34, 0x78abb0c, 0x78abb20, 0x7d3a10c y 0x77f3b5c, que
// en el login y la selección están en −1 o en 0, y 0x659290(0x200) en 0x5ac61c, que en la selección da
// false y en el login no está verificado (08, P-C27 h).
[[nodiscard]] Render::OriginalQuad CursorQuad(Point mouse, bool leftDown);

// 0x620a00(1), glColor3f(1, 1, 1) y el quad de CursorQuad con el mouse y el botón de `input`.
void DrawGameCursor(Render::SpriteBatch& sprites, const Input::InputState& input, App::PixelSize screen);
} // namespace Client::UI
