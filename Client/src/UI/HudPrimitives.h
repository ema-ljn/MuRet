#pragma once

#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Scenes/ITextDrawer.h"

#include <string_view>

// Las primitivas que el HUD del juego comparte (06 § HUD y ventanas, CL8 hito 2): las cifras de FontTest
// (0x6218f0), las barras chicas (0x5a6af0) y la ayuda con su marco que sale con el mouse encima (0x554340). Todas en
// 640 × 480 y con el estado de GL que dejan, como el original.
namespace Client::UI
{
// Interface\FontTest.tga, el atlas de las cifras (Scenes::DamageTextureSet): 16 columnas de 16 píxeles en una fila
// de 32 de las 64 de alto.
inline constexpr Render::TextureSlot DigitsSlot{1};

// 0x6218f0(x, y, n, w, h): `number` en decimal (_itoa en base 10) centrado en `x`: corre la x la mitad del ancho de
// las cifras (len · w · 0,5) y dibuja cada carácter c con 0x621b60(1, x, y, w, h, (c − '0') · 0,0625, 0, 0,0625,
// 0,5, 1, 1, 0), escalado y sin alfa, con el color y la mezcla que haya; después de cada uno suma w · 0,7 en float.
// El '-' de un negativo es la cifra −3, como en el original. Devuelve la x del final.
float DrawNumber(Render::SpriteBatch& sprites, float x, float y, int number, float width, float height);

// 0x5a6af0(x, y, w, h, lleno, rojo, acotar): la barra chica del ayudante y de la vida del invocado. Con `keepOnScreen`
// la mete en (0..640, 0..433): una x o una y negativas van a 0, y si x + w + 4 pasa de 640 la x es 640 − (w + 5) (y
// lo mismo con 433 y la altura). Después la sombra (0x620a00(1), negro con alfa 0,5, (x + 1, y + 1, w + 4, h + 4)),
// y con la mezcla aditiva el marco (0,2; 0; 0) o (0; 0,2; 0,2) en (x, y, w + 4, h + 4), el fondo (50, 10, 0)/255 o
// (0, 50, 50)/255 en (x + 2, y + 2, w, h) y lo lleno, (200, 50, 0)/255 o (0, 200, 50)/255, en (x + 2, y + 2, lleno,
// h). Termina con 0x620980, sin mezcla.
void DrawGauge(Render::SpriteBatch& sprites, float x, float y, float width, float height, float fill, bool red,
               bool keepOnScreen);

// 0x554340(x, y, texto): `help` con la fuente normal, en blanco sobre un fondo transparente (0x6c8e5c en −1 y
// 0x6c8e64 en 0, que quedan así), dentro de un marco negro de un punto (x − 2, y − 3, cx/rx + 4, cy/ry + 4) y un
// fondo negro con alfa 0,8 en (x − 1, y − 2, cx/rx + 2, cy/ry + 2), con cx y cy en píxeles y rx y ry los de la
// pantalla. Guarda la mezcla de antes (0x7d366dc) y la vuelve a poner al final; el color queda en blanco.
void DrawHelp(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, int x, int y, std::string_view help);
} // namespace Client::UI
