#pragma once

#include "Render/Text/FontSet.h"

#include <cstdint>

// Lo que el original deja en globales antes de dibujar un texto con 0x554180: la fuente elegida
// con SelectObject, el color del texto (DAT_006c8e5c) y el del fondo (DAT_006c8e64). MuRet los
// pasa en cada llamada (06 § Texto). El ancho de la caja que también escribe el login
// (DAT_006c8e70 = 0x100) no va: 0x467060, el texto del login, no lo lee; lo lee solo 0x553ae0
// (en 0x553bef), que el login no llama.
namespace Client::Render::Text
{
// Los colores en el formato del original, 0xAABBGGRR: el byte de abajo es el rojo, como lo deja
// 0x465a00 en el buffer RGBA de 0x554000 (un dword por píxel). 0x80000000 es negro con alfa 0x80.
struct TextColors
{
    std::uint32_t text = 0xffffffff;
    std::uint32_t background = 0;

    friend bool operator==(const TextColors&, const TextColors&) = default;
};

struct TextStyle
{
    FontKind font = FontKind::Normal;
    TextColors colors;

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};
} // namespace Client::Render::Text
