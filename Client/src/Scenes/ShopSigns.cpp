#include "Scenes/ShopSigns.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameProjection.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Scenes
{
namespace
{
using Render::Text::FontKind;

// 0x452f8f: con más de 20 bytes, el título va en dos líneas.
constexpr std::size_t OneLineBytes = 0x14;
// 0x451916 y 0x45196b: con más de 14 bytes, el título corto es de 12 (o 13) y "..".
constexpr std::size_t ShortTitleBytes = 0xe;
constexpr std::size_t ShortTitleCut = 0xc;
// 0x6c7970.
constexpr std::string_view ShortTitleTail = "..";
// Un byte de 0x80 o más empieza una letra de dos bytes (TEST 0x80).
constexpr unsigned char DoubleByteFlag = 0x80;
// 0x452131: 60 (0x6b86cc) sobre el techo del dueño, como el globo.
constexpr double AboveTheTop = 60.0;
constexpr std::int32_t DesignWidth = 640;
constexpr std::int32_t DesignHeight = 480;
// 0x452191..0x4521a3: lea esi·9 y shl 2, 36 filas de 480 más arriba.
constexpr std::int32_t RaisedRows = 36;
// 0x451f5f: contra el borde derecho, 4 más adentro.
constexpr std::int32_t RightMargin = 4;
// 0x452eee..0x452ef4: lea esi·3, shl 4 y sub esi, H · 47 / 480 para la barra.
constexpr std::int32_t BarRows = 47;
// 0x452b12: la caja de los renglones mide el ancho del cartel más 4.
constexpr std::int32_t BoxMargin = 4;
// 0x451c41..0x451c50: +0x10 cuenta de 0 a 7; 0x451c71, el elegido parpadea con 0, 1 y 2.
constexpr std::int32_t LastBlinkStep = 6;
constexpr std::int32_t BlinkShown = 3;
// 0x451c6c: 0x6c8e30, el elegido; en MuRet, el lugar 2 de WorldRuntime::selectedTargets.
constexpr std::size_t ChosenTarget = 2;
// 0x451c90: el comando 2 de la ventana de los comandos (+0x20 de [0x6e592c]), la tienda.
constexpr std::int32_t ShopCommand = 2;
// 0x452a90..0x452afa: el PK de los colores.
constexpr std::uint8_t PkFour = 4;
constexpr std::uint8_t PkFive = 5;
constexpr std::uint8_t PkSix = 6;
// 0x451afc y 0x451b00: +0x370 en 2 u 8 va como el PK 6.
constexpr std::uint8_t RelationTwo = 2;
constexpr std::uint8_t RelationEight = 8;
// 0x452c0d: las líneas del título, en blanco sobre este fondo.
constexpr std::uint32_t LineText = 0xffffffffu;
constexpr std::uint32_t LineBackground = 0xc829396cu;

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

// IMUL en 32 bits y la división con signo por la constante (el mul por el número mágico, que trunca hacia el 0).
std::int32_t Scaled(std::int32_t value, std::int32_t factor, std::int32_t divisor)
{
    return Wrapped(Bits(value) * Bits(factor)) / divisor;
}

bool HighByte(char letter)
{
    return (static_cast<unsigned char>(letter) & DoubleByteFlag) != 0;
}

// El primer corte desde `from` que no parte una letra de dos bytes. Pasado el final, operator[] da el "" de 0x6b85ec
// (0x45195b), que cuenta 1.
std::size_t CutAfter(std::string_view text, std::size_t from)
{
    std::size_t at = 0;
    while (at < from)
        at += at < text.size() && HighByte(text[at]) ? std::size_t{2} : std::size_t{1};
    return at;
}

std::int32_t Width(const Render::Text::ITextMeasure& measure, FontKind font, std::string_view text)
{
    return measure.Measure(font, text).width;
}

// Lo que el dibujo escribe del mapa: el vivo o, en un dibujo intermedio, una copia.
struct SignState
{
    std::map<std::size_t, ShopTitle>& titles;
    std::int32_t& blink;
};

// 0x452d50 y 0x452e50 de un renglón: la caja con el texto en (0, 0), sin columnas de adelante, en el quad corrido por
// ShopSignRowAt. 0x452d50 pinta la caja del ancho del cartel más 4 (0x452b12) y el quad muestra sus primeras `width`
// columnas; MuRet pinta solo esas, que son las que se ven (las otras, del mismo color en cada columna, quedan fuera).
void DrawTheRow(ShopSignDrawContext& context, UI::Point at, std::int32_t width, int rows, std::string_view text,
                const Render::Text::TextStyle& style, App::PixelSize screen)
{
    const UI::Point row = ShopSignRowAt(at, width, rows, screen);
    context.text.DrawBoxed(row.x, row.y, text, style,
                           Render::Text::BoxShape{.width = width, .rows = rows, .prefixColumns = 0, .prefixColor = 0},
                           false);
    context.dcFont = style.font;
}

// 0x452a10(PK), con el cartel en su lugar.
void DrawTheSign(ShopSignDrawContext& context, const ShopTitle& sign, const ShopSignLayout& layout, UI::Point at,
                 std::uint8_t pk, std::string_view header, int rows, App::PixelSize screen)
{
    const ShopSignColors colors = ShopSignColorsOf(pk, sign.hovered != 0);
    const std::int32_t box = Add(layout.width, BoxMargin);
    // 0x452b28..0x452bb7: el texto 1104 en negrita, de su ancho (+0x58).
    DrawTheRow(context, at, layout.header, rows, header,
               Render::Text::TextStyle{.font = FontKind::Bold,
                                       .colors = {.text = colors.header, .background = colors.background}},
               screen);
    // 0x452bbc..0x452bfc: el nombre en negrita, a su derecha, con lo que queda de la caja.
    DrawTheRow(context, UI::Point{.x = Add(at.x, layout.header), .y = at.y}, Subtract(box, layout.header), rows,
               sign.name,
               Render::Text::TextStyle{.font = FontKind::Bold,
                                       .colors = {.text = colors.name, .background = colors.background}},
               screen);
    // 0x452c01..0x452cbb: con la segunda línea, las dos; si no, la primera si tiene; en la normal.
    const Render::Text::TextStyle lineStyle{.font = FontKind::Normal,
                                            .colors = {.text = LineText, .background = LineBackground}};
    if (!layout.lines.second.empty())
    {
        DrawTheRow(context, UI::Point{.x = at.x, .y = Add(at.y, rows)}, box, rows, layout.lines.first, lineStyle,
                   screen);
        DrawTheRow(context, UI::Point{.x = at.x, .y = Add(at.y, 2 * rows)}, box, rows, layout.lines.second, lineStyle,
                   screen);
    }
    else if (!layout.lines.first.empty())
    {
        DrawTheRow(context, UI::Point{.x = at.x, .y = Add(at.y, rows)}, box, rows, layout.lines.first, lineStyle,
                   screen);
    }
}
} // namespace

Core::Expected<std::string_view, std::string> ShopSignHeader(const Data::Text::TextTable* texts,
                                                             std::string_view address)
{
    const std::optional<std::string_view> header =
        texts != nullptr ? texts->Text(ShopSignHeaderText) : std::optional<std::string_view>{};
    if (!header)
        return Core::Unexpected(std::format("{}: el texto {} de Text.bmd (la cabeza de las tiendas personales) no está",
                                            address, ShopSignHeaderText));
    return *header;
}

ShopTitleLines SplitTheShopTitle(std::string_view title)
{
    // 0x453142..0x45319b: el título entero y la segunda línea en "" (0x6e57a4).
    if (title.size() <= OneLineBytes)
        return ShopTitleLines{.first = std::string(title), .second = {}};
    // 0x452f98..0x452fc3: desde la mitad; 0x452fc6..0x45307a, la primera hasta el corte; 0x45307e.., el resto.
    const std::size_t cut = std::min(CutAfter(title, title.size() >> 1), title.size());
    return ShopTitleLines{.first = std::string(title.substr(0, cut)), .second = std::string(title.substr(cut))};
}

std::string ShortShopTitle(std::string_view title)
{
    // 0x451a8c..0x451a9c: con 14 bytes o menos, entero (0x4532f0).
    if (title.size() <= ShortTitleBytes)
        return std::string(title);
    // 0x451924..0x45196e: hasta 12 o más, de a una letra; 0x451970..0x451a85, ese pedazo y "..".
    const std::size_t cut = std::min(CutAfter(title, ShortTitleCut), title.size());
    std::string shortTitle(title.substr(0, cut));
    shortTitle += ShortTitleTail;
    return shortTitle;
}

std::string ShortShopTitle(const PersonalShopState& shops, std::size_t index)
{
    // 0x4518d8..0x4518e5: 0x453b60 busca el personaje; sin él, la salida no cambia.
    const auto found = shops.titles.find(index);
    if (found == shops.titles.end())
        return {};
    return ShortShopTitle(found->second.title);
}

ShopSignLayout MeasureTheShopSign(const ShopTitle& sign, std::string_view header,
                                  const Render::Text::ITextMeasure& measure, int fontStep)
{
    ShopSignLayout layout;
    // 0x452760..0x452793: el texto 1104 con la negrita.
    layout.header = Width(measure, FontKind::Bold, header);
    // 0x4527a7: las dos líneas.
    layout.lines = SplitTheShopTitle(sign.title);
    // 0x4531b6..0x453245: el nombre con la negrita y las dos líneas con la normal.
    const std::int32_t name = Add(layout.header, Width(measure, FontKind::Bold, sign.name));
    const std::int32_t first = Width(measure, FontKind::Normal, layout.lines.first);
    const std::int32_t second = Width(measure, FontKind::Normal, layout.lines.second);
    if (!layout.lines.second.empty())
    {
        // 0x45324e..0x453287: el mayor, en tres renglones.
        layout.width = std::max(name, std::max(first, second));
        layout.height = Wrapped(Bits(3) * Bits(fontStep));
    }
    else if (!layout.lines.first.empty())
    {
        // 0x453295..0x4532b8: en dos.
        layout.width = std::max(name, first);
        layout.height = Wrapped(Bits(2) * Bits(fontStep));
    }
    else
    {
        // 0x4532c2..0x4532da: el nombre solo.
        layout.width = name;
        layout.height = fontStep;
    }
    return layout;
}

Core::Expected<UI::Point, std::string> PlaceTheShopSign(const GameProjection& projection, const ActorState& owner,
                                                        std::int32_t width, App::PixelSize screen)
{
    // 0x45211c..0x452143: FLD, FADD y FADD en la x87, y FSTP en float.
    const glm::vec3 above{
        owner.position.x, owner.position.y,
        static_cast<float>(static_cast<double>(owner.boundsTop) + static_cast<double>(owner.position.z) + AboveTheTop)};
    const auto projected = ProjectGamePoint(projection, above);
    if (!projected.HasValue())
        return Core::Unexpected(projected.Error());
    // 0x45214b..0x45217f: x · W / 640 menos la mitad del ancho (cdq, sub y sar, hacia el 0).
    const std::int32_t x = Subtract(Scaled(screen.width, projected.Value().x, DesignWidth), width / 2);
    // 0x452181..0x4521b8: y · H / 480 menos 36 · H / 480.
    const std::int32_t y = Subtract(Scaled(screen.height, projected.Value().y, DesignHeight),
                                    Scaled(screen.height, RaisedRows, DesignHeight));
    return UI::Point{.x = x, .y = y};
}

void SpreadTheShopSigns(std::span<ShopSignBox> signs, App::PixelSize screen)
{
    // 0x451e5e..0x451f13: cada par (a, b) distinto, con el rectángulo de a vuelto a leer después de moverlo.
    for (std::size_t a = 0; a < signs.size(); ++a)
    {
        for (std::size_t b = 0; b < signs.size(); ++b)
        {
            if (a == b)
                continue;
            ShopSignBox& moved = signs[a];
            const ShopSignBox& other = signs[b];
            const std::int32_t movedRight = Add(moved.x, moved.width);
            const std::int32_t movedBottom = Add(moved.y, moved.height);
            const std::int32_t otherRight = Add(other.x, other.width);
            const std::int32_t otherBottom = Add(other.y, other.height);
            if (movedRight <= other.x || moved.x >= otherRight || movedBottom <= other.y || moved.y >= otherBottom)
                continue;
            // 0x451eb7..0x451ed8: la mitad del otro con cdq, sub y sar; arriba, y − abajo + arriba del otro.
            const std::int32_t middle = Add(otherBottom, other.y) / 2;
            moved.y = movedBottom < middle ? Add(Subtract(moved.y, movedBottom), other.y) : otherBottom;
        }
    }
    // 0x451f1a..0x451fce: contra los bordes, con las comparaciones sin signo.
    for (ShopSignBox& sign : signs)
    {
        if (sign.x < 0)
            sign.x = 0;
        const std::int32_t right = Subtract(screen.width, sign.width);
        if (Bits(sign.x) >= Bits(right))
            sign.x = Subtract(right, RightMargin);
        if (sign.y < 0)
            sign.y = 0;
        const std::int32_t bottom = Subtract(screen.height, sign.height);
        if (Bits(sign.y) >= Bits(bottom))
            sign.y = bottom;
    }
}

UI::Point ShopSignRowAt(UI::Point at, std::int32_t width, std::int32_t rows, App::PixelSize screen)
{
    // 0x452eaa..0x452ed7.
    if (at.x < 0)
        at.x = 0;
    if (Bits(Add(width, at.x)) > Bits(screen.width))
        at.x = Subtract(screen.width, width);
    // 0x452eda..0x452f12: el tope es la línea de la barra, y la y queda en ella.
    if (at.y < 0)
        at.y = 0;
    const std::int32_t limit = Subtract(screen.height, Scaled(screen.height, BarRows, DesignHeight));
    if (Bits(Add(rows, at.y)) > Bits(limit))
        at.y = limit;
    return at;
}

ShopSignColors ShopSignColorsOf(std::uint8_t pk, bool hovered)
{
    // 0x452a2f..0x452a8a: neg, sbb, and y add con +0x60.
    const std::uint32_t background = hovered ? 0x800096fau : 0x8029396cu;
    ShopSignColors colors{.name = hovered ? 0xffffe6e6u : 0xff00e6e6u,
                          .header = hovered ? 0xff29396cu : 0xff0096fau,
                          .background = background};
    std::uint32_t text = 0;
    switch (pk)
    {
    case PkFour:
        text = 0xff00b4e6u;
        break;
    case PkFive:
        text = 0xff006ee6u;
        break;
    case PkSix:
        text = 0xff0014f0u;
        break;
    default:
        return colors;
    }
    // 0x452a9b..0x452b02: los dos textos del mismo color y el fondo de los tres casos.
    return ShopSignColors{.name = text, .header = text, .background = hovered ? 0x80527ab6u : 0x8029396cu};
}

ShopSignColors ShopBubbleColorsOf(const ActorState& owner, bool hovered)
{
    const bool enemy = owner.byte370 == RelationTwo || owner.byte370 == RelationEight;
    return ShopSignColorsOf(enemy ? PkSix : owner.pk, hovered);
}

bool ShopSignHovered(const PersonalShopState& shops, std::size_t index)
{
    // 0x4517cd..0x451806: lower_bound del personaje; con el suyo, +0x60.
    const auto found = shops.titles.find(index);
    return found != shops.titles.end() && found->second.hovered != 0;
}

Core::Expected<void, std::string> DrawTheShopSigns(ShopSignDrawContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    // 0x451c27..0x451c2c: el mapa vacío.
    if (shops.titles.empty())
        return {};
    // Un dibujo intermedio escribe +0x10 y los +0x60 en una copia.
    std::optional<std::map<std::size_t, ShopTitle>> scratchTitles;
    std::int32_t scratchBlink = shops.signBlink;
    if (context.projecting)
        scratchTitles.emplace(shops.titles);
    SignState state{.titles = scratchTitles ? *scratchTitles : shops.titles,
                    .blink = context.projecting ? scratchBlink : shops.signBlink};

    // 0x451d6a: 0x5bb8d0, el ancho del mundo, con sus rand(); el rectángulo es el del pase del mundo.
    static_cast<void>(context.panels.WorldWidth());
    const App::PixelSize screen = context.sprites.Screen();
    const int rows = Render::Text::FontStepFor(screen.width);
    const auto header = ShopSignHeader(context.texts, "0x452776");
    if (!header.HasValue())
        return Core::Unexpected(header.Error());
    const std::span<const ActorState, ActorPoolSize> actors = context.world.Actors();

    // 0x451daf..0x451e23: el lugar de cada uno (0x452110), en el orden del mapa.
    std::vector<ShopSignLayout> layouts;
    std::vector<ShopSignBox> boxes;
    layouts.reserve(state.titles.size());
    boxes.reserve(state.titles.size());
    for (const auto& [index, sign] : state.titles)
    {
        if (context.projection == nullptr)
            return Core::Unexpected(std::string("0x452146: el cartel no tiene la proyección del pase del mundo"));
        if (screen.width <= 0 || screen.height <= 0)
            return Core::Unexpected(std::string("0x452146: la pantalla no tiene tamaño"));
        ShopSignLayout layout = MeasureTheShopSign(sign, header.Value(), context.text, rows);
        const auto placed = PlaceTheShopSign(*context.projection, actors[index], layout.width, screen);
        if (!placed.HasValue())
            return Core::Unexpected(placed.Error());
        boxes.push_back(
            ShopSignBox{.x = placed.Value().x, .y = placed.Value().y, .width = layout.width, .height = layout.height});
        layouts.push_back(std::move(layout));
    }
    // 0x451c39: 0x451e40.
    SpreadTheShopSigns(boxes, screen);
    // 0x451c3e..0x451c50.
    state.blink = state.blink > LastBlinkStep ? 0 : state.blink + 1;

    const std::int32_t chosen = runtime.selectedTargets[ChosenTarget];
    std::size_t at = 0;
    for (auto& [index, sign] : state.titles)
    {
        // 0x451c67..0x451cc0: IsOpen(0x200) no saca rand().
        if (chosen != -1 && state.blink < BlinkShown && context.panels.IsOpen(UI::CommandsPanel) &&
            runtime.panels.delegated.commandsWindow.chosen == ShopCommand)
        {
            if (static_cast<std::int64_t>(index) == chosen)
                sign.hovered = 1;
        }
        else
        {
            sign.hovered = 0;
        }
        // 0x451cc5..0x451cec: a la vista y con los carteles prendidos.
        if (sign.visible && shops.signsShown != 0)
        {
            const ShopSignBox& box = boxes[at];
            DrawTheSign(context, sign, layouts[at], UI::Point{.x = box.x, .y = box.y}, actors[index].pk, header.Value(),
                        rows, screen);
        }
        ++at;
    }
    return {};
}
} // namespace Client::Scenes
