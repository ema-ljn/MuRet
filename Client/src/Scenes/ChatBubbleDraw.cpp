#include "Scenes/ChatBubbleDraw.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/GameProjection.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/ShopSigns.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::Text::FontKind;

// 0x5558e5: el nombre se mide con 3 más.
constexpr std::int32_t NameMargin = 3;
// 0x5559c5: las cinco medidas que dan TRUE (el nombre, los dos renglones, el gremio y el puesto).
constexpr std::int32_t MeasuredTexts = 5;
// 0x5a90a7: 60 (0x6b86cc) sobre el techo del dueño.
constexpr double AboveTheTop = 60.0;
// 0x5a90f2: y 0x24 más arriba.
constexpr std::int32_t RaisedBy = 0x24;
constexpr std::int32_t DesignWidth = 640;
constexpr std::int32_t DesignHeight = 480;
// 0x555b18: el dueño con este bit de +0x7c no tiene globo.
constexpr std::uint32_t HiddenBubbleFlag = 0x10000000;
// 0x555b4c: en el asedio, el bit de +0x78 con el que solo lo ve su bando.
constexpr std::uint32_t SiegeSideFlag = 0x4000;
// 0x556327: el paso de la letra no pasa de 32.
constexpr int LargestFontStep = 0x20;
// 0x55635x: las columnas del DIB, cinco cajas de 256 (0x6c7a84).
constexpr std::int32_t DibColumns = 5 * 256;
// 0x556600..0x556650: el color del nombre por +0x64.
constexpr std::array<std::uint32_t, 6> NameColors{0xfff0ff96u, 0xffff7864u, 0xffffb48cu,
                                                  0xffffdcc8u, 0xff3c96ffu, 0xff1e50ffu};
constexpr std::uint32_t OtherNameColor = 0xff0000ffu;
// 0x55666a y 0x556674: el globo propio.
constexpr std::uint32_t OwnBackground = 0x9600643cu;
constexpr std::uint32_t OwnPrefix = 0xff00ffc8u;
// 0x556690: el mismo gremio pasa el 1 a 0x513270 y a 0x5132a0.
constexpr std::uint8_t SameGuildRelation = 1;
// 0x5569ec..0x556a1f: el fondo de los renglones por +0x65.
constexpr std::uint32_t NoGuildBackground = 0x96321e0au;
constexpr std::uint32_t GuildBackground = 0x9600321eu;
constexpr std::uint32_t OtherGuildBackground = 0x96000032u;
constexpr std::uint8_t GuildByteOne = 1;
// Los renglones: con menos de 10 pasos, con la mitad del alfa.
constexpr std::uint32_t LineColor = 0xffc8dce6u;
constexpr std::uint32_t FadingLineColor = 0x80c8dce6u;
constexpr std::int32_t FadingSteps = 10;
// 0x5567d1..0x5567e1: bajo el mouse, el texto y el fondo se cambian 3 de cada 6 dibujos del área.
constexpr std::int32_t BlinkCycle = 6;
constexpr std::int32_t BlinkSwapped = 3;
// 0x555b5e..0x555bda: los bandos del asedio por +0x360.
struct SiegeSide
{
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};
constexpr std::array<SiegeSide, 4> SiegeSides{
    {{.first = 9, .second = 6}, {.first = 10, .second = 7}, {.first = 11, .second = 8}, {.first = 13, .second = 12}}};
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

std::int32_t Wrapped(std::uint32_t value)
{
    return std::bit_cast<std::int32_t>(value);
}

std::uint32_t Bits(std::int32_t value)
{
    return std::bit_cast<std::uint32_t>(value);
}

std::int32_t Add(std::int32_t a, std::int32_t b)
{
    return Wrapped(Bits(a) + Bits(b));
}

std::int32_t Subtract(std::int32_t a, std::int32_t b)
{
    return Wrapped(Bits(a) - Bits(b));
}

// lea, shl (o imul) en 32 bits y IDIV: un tamaño de la pantalla a 640 × 480, o al revés.
std::int32_t Scaled(std::int32_t value, std::int32_t factor, std::int32_t divisor)
{
    return Wrapped(Bits(value) * Bits(factor)) / divisor;
}

bool Alive(const ChatBubble& bubble)
{
    return bubble.nameTime > 0 || bubble.time1 > 0;
}

// 0x513270: el color de adelante (el gremio y el puesto) por +0x370 del dueño.
std::uint32_t PrefixColorOf(std::uint8_t relation)
{
    if (relation == 0)
        return 0xffffe6e6u;
    if (relation == 2 || relation == 8)
        return 0xff001effu;
    return 0xff00ffc8u;
}

// 0x5132a0: el fondo, igual.
std::uint32_t BackgroundOf(std::uint8_t relation)
{
    if (relation == 0)
        return 0x96321e0au;
    if (relation == 2 || relation == 8)
        return 0x96000000u;
    return 0x96503214u;
}

// 0x555b5e..0x555bda: con el héroe en un bando, el dueño de otro no tiene globo.
bool OfAnotherSiegeSide(std::uint8_t hero, std::uint8_t owner)
{
    for (const SiegeSide& side : SiegeSides)
    {
        if (hero == side.first || hero == side.second)
            return owner != side.first && owner != side.second;
    }
    return false;
}

std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

// 0x55627a..0x556cd2, el camino de TextOut distinto de 1 (06 § Texto): el globo en (x, y) de 640 × 480.
Core::Expected<void, std::string> DrawTheBubble(ChatBubbleDrawContext& context, const ChatBubble& bubble, int fontStep)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::span<const ActorState, ActorPoolSize> actors = context.world.Actors();
    if (!bubble.owner)
        return Core::Unexpected(std::string("0x555b0f: un globo sin dueño"));
    const std::size_t ownerSlot = *bubble.owner;
    const ActorState& owner = actors[ownerSlot];
    const ActorState& hero = actors[runtime.hero];
    if ((owner.flags7c & HiddenBubbleFlag) == HiddenBubbleFlag)
        return {};
    // 0x555b2a..0x555bda: 0x4294b0 lee el byte del asedio (0x7239ec).
    if (ownerSlot != runtime.hero && runtime.siegeActive && (owner.flags78 & SiegeSideFlag) == SiegeSideFlag &&
        OfAnotherSiegeSide(hero.byte360, owner.byte360))
        return {};
    // 0x55627a..0x556291: 0x620a00(1) y glColor3f(1, 1, 1).
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    context.sprites.SetColor(White);
    // 0x556297..0x5562ae. Las potencias de 2 del ancho y del alto (0x5562b7..0x5562da) van a 0x553e50 y a 0x554000, que
    // no las leen.
    std::int32_t boxWidth = bubble.width;
    context.boxWidth = bubble.width;
    context.boxHeight = bubble.height;
    context.clampBoxes = 1;
    const App::PixelSize screen = context.sprites.Screen();
    // 0x5562dc..0x55631f: a píxeles, con IMUL y la división por 640 y por 480 con signo.
    const std::int32_t x = Scaled(screen.width, bubble.x, DesignWidth);
    std::int32_t y = Scaled(screen.height, bubble.y, DesignHeight);
    const int rows = std::min(fontStep, LargestFontStep);
    // 0x556339..0x55639e: el borrado del DIB, fila por fila, deja el ancho en 1.280 como mucho.
    if (rows > 0 && DibColumns < boxWidth)
    {
        boxWidth = DibColumns;
        context.boxWidth = DibColumns;
    }
    const bool clampY = context.clampBoxes != 0;
    // 0x5563a4..0x5565ef: con el cartel del dueño (0x451810, no +0x54), el texto 1104 y el título corto en negrita,
    // dos renglones más arriba, y los demás, uno (Q-C978).
    if (HasShopTitle(runtime.social.personalShop, ownerSlot))
    {
        const auto header = ShopSignHeader(context.texts, "0x556421");
        if (!header.HasValue())
            return Core::Unexpected(header.Error());
        // 0x5563bc..0x556405: 0x6c8e5c, 0x6c8e60 y 0x6c8e64 con +0x60 del cartel (0x4517c0).
        const ShopSignColors colors =
            ShopBubbleColorsOf(owner, ShopSignHovered(runtime.social.personalShop, ownerSlot));
        const Render::Text::TextStyle style{.font = FontKind::Bold,
                                            .colors = {.text = colors.name, .background = colors.background}};
        context.dcFont = FontKind::Bold;
        y = Subtract(y, Wrapped(Bits(rows) * 2u));
        // 0x556417..0x556486: el texto 1104 como lo de adelante de 0x553e50, de su ancho, en (x, y).
        const std::int32_t headerWidth = context.text.Measure(FontKind::Bold, header.Value()).width;
        context.text.DrawBoxed(
            x, y, header.Value(), style,
            Render::Text::BoxShape{
                .width = headerWidth, .rows = rows, .prefixColumns = headerWidth, .prefixColor = colors.header},
            clampY);
        // 0x5564f6..0x556544: el título, a su derecha, con lo que queda del ancho (0x77f31b8 menos el del texto).
        context.text.DrawBoxed(Add(x, headerWidth), y, CString(bubble.shopTitle), style,
                               Render::Text::BoxShape{.width = Subtract(boxWidth, headerWidth),
                                                      .rows = rows,
                                                      .prefixColumns = 0,
                                                      .prefixColor = colors.header},
                               clampY);
        // 0x556549..0x556567.
        context.dcFont = FontKind::Normal;
        y = Add(y, rows);
    }
    std::uint32_t text = bubble.color < NameColors.size() ? NameColors[bubble.color] : OtherNameColor;
    std::uint32_t background = 0;
    std::uint32_t prefix = 0;
    if (ownerSlot == runtime.hero)
    {
        background = OwnBackground;
        prefix = OwnPrefix;
    }
    else
    {
        const std::uint8_t relation = owner.word1f2 == hero.word1f2 ? SameGuildRelation : owner.byte370;
        prefix = PrefixColorOf(relation);
        background = BackgroundOf(relation);
    }
    // 0x5566d7..0x5567f4: el mouse sobre el globo (con DIV sin signo), el campo del chat abierto (0x6c8e68), el héroe
    // en la zona segura y otro nombre que el suyo.
    const std::int32_t right = Add(Wrapped(Bits(bubble.width) * Bits(DesignWidth) / Bits(screen.width)), bubble.x);
    const std::int32_t bottom = Add(Wrapped(Bits(bubble.height) * Bits(DesignHeight) / Bits(screen.height)), bubble.y);
    if (bubble.x <= context.mouse.x && context.mouse.x < right && bubble.y <= context.mouse.y &&
        context.mouse.y < bottom && runtime.ui.chatOpen && hero.safeZone != 0 &&
        CString(bubble.name) != ChatNameOf(hero) && context.blink % BlinkCycle < BlinkSwapped)
    {
        std::swap(text, background);
    }
    const auto line = [&](std::string_view drawn, int prefixColumns)
    {
        context.text.DrawBoxed(
            x, y, drawn,
            Render::Text::TextStyle{.font = FontKind::Normal, .colors = {.text = text, .background = background}},
            Render::Text::BoxShape{
                .width = boxWidth, .rows = rows, .prefixColumns = prefixColumns, .prefixColor = prefix},
            clampY);
    };
    // 0x5567ff..0x5568cf y 0x5568cf..0x55699a: el gremio y el puesto, con su cx de adelante (0x553e50 los mide).
    for (const std::string* labeled : {&bubble.guild, &bubble.guildPost})
    {
        if (labeled->empty())
            continue;
        line(*labeled, context.text.Measure(FontKind::Normal, *labeled).width);
        y = Add(y, rows);
    }
    // 0x55699a..0x5569e9: el nombre.
    line(bubble.name, 0);
    y = Add(y, rows);
    background = bubble.guildByte == 0 ? NoGuildBackground
                                       : (bubble.guildByte == GuildByteOne ? GuildBackground : OtherGuildBackground);
    // 0x556a24..0x556cd2: el renglón de arriba, si tiene tiempo, y el de abajo.
    if (bubble.time2 >= 1)
    {
        text = bubble.time2 < FadingSteps ? FadingLineColor : LineColor;
        line(bubble.line2, 0);
        y = Add(y, rows);
    }
    if (bubble.time2 >= 1 || bubble.time1 >= 1)
    {
        text = bubble.time1 < FadingSteps ? FadingLineColor : LineColor;
        line(bubble.line1, 0);
    }
    context.textColors = Render::Text::TextColors{.text = text, .background = background};
    return {};
}
} // namespace

Core::Expected<void, std::string> MeasureTheChatBubble(ChatBubble& bubble, const Render::Text::ITextMeasure& measure,
                                                       int fontStep, const Data::Text::TextTable* texts)
{
    const auto width = [&](const std::string& text) { return measure.Measure(FontKind::Normal, text).width; };
    const std::int32_t name = Add(width(bubble.name), NameMargin);
    const std::int32_t line1 = width(bubble.line1);
    const std::int32_t line2 = width(bubble.line2);
    const std::int32_t guild = width(bubble.guild);
    const std::int32_t post = width(bubble.guildPost);
    // 0x5558df..0x5559a7: el mayor, con los renglones que tienen tiempo.
    std::int32_t widest = std::max({name, guild, post});
    if (bubble.time2 > 0)
        widest = std::max({widest, line1, line2});
    else if (bubble.time1 > 0)
        widest = std::max(widest, line1);
    bubble.width = widest;
    bubble.height = Wrapped(Bits(MeasuredTexts) * Bits(fontStep));
    // 0x5559c2..0x555a62: con lstrlen(+0x54) mayor que 0, la negrita; el título y el texto 1104 (0x770df9c), y la
    // normal al final, la que 0x5a9050 dejó.
    const std::string_view shopTitle = CString(bubble.shopTitle);
    if (shopTitle.empty())
        return {};
    const auto header = ShopSignHeader(texts, "0x555a0f");
    if (!header.HasValue())
        return Core::Unexpected(header.Error());
    const std::int32_t titled =
        Add(measure.Measure(FontKind::Bold, shopTitle).width, measure.Measure(FontKind::Bold, header.Value()).width);
    if (bubble.width < titled)
        bubble.width = titled;
    bubble.height = Add(bubble.height, fontStep);
    return {};
}

Core::Expected<void, std::string> DrawTheChatBubbles(ChatBubbleDrawContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::span<const ActorState, ActorPoolSize> actors = context.world.Actors();
    const App::PixelSize screen = context.sprites.Screen();
    const int fontStep = Render::Text::FontStepFor(screen.width);
    // Un dibujo intermedio escribe en una copia.
    std::optional<ChatBubbleTable> scratch;
    if (context.projecting)
        scratch.emplace(runtime.chat.bubbles);
    ChatBubbleTable& bubbles = scratch ? *scratch : runtime.chat.bubbles;

    // 0x5a9056..0x5a9065: SelectObject con la normal.
    context.dcFont = FontKind::Normal;
    // 0x5a906b..0x5a9107: dónde va cada uno y cuánto mide.
    for (ChatBubble& bubble : bubbles)
    {
        if (!Alive(bubble))
            continue;
        if (!bubble.owner)
            return Core::Unexpected(std::string("0x5a90b2: un globo sin dueño va donde dice +0x288, que escriben las "
                                                "tiendas personales (CL9.4)"));
        if (context.projection == nullptr)
            return Core::Unexpected(std::string("0x5a90be: el globo no tiene la proyección del pase del mundo"));
        // 0x5a90e2: el IDIV por W, que con 0 sería una excepción.
        if (screen.width <= 0 || screen.height <= 0)
            return Core::Unexpected(std::string("0x5a90e2: la pantalla no tiene tamaño"));
        const ActorState& owner = actors[*bubble.owner];
        // 0x5a9096..0x5a90ad: FLD, FADD y FADD en la x87, y FSTP en float.
        const glm::vec3 above{owner.position.x, owner.position.y,
                              static_cast<float>(static_cast<double>(owner.boundsTop) +
                                                 static_cast<double>(owner.position.z) + AboveTheTop)};
        const auto projected = ProjectGamePoint(*context.projection, above);
        if (!projected.HasValue())
            return Core::Unexpected(projected.Error());
        if (auto measured = MeasureTheChatBubble(bubble, context.text, fontStep, context.texts); !measured.HasValue())
            return measured;
        // 0x5a90d2..0x5a90f8: la mitad del ancho en 640 × 480, con IDIV y la mitad truncada hacia el 0.
        bubble.x = Subtract(projected.Value().x, Scaled(bubble.width, DesignWidth, screen.width) / 2);
        bubble.y = Subtract(projected.Value().y, RaisedBy);
    }
    // 0x5a910d..0x5a9210: cada par que se pisa corre la y del primero.
    for (std::size_t first = 0; first < bubbles.size(); ++first)
    {
        ChatBubble& moved = bubbles[first];
        if (!Alive(moved))
            continue;
        for (std::size_t second = 0; second < bubbles.size(); ++second)
        {
            const ChatBubble& other = bubbles[second];
            if (first == second || !Alive(other))
                continue;
            if (Add(Scaled(moved.width, DesignWidth, screen.width), moved.x) <= other.x)
                continue;
            if (moved.x >= Add(Scaled(other.width, DesignWidth, screen.width), other.x))
                continue;
            const std::int32_t movedHeight = Scaled(moved.height, DesignHeight, screen.height);
            if (Add(moved.y, movedHeight) <= other.y)
                continue;
            const std::int32_t otherHeight = Scaled(other.height, DesignHeight, screen.height);
            const std::int32_t otherBottom = Add(otherHeight, other.y);
            if (moved.y >= otherBottom)
                continue;
            moved.y = moved.y < Add(otherHeight / 2, other.y) ? Subtract(other.y, movedHeight) : otherBottom;
        }
    }
    // 0x5a9216..0x5a9285: contra los bordes, con la x y la y de 640 × 480 contra W y H menos el tamaño en píxeles y la
    // comparación sin signo (Q-C522), y el dibujo.
    for (ChatBubble& bubble : bubbles)
    {
        if (!Alive(bubble))
            continue;
        bubble.x = std::max(bubble.x, 0);
        const std::int32_t right = Subtract(screen.width, bubble.width);
        if (Bits(bubble.x) >= Bits(right))
            bubble.x = right;
        bubble.y = std::max(bubble.y, 0);
        const std::int32_t bottom = Subtract(screen.height, bubble.height);
        if (Bits(bubble.y) >= Bits(bottom))
            bubble.y = bottom;
        if (auto drawn = DrawTheBubble(context, bubble, fontStep); !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace Client::Scenes
