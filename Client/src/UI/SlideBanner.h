#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Data::Text
{
struct SlideTable;
}

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Scenes
{
class ITextDrawer;
}

// El cartel que cruza la pantalla en el juego (CL8 hito 3; 06 § El cartel de Slide.bmd): el objeto [0x5763528]
// (0x46aea0, 0x121c bytes) con sus dos colas (0x46a020, de 0x8d4 bytes: la normal en +4 y la de la fuente gruesa en
// +0x8d8) y los tramos de Slide.bmd. Lo llenan el timer 0x3eb (0x46b0e0, Scenes/Slides) y el 0D de 10 a 15
// (0x4c66b3), lo pasa el paso de la UI (0x46b7b0) y lo dibuja el 15 del HUD (0x46b090). Ninguna de estas funciones
// saca rand() salvo 0x46b680. Cada byte es una letra (Q-C67): _mbclen da 1.
namespace Client::UI
{
inline constexpr std::size_t SlideGroups = 5;

// Una entrada de una cola: el valor del multimap de +0x84 (el nodo + 0x10..+0x20).
struct SlideEntry
{
    // +0x10: lo que pide 0x46b720 menos 1 o menos 4. Con −1 caduca a los 60 segundos de su clave (0x46ae07).
    std::int32_t kind = 0;
    // +0x14: el texto. 0x46abf0 hace una copia por llamada y la suelta la última repetición (+0x20); MuRet guarda
    // una por entrada, porque las repeticiones salen siempre en orden y la última es la de más atrás.
    std::string text;
    // +0x18: 0xAABBGGRR, como 0x6c8e5c.
    std::uint32_t color = 0;
    // +0x1c: la velocidad (0x46abc0).
    float speed = 0.0f;
};

// 0x46a020: una cola. Lo que el constructor no escribe (+0x50, +0x54, +0x7c, +0x80 y el texto) lo pone 0x46b010 en el
// primer paso del juego.
struct SlideQueue
{
    // +0x50 y +0x54 (0x45a9d0): dónde va, en 640 × 480. La y sube una vez el alto de la letra (0x46a770).
    std::int32_t x = 0;
    std::int32_t y = 0;
    // +0x7c y +0x80: la hora del último segundo contado (timeGetTime) y los segundos que van (0x46ada0).
    std::uint32_t lastTick = 0;
    std::uint32_t seconds = 0;
    // +0x84: el multimap por el segundo de salida, sin signo. Con la misma clave, en el orden en que entraron.
    std::multimap<std::uint32_t, SlideEntry> entries;
    // +0xa4: la fuente (0x5763554 o 0x5763558).
    Render::Text::FontKind font = Render::Text::FontKind::Normal;
    // +0xa8: lo que pasa, en el buffer de 0x400 bytes de turno (+0xac o +0x4ac).
    std::string text;
    // +0x8ac..+0x8b8: el corrimiento, el empuje (1, o −1 con el mouse encima), la velocidad y la velocidad máxima (2,5
    // del constructor). El corrimiento suma a la x; negativo, son columnas del texto que no se ven (0x467060 con el
    // modo 6, Q-C519).
    float scroll = 0.0f;
    float push = 1.0f;
    float velocity = 0.0f;
    float maxSpeed = 2.5f;
    // +0x8bc: la opacidad, de 0 a 205 de a 30.
    std::int32_t alpha = 0;
    // +0x8c0 y +0x8c4: los bytes y el ancho en píxeles del pedazo de adelante, de hasta 4 letras (0x46ab30).
    std::uint32_t chunkBytes = 0;
    std::int32_t chunkWidth = 0;
    // +0x8c8: el alto de la letra en 640 × 480 (el de "Z"), 0 hasta el primer dibujo con texto.
    std::int32_t lineHeight = 0;
    // +0x8cc: el color del texto que pasa.
    std::uint32_t color = 0;
    // +0x8d0: con 1, el alfa del texto baja a la mitad 5 de cada 10 dibujos. 0x46b010 pasa 0 y nadie más lo escribe.
    std::int32_t blink = 0;
};

struct SlideBanner
{
    // +4 y +0x8d8.
    SlideQueue normal{};
    SlideQueue bold{};
    // +0x11ac: el período del timer, en segundos (10 del constructor).
    std::int32_t periodSeconds = 10;
    // +0x11b0, +0x11c4 y +0x11d8: los tramos de Slide.bmd (Data::Text::SlideGroup).
    std::array<std::int32_t, SlideGroups> maxLevels{};
    std::array<std::int32_t, SlideGroups> counts{};
    std::array<std::vector<std::string>, SlideGroups> texts{};
    // +0x1218: la velocidad de los carteles del timer (2,5 del constructor).
    float speed = 2.5f;
    // El timer 0x3eb de la ventana: cuándo vence (en ms de timeGetTime) y cada cuánto, desde el primer 0x46b010. El
    // binario no importa KillTimer: no se apaga.
    std::optional<std::uint32_t> timerDue{};
    std::uint32_t timerPeriod = 0;
    // 0x5762ee4 (.bss): los textos que dibujó el cartel, que lee el parpadeo.
    std::int32_t drawn = 0;
};

// 0x46b3b0 después de leer el archivo: el período, la velocidad y los tramos. Si había textos, antes vacía los cinco
// tramos (0x46b5b0).
void LoadTheSlides(SlideBanner& banner, const Data::Text::SlideTable& table);

// 0x46a460(fuente, parpadeo) y los dos 0x45a9d0(0, 0x1ad) de 0x46b010: las dos colas sin texto y en (0, 0x1ad), con
// el segundo 10 desde `now`. No toca sus entradas, su velocidad máxima ni su color.
void ResetTheSlideQueues(SlideBanner& banner, std::uint32_t now);

// 0x46b680: el primer tramo con el nivel menor o igual que su tope y, si su cuenta no es 0, un rand() módulo la cuenta
// (IDIV) y el texto de ese lugar, si la lista lo tiene. Vacío, el NULL que no encola nada.
[[nodiscard]] std::optional<std::string> PickASlide(SlideBanner& banner, std::uint16_t level,
                                                    Render::Models::CrtRandom& random);

// 0x46b720 sin la escena, que mira quien llama: con un texto no vacío, de 1 a 30 repeticiones y el tipo de 0 a 5,
// 0x46abf0 encola `repeats` entradas en la cola normal (0 a 2) o en la gruesa (3 a 5), con el tipo menos 1 o menos 4:
// con 0, todas con la clave 0, y si no, la i-ésima en el segundo de la cola más i · `gap`.
void QueueTheSlide(SlideBanner& banner, std::int32_t repeats, std::uint16_t gap, std::string_view text,
                   std::int32_t kind, float speed, std::uint32_t color);

// Lo que el paso de las colas mide y deja: GetTextExtentPointA, el SelectObject de 0x46a9c0 y rx.
struct SlideStepContext
{
    // Sin medidor, una cola que tiene que agregar un texto corta con error.
    const Render::Text::ITextMeasure* measure = nullptr;
    Render::Text::FontKind& dcFont;
    App::PixelSize screen{640, 480};
    // timeGetTime().
    std::uint32_t now = 0;
};

// 0x46b7b0: 0x46add0 en la gruesa y después en la normal. Una cola sin nada que pasar cuenta su segundo y, si su
// primera entrada ya salió, la agrega al texto con espacios adelante (0x46a9c0; si no entra en 0x400 bytes, espera),
// le pone la velocidad (0x46abc0) y la saca.
[[nodiscard]] Core::Expected<void, std::string> StepTheSlides(SlideBanner& banner, const SlideStepContext& context);

// 0x46a510: la cola no tiene nada que pasar (sin texto, o con menos bytes que su pedazo de adelante).
[[nodiscard]] bool SlideQueueIsIdle(const SlideQueue& queue);

// Lo que el dibujo usa y deja: el lote, los textos, la fuente del DC y los colores de 0x465950, el mouse en 640 ×
// 480 y 0x6c7a8c.
struct SlideDrawContext
{
    Render::SpriteBatch& sprites;
    Scenes::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    Render::Text::TextColors& textColors;
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    bool slidesOn = true;
};

// 0x46b090: con la gruesa pasando, la normal se apaga (0x46a540(1)); con la gruesa sin nada y ya apagada, la normal
// (0x46a540(0)); después, con la normal apagada, la gruesa. Cada cola sube o baja su opacidad, dibuja sus tres
// franjas negras y, si tiene algo, su texto con el modo 6 de 0x467060 y lo corre. Corta en 0x46a850 si tendría que
// sacar de adelante más bytes de los que quedan (Q-C518).
[[nodiscard]] Core::Expected<void, std::string> DrawTheSlides(SlideBanner& banner, const SlideDrawContext& context);
} // namespace Client::UI
