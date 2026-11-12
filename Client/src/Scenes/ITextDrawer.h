#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"

#include <string_view>

// Un texto en (x, y) de 640 × 480, como 0x554180 con TextOut distinto de 1, el del login (CL5.4).
// Lo implementa TextCacheDrawer sobre el TextCache de la CL5.3; los tests, el RecordingTextDrawer
// de support/, que anota. Dibuja en el lote del frame, con el color y la mezcla que haya.
namespace Client::Scenes
{
// Measure (Render::Text::ITextMeasure) da el ancho y el alto del texto en píxeles, para ubicarlo.
class ITextDrawer : public Render::Text::ITextMeasure
{
public:
    virtual void Draw(int x, int y, std::string_view text, const Render::Text::TextStyle& style) = 0;
};
} // namespace Client::Scenes
