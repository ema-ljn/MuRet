#pragma once

#include "App/WindowState.h"
#include "Render/QuadCorner.h"
#include "Render/TextureSlot.h"

// El quad de 0x621b60, el átomo de la UI del original (06 § Primitivas 2D): una textura en un
// rectángulo, con la y dada vuelta (H − y), la posición y el tamaño escalados desde 640 × 480 si
// sus banderas lo piden, y un alfa que cambia el color de los vértices. Lo arma el SpriteBatch.
namespace Client::Render
{
// Los argumentos de 0x621b60, en su orden: 0x621b60(slot, x, y, w, h, u, v, du, dv, escalar
// tamaño, escalar posición, alfa). x e y son la esquina de arriba a la izquierda, con la y desde
// arriba, en 640 × 480 si scalePosition, o en píxeles.
struct OriginalQuad
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
    bool scaleSize = false;
    bool scalePosition = false;
    // Con más de 0 (DAT_006b86a0), cada vértice va con (1, 1, 1, alfa) y el color de GL queda en
    // blanco (Q-C57); con 0, el quad usa el color que haya.
    float alpha = 0.0f;
};

// Las cuatro esquinas de 0x621b60 en una pantalla de `screen`: (x, H − y), (x, H − y − h),
// (x + w, H − y − h) y (x + w, H − y), con las UV (u, v), (u, v + dv), (u + du, v + dv) y
// (u + du, v): la v de arriba es la fila 0 del buffer de la textura. El borde de abajo se resta
// del de arriba sin redondearlo antes, como la x87 (FST y FLD ST0 en 0x621beb..0x621bf3): en
// 1280 × 1024 el fondo 0x15 del login termina en 287,99997 y no en 288. 0x621aa0, el quad sin
// textura, hace las mismas cuentas con las dos banderas prendidas.
[[nodiscard]] QuadCorners FanCorners(const OriginalQuad& quad, App::PixelSize screen);
} // namespace Client::Render
