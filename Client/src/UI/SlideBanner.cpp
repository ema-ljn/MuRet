#include "UI/SlideBanner.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/SlideTable.h"
#include "Render/Models/Cloth.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"
#include "UI/ChatLogWindow.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::UI
{
namespace
{
using Render::Text::FontKind;

// 0x46b01d..0x46b04b: las dos colas en (0, 0x1ad), con el segundo 10 de 0x46a460.
constexpr std::int32_t SlideTop = 0x1ad;
constexpr std::uint32_t FirstSecond = 10;
// 0x46b73e y 0x46b746: hasta 30 repeticiones y los tipos de 0 a 5.
constexpr std::int32_t MaxRepeats = 0x1e;
constexpr std::int32_t LastKind = 5;
constexpr std::int32_t FirstBoldKind = 3;
// 0x46ac98: el tipo que entra con la clave 0.
constexpr std::int32_t AtOnceKind = 1;
// 0x46ae07: el tipo que caduca, y a los cuántos segundos.
constexpr std::int32_t ExpiringKind = -1;
constexpr std::uint32_t ExpirySeconds = 0x3c;
// 0x46ada0: un segundo de timeGetTime.
constexpr std::uint32_t Second = 1000;
// 0x46a9c0: 640 (0x6b8cb0) y el tope del buffer, 1024 (0x6b89b8).
constexpr double DesignWidth = 640.0;
constexpr double BufferBytes = 1024.0;
// 0x46ab30 con 4: el pedazo de adelante.
constexpr std::size_t ChunkLetters = 4;
// 0x46abc0: la velocidad de una entrada sin velocidad (0x40200000).
constexpr float DefaultSpeed = 2.5f;
// 0x46a540: la opacidad sube y baja de a 30 hasta 205; las franjas restan 25 hasta 180 (0xb4), y el texto suma 50.
constexpr std::int32_t AlphaStep = 0x1e;
constexpr std::int32_t MaxAlpha = 0xcd;
constexpr std::int32_t PlainBandAlpha = 0xb4;
constexpr std::int32_t BandFade = 0x19;
constexpr std::int32_t TextLift = 0x32;
// 0x6b88f8: 1/255 en float (0x3b808081).
constexpr float InverseByte = 1.0f / 255.0f;
// 0x46a7e3..0x46a7ee: el parpadeo, la mitad de cada 10.
constexpr std::int32_t BlinkCycle = 10;
constexpr std::int32_t BlinkDim = 5;
// 0x46a910: el rectángulo del mouse (0x459f00 con el modo 1), 3 arriba y 6 más que la letra.
constexpr std::int32_t HoverMode = 1;
constexpr std::int32_t HoverAbove = 3;
constexpr std::int32_t HoverExtra = 6;

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x46a510.
bool IsIdle(const SlideQueue& queue)
{
    return queue.text.empty() || queue.text.size() < queue.chunkBytes;
}

// 0x46a460(fuente, parpadeo).
void ResetTheQueue(SlideQueue& queue, FontKind font, std::uint32_t now)
{
    queue.font = font;
    queue.seconds = FirstSecond;
    queue.blink = 0;
    queue.lastTick = now;
    queue.text.clear();
    queue.scroll = 0.0f;
    queue.push = 1.0f;
    queue.velocity = 0.0f;
    queue.chunkBytes = 0;
    queue.chunkWidth = 0;
    queue.lineHeight = 0;
    queue.alpha = 0;
}

// 0x46abf0(repeticiones, salto, texto, tipo, velocidad, color), con la escena y el texto ya mirados.
void Enqueue(SlideQueue& queue, std::int32_t repeats, std::uint16_t gap, std::string_view text, std::int32_t kind,
             float speed, std::uint32_t color)
{
    std::uint32_t offset = 0;
    for (std::int32_t i = 0; i < repeats; ++i)
    {
        SlideEntry entry{.kind = kind, .text = std::string(text), .color = color, .speed = speed};
        // 0x46ac98: el tipo 1 entra con la clave 0 (0x476040); los otros, con el segundo más el salto. El multimap
        // (+0x8c en 1) inserta después de los de la misma clave, como std::multimap.
        const std::uint32_t key = kind == AtOnceKind ? 0U : queue.seconds + offset;
        queue.entries.emplace(key, std::move(entry));
        offset += gap;
    }
}

// 0x46ab30(texto, 4): los bytes del pedazo de adelante quedan en +0x8c0, salvo con el texto vacío, que no lo toca y
// mide 0.
std::int32_t MeasureTheChunk(SlideQueue& queue, const Render::Text::ITextMeasure& measure)
{
    if (queue.text.empty())
        return 0;
    const std::size_t bytes = std::min(ChunkLetters, queue.text.size());
    queue.chunkBytes = static_cast<std::uint32_t>(bytes);
    return measure.Measure(queue.font, std::string_view(queue.text).substr(0, bytes)).width;
}

// 0x46a9c0(texto, color): los espacios de un ancho de pantalla y el texto, detrás de lo que pasa. Sin lugar en los
// 0x400 bytes, false.
Core::Expected<bool, std::string> Append(SlideQueue& queue, const SlideEntry& entry, const SlideStepContext& context)
{
    if (entry.text.empty())
        return true;
    queue.color = entry.color;
    context.dcFont = queue.font;
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x46a9c0: no hay con qué medir el espacio del cartel"));
    // 0x46aa2e..0x46aa52: 640 / cx · rx en la x87, hasta 1024. Un espacio de ancho 0 da infinito, que también para en
    // 1024.
    const std::int32_t space = context.measure->Measure(queue.font, " ").width;
    double spaces = BufferBytes;
    if (space != 0)
        spaces = DesignWidth / static_cast<double>(space) * static_cast<double>(Render::ScaleX(1.0f, context.screen));
    spaces = std::min(spaces, BufferBytes);
    if (static_cast<double>(queue.text.size()) + static_cast<double>(entry.text.size()) + spaces > BufferBytes)
        return false;
    queue.text.append(static_cast<std::size_t>(spaces), ' ');
    queue.text += entry.text;
    return true;
}

// 0x46abc0: rx por la velocidad, o por 2,5 si no es positiva.
void SetTheSpeed(SlideQueue& queue, float speed, App::PixelSize screen)
{
    if (speed <= 0.0f)
        speed = DefaultSpeed;
    queue.maxSpeed = F32(static_cast<double>(Render::ScaleX(1.0f, screen)) * static_cast<double>(speed));
}

// 0x46add0.
Core::Expected<void, std::string> StepTheQueue(SlideQueue& queue, const SlideStepContext& context)
{
    if (!IsIdle(queue))
        return {};
    // 0x46ada0: suma sin signo.
    if (static_cast<std::uint32_t>(queue.lastTick + Second) < context.now)
    {
        queue.lastTick = context.now;
        ++queue.seconds;
    }
    if (queue.entries.empty())
        return {};
    const auto head = queue.entries.begin();
    if (head->first > queue.seconds)
        return {};
    const SlideEntry& entry = head->second;
    if (entry.kind != ExpiringKind || queue.seconds <= static_cast<std::uint32_t>(head->first + ExpirySeconds))
    {
        auto appended = Append(queue, entry, context);
        if (!appended.HasValue())
            return Core::Unexpected(std::move(appended).Error());
        if (!appended.Value())
            return {};
    }
    SetTheSpeed(queue, entry.speed, context.screen);
    queue.entries.erase(head);
    return {};
}

// 0x46a850: con el corrimiento pasado del pedazo de adelante, lo saca y suma su ancho.
Core::Expected<void, std::string> DropTheChunk(SlideQueue& queue, const Render::Text::ITextMeasure& measure)
{
    if (queue.chunkWidth == 0)
        queue.chunkWidth = MeasureTheChunk(queue, measure);
    if (!(static_cast<double>(queue.scroll) < -static_cast<double>(queue.chunkWidth)))
        return {};
    // 0x46a8c9: strncpy con strlen − [+0x8c0], sin signo. Con menos bytes que el pedazo, el largo da la vuelta y el
    // original escribe ceros fuera del buffer (Q-C518).
    if (queue.text.size() < queue.chunkBytes)
        return Core::Unexpected(std::string("0x46a850: el cartel tendría que sacar más bytes de los que le quedan "
                                            "(strncpy con un largo negativo, Q-C518)"));
    queue.text.erase(0, queue.chunkBytes);
    queue.scroll = F32(static_cast<double>(queue.chunkWidth) + static_cast<double>(queue.scroll));
    queue.chunkWidth = MeasureTheChunk(queue, measure);
    return {};
}

// 0x46a910: con el mouse sobre la franja frena de a 1 por dibujo, y si no acelera hasta la máxima. Las comparaciones
// son con la suma en la x87; la velocidad se guarda en float.
void Scroll(SlideQueue& queue, const SlideDrawContext& context)
{
    const bool hovered = MouseInRect(context.mouseX, context.mouseY, 0, queue.y - HoverAbove,
                                     context.sprites.Screen().width, queue.lineHeight + HoverExtra, HoverMode);
    queue.push = hovered ? -1.0f : 1.0f;
    const double sum = static_cast<double>(queue.velocity) + static_cast<double>(queue.push);
    if (sum >= static_cast<double>(queue.maxSpeed))
        queue.velocity = queue.maxSpeed;
    else
    {
        queue.velocity = F32(sum);
        if (sum < 0.0)
            queue.velocity = 0.0f;
    }
    queue.scroll = F32(static_cast<double>(queue.scroll) - static_cast<double>(queue.velocity));
}

// La opacidad de las franjas finas y del texto: hasta 180 resta 25, sin bajar de 0.
std::int32_t Faded(std::int32_t alpha)
{
    return alpha <= PlainBandAlpha ? std::max(alpha - BandFade, 0) : alpha;
}

Render::Color Black(std::int32_t alpha)
{
    return Render::Color{0.0f, 0.0f, 0.0f, static_cast<float>(alpha & 0xff) / 255.0f};
}

// 0x46a540(forzado): `forced` apaga la cola aunque tenga texto (la normal, mientras pasa la gruesa), y sigue
// dibujando y corriendo su texto mientras se apaga.
Core::Expected<void, std::string> DrawTheQueue(SlideBanner& banner, SlideQueue& queue, bool forced,
                                               const SlideDrawContext& context)
{
    if (!context.slidesOn)
        return {};
    bool goingOut = false;
    if (IsIdle(queue) || forced)
    {
        goingOut = true;
        if (queue.alpha > 0)
            queue.alpha -= AlphaStep;
        if (queue.alpha < 0)
            queue.alpha = 0;
    }
    else
    {
        if (queue.alpha < MaxAlpha)
            queue.alpha += AlphaStep;
        if (queue.alpha > MaxAlpha)
            queue.alpha = MaxAlpha;
    }
    if (queue.alpha <= 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    const App::PixelSize screen = sprites.Screen();
    // 0x46a5c3..0x46a6b4: 0x620a00(1), las dos rayas de 1 y la franja, con 0x621aa0 y el ancho W en píxeles como si
    // fuera de 640 × 480.
    sprites.SetBlend(Render::BlendMode::Alpha);
    const auto width = static_cast<float>(screen.width);
    sprites.SetColor(Black(Faded(queue.alpha)));
    sprites.ColorQuad(0.0f, static_cast<float>(queue.y - 3), width, 1.0f);
    sprites.ColorQuad(0.0f, static_cast<float>(queue.lineHeight + 2 + queue.y), width, 1.0f);
    sprites.SetColor(Black(std::max(queue.alpha - BandFade, 0)));
    sprites.ColorQuad(0.0f, static_cast<float>(queue.y - 2), width, static_cast<float>(queue.lineHeight + 4));
    // 0x459ec0: glColor4f(1, 1, 1, 1) y GL_TEXTURE_2D.
    sprites.SetColor(Render::Color{});
    sprites.EnableTexture();
    if (goingOut && !forced)
    {
        sprites.SetBlend(Render::BlendMode::Opaque);
        return {};
    }
    context.dcFont = queue.font;
    if (auto dropped = DropTheChunk(queue, context.text); !dropped.HasValue())
        return dropped;
    if (queue.lineHeight == 0)
    {
        // 0x46a74d: el alto de "Z" sobre ry, truncado, y la y sube eso (0x45a9d0).
        queue.lineHeight = Render::Text::DesignY(context.text.Measure(queue.font, "Z").height, screen);
        queue.y -= queue.lineHeight;
    }
    // 0x46a775..0x46a80a: el alfa del color por la opacidad (con 50 más) sobre 255, en la x87, y el byte de _ftol.
    const std::uint32_t colorAlpha = queue.color >> 24U;
    const double lifted = static_cast<double>(Faded(queue.alpha) + TextLift) * static_cast<double>(colorAlpha);
    auto textAlpha = static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(static_cast<std::int32_t>(lifted * static_cast<double>(InverseByte)) & 0xff));
    if (queue.blink == 1 && banner.drawn % BlinkCycle < BlinkDim)
        textAlpha >>= 1U;
    context.textColors =
        Render::Text::TextColors{.text = (queue.color & 0xffffffU) + (textAlpha << 24U), .background = 0};
    // 0x46a816..0x46a82b: 0x5542f0(_ftol(x + corrimiento), y, texto, 0, 6, 0).
    const auto x = static_cast<std::int32_t>(static_cast<double>(queue.x) + static_cast<double>(queue.scroll));
    context.text.DrawClippedAtLeft(x, queue.y, queue.text,
                                   Render::Text::TextStyle{.font = queue.font, .colors = context.textColors});
    sprites.SetBlend(Render::BlendMode::Opaque);
    Scroll(queue, context);
    ++banner.drawn;
    return {};
}
} // namespace

void LoadTheSlides(SlideBanner& banner, const Data::Text::SlideTable& table)
{
    // 0x46b3c4..0x46b3dc: con algún texto, 0x46b5b0 vacía las listas antes; los topes y las cuentas se pisan igual.
    banner.periodSeconds = table.periodSeconds;
    banner.speed = table.speed;
    for (std::size_t group = 0; group < SlideGroups; ++group)
    {
        banner.maxLevels[group] = table.groups[group].maxLevel;
        banner.counts[group] = table.groups[group].count;
        banner.texts[group] = table.groups[group].texts;
    }
}

void ResetTheSlideQueues(SlideBanner& banner, std::uint32_t now)
{
    ResetTheQueue(banner.normal, FontKind::Normal, now);
    ResetTheQueue(banner.bold, FontKind::Bold, now);
    banner.normal.x = 0;
    banner.normal.y = SlideTop;
    banner.bold.x = 0;
    banner.bold.y = SlideTop;
}

std::optional<std::string> PickASlide(SlideBanner& banner, std::uint16_t level, Render::Models::CrtRandom& random)
{
    // 0x46b689..0x46b69d: CMP con JLE, con signo.
    std::size_t group = 0;
    while (static_cast<std::int32_t>(level) > banner.maxLevels[group])
    {
        if (++group == SlideGroups)
            return std::nullopt;
    }
    const std::int32_t count = banner.counts[group];
    if (count == 0)
        return std::nullopt;
    // 0x46b6c7..0x46b6de: IDIV y la comparación sin signo con la lista. rand() no es negativo: el resto tampoco.
    const std::int32_t at = random.Next() % count;
    const std::vector<std::string>& texts = banner.texts[group];
    if (static_cast<std::uint32_t>(at) >= texts.size())
        return std::nullopt;
    return texts[static_cast<std::size_t>(at)];
}

void QueueTheSlide(SlideBanner& banner, std::int32_t repeats, std::uint16_t gap, std::string_view text,
                   std::int32_t kind, float speed, std::uint32_t color)
{
    // 0x46b72f..0x46b749: el texto, las repeticiones con signo y el tipo sin signo.
    if (text.empty() || repeats > MaxRepeats || kind < 0 || kind > LastKind)
        return;
    if (kind < FirstBoldKind)
        Enqueue(banner.normal, repeats, gap, text, kind - 1, speed, color);
    else
        Enqueue(banner.bold, repeats, gap, text, kind - (FirstBoldKind + 1), speed, color);
}

Core::Expected<void, std::string> StepTheSlides(SlideBanner& banner, const SlideStepContext& context)
{
    if (auto bold = StepTheQueue(banner.bold, context); !bold.HasValue())
        return bold;
    return StepTheQueue(banner.normal, context);
}

bool SlideQueueIsIdle(const SlideQueue& queue)
{
    return IsIdle(queue);
}

Core::Expected<void, std::string> DrawTheSlides(SlideBanner& banner, const SlideDrawContext& context)
{
    if (!IsIdle(banner.bold))
    {
        if (auto normal = DrawTheQueue(banner, banner.normal, true, context); !normal.HasValue())
            return normal;
    }
    else if (banner.bold.alpha <= 0)
    {
        if (auto normal = DrawTheQueue(banner, banner.normal, false, context); !normal.HasValue())
            return normal;
    }
    if (banner.normal.alpha > 0)
        return {};
    return DrawTheQueue(banner, banner.bold, false, context);
}
} // namespace Client::UI
