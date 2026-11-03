#pragma once

#include "App/WindowState.h"
#include "Render/QuadCorner.h"
#include "Render/TextureSlot.h"

// El quad de 0x622000, el de la arena que corre en el login (06 § Primitivas 2D y § El login, el
// dibujo): el de 0x621b60 siempre escalado desde 640 × 480, sin alfa, y con la v corrida un cuarto
// de dv del lado izquierdo, así que la textura se inclina. Lo arma el SpriteBatch.
namespace Client::Render
{
// Los nueve argumentos de 0x622000, en su orden: 0x622000(slot, x, y, w, h, u, v, du, dv), con x,
// y, w y h en 640 × 480.
struct OriginalSkewedQuad
{
    TextureSlot slot{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float du = 1.0f;
    float dv = 1.0f;
};

// Las cuatro esquinas de 0x622000: las posiciones de 0x621b60 con las dos banderas prendidas, y las
// UV (u, v + 0,25 dv), (u, v + 0,75 dv), (u + du, v + dv) y (u + du, v). La de abajo a la izquierda
// sale de v + dv sin redondear menos 0,25 dv (0x6220b7..0x6220c2), no de v + 0,75 dv.
[[nodiscard]] QuadCorners SkewedFanCorners(const OriginalSkewedQuad& quad, App::PixelSize screen);
} // namespace Client::Render
