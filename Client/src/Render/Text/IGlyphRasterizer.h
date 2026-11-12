#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/TextBox.h"

#include <string_view>

// Lo que el texto le pide a GDI en el original (06 § Texto): medir, como GetTextExtentPointA, y
// dibujar, como TextOutA sobre el DIB. Lo implementa FreeTypeRasterizer; los tests del diseño y de
// la caché usan uno falso (tests/support/FakeGlyphRasterizer.h), que corre sin fuente.
//
// Un texto es una cadena de bytes, como en el original, y cada byte es un carácter: el de
// Text.bmd es de un byte por letra (Q-C67).
namespace Client::Render::Text
{
// El SIZE de GetTextExtentPointA: el avance sumado de las letras y el alto de la celda.
struct TextExtent
{
    int width = 0;
    int height = 0;

    friend bool operator==(const TextExtent&, const TextExtent&) = default;
};

// GetTextExtentPointA sobre el DC de los textos (0x57634f4): lo que piden también el paso y los paquetes, como el área
// de mensajes al agregar un renglón (0x555190). Lo cumplen el rasterizador y el que dibuja (Scenes::ITextDrawer).
class ITextMeasure
{
public:
    virtual ~ITextMeasure() = default;

    // GetTextExtentPointA con la fuente `font`. Un texto vacío mide 0 de ancho y el alto de la
    // celda.
    [[nodiscard]] virtual TextExtent Measure(FontKind font, std::string_view text) const = 0;
};

class IGlyphRasterizer : public ITextMeasure
{
public:

    // TextOutA en (−firstColumn, 0), con la celda arriba (TA_TOP) y sin suavizado: pone 0xff en
    // cada píxel de `ink` donde cae una letra. No borra lo demás: el que llama la da en 0, como
    // 0x467060 borra el DIB.
    virtual void DrawInk(FontKind font, std::string_view text, int firstColumn, InkMask ink) const = 0;
};
} // namespace Client::Render::Text
