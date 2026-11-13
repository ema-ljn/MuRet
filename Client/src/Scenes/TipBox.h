#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Render
{
class SpriteBatch;
}

// La caja de ayuda (CL8 hito 6; 06 § HUD y ventanas, "Las ayudas de F1"): 0x5af090 y su lista de renglones de la .bss,
// que llenan las ayudas de 0x5b0550, el cartel del conjunto (0x40eca0) y los tooltips (0x5f7bc0).
namespace Client::Scenes
{
class ITextDrawer;
struct HudState;

// 0x78793c8: renglones de 100 bytes. 0x40eca0 vacía los primeros 30 (hasta 0x7879f80) y 0x787a15c ya es otro global
// (*inferido*): 34 entran.
inline constexpr std::size_t TipLineSize = 100;
inline constexpr std::size_t TipLineCount = 34;

struct TipLine
{
    // El texto, hasta su '\0'.
    std::string text;
    // 0x787a560 + 4 · i: el color (0x5af368, de 0 a 10).
    std::int32_t color = 0;
    // 0x78a9230 + 4 · i: con otro que 0, la negrita (0x5763558); si no, la normal (0x5763554).
    std::int32_t bold = 0;
};

struct TipList
{
    std::array<TipLine, TipLineCount> lines{};
    // 0x78abb30: cuántos renglones llenó el último que la escribió.
    std::int32_t count = 0;
};

// Lo que lee y escribe 0x5af090: el lote, los textos y, de HudState, la lista, la fuente del DC (0x57634f4, la del
// último renglón) y los colores de 0x465950 (0x6c8e5c y 0x6c8e64, los del último renglón dibujado).
struct TipBoxContext
{
    Render::SpriteBatch& sprites;
    ITextDrawer& text;
    HudState& hud;
};

// 0x5af090(x, y, count, width, mode, frame).
struct TipBoxCall
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t count = 0;
    std::int32_t width = 0;
    std::int32_t mode = 0;
    bool frame = false;
};

// 0x5af090: mide los renglones hasta el primero vacío (el más ancho y el alto del último), arma la caja centrada en x
// (con 4 de margen, sin salir de la pantalla) con la mezcla normal, el marco negro de 1 y el fondo negro al 80 % si
// `frame`, y dibuja cada renglón con su fuente y su color, centrado en la caja (0x465950 con el modo 2), salvo los que
// empiezan con '\n' o son un espacio solo, que bajan medio renglón o uno. Termina con el blanco y sin mezcla. Corta
// con otro modo que el 2, un renglón con fondo (los colores 5, 7, 8 y 9) o más de TipLineCount renglones llenos.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTipBox(TipBoxContext& context, const TipBoxCall& call);
} // namespace Client::Scenes
