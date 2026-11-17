#pragma once

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "UI/Widget.h"

#include <cstdint>
#include <string>

namespace Client::Render
{
class SpriteBatch;
}

// El dibujo de los globos (CL8 hito 3; 06 § El chat, "Los globos"): el 12 del HUD, 0x5a9050, con la medida (0x555840),
// la proyección (0x6205b0) y el dibujo de cada uno (0x555b00 con TextOut distinto de 1, 0x553e50 y 0x554000). Nada de
// esto saca rand(). Escribe en cada entrada dónde quedó y cuánto mide (+0x278..+0x284), que lee el clic derecho del
// paso (0x557650).
namespace Client::Scenes
{
class ITextDrawer;
class WorldState;
struct ChatBubble;
struct GameProjection;

// 0x555840(entrada) con la fuente normal en el DC: el ancho (+0x280) es el mayor de los del nombre más 3, el gremio y
// el puesto, con el renglón de abajo si alguno de los dos tiene tiempo y el de arriba si lo tiene el de arriba; el alto
// (+0x284), el paso de la letra (0x7758568) por las cinco medidas que dan TRUE, que son todas (Q-C520). Con el título
// de una tienda personal corta (0x5559dd, CL9.4).
[[nodiscard]] Core::Expected<void, std::string>
MeasureTheChatBubble(ChatBubble& bubble, const Render::Text::ITextMeasure& measure, int fontStep);

// Lo que el dibujo lee y deja.
struct ChatBubbleDrawContext
{
    WorldState& world;
    // 0x6203c0 del pase del mundo de este frame, el que lee 0x6205b0. Sin ella, un globo vivo corta.
    const GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    ITextDrawer& text;
    // El SelectObject de 0x5a9050 (la normal).
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c y 0x6c8e64: quedan los del último renglón.
    Render::Text::TextColors& textColors;
    // 0x77f3b66, que 0x555b00 prende, y 0x77f31b8 y 0x77f31bc, el ancho (hasta 1.280) y el alto del último globo.
    std::uint8_t& clampBoxes;
    std::int32_t& boxWidth;
    std::int32_t& boxHeight;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    UI::Point mouse{};
    // 0x77f3b94: el contador del área vieja.
    std::int32_t blink = 0;
    // Un dibujo intermedio de MuRet no escribe la tabla: el paso del original lee lo del último frame suyo.
    bool projecting = false;
};

// 0x5a9050: SelectObject de la normal; cada entrada con el nombre o el renglón de abajo con tiempo va 60 sobre el
// techo de su dueño (+0x138, 0x6b86cc), proyectada a 640 × 480, se mide, y queda centrada en la x y 0x24 más arriba;
// después, cada par que se pisa corre la y del primero arriba o abajo del otro, según de qué lado de su mitad esté, y
// cada una va contra los bordes (la x con W − ancho y la y con H − alto, en píxeles, Q-C522) y se dibuja con 0x555b00.
// Un globo sin dueño (+0x288, las tiendas de la CL9.4) corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheChatBubbles(ChatBubbleDrawContext& context);
} // namespace Client::Scenes
