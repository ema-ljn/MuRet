#include "PersonalShop/ShopSigns.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameProjection.h"
#include "Render/Frame/ITextDrawer.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"

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
#include <utility>
#include <vector>

namespace Client::PersonalShop
{
namespace
{
using Render::Text::FontKind;

// 0x452f8f: with more than 20 bytes, the title goes in two lines.
constexpr std::size_t OneLineBytes = 0x14;
// 0x451916 and 0x45196b: with more than 14 bytes, the short title is 12 (or 13) and "..".
constexpr std::size_t ShortTitleBytes = 0xe;
constexpr std::size_t ShortTitleCut = 0xc;
// 0x6c7970.
constexpr std::string_view ShortTitleTail = "..";
// A byte of 0x80 or more starts a two-byte letter (TEST 0x80).
constexpr unsigned char DoubleByteFlag = 0x80;
// 0x452131: 60 (0x6b86cc) above the owner's top, like the bubble.
constexpr double AboveTheTop = 60.0;
constexpr std::int32_t DesignWidth = 640;
constexpr std::int32_t DesignHeight = 480;
// 0x452191..0x4521a3: lea esi·9 and shl 2, 36 rows of 480 higher.
constexpr std::int32_t RaisedRows = 36;
// 0x451f5f: against the right edge, 4 further in.
constexpr std::int32_t RightMargin = 4;
// 0x452eee..0x452ef4: lea esi·3, shl 4 and sub esi, H · 47 / 480 for the bar.
constexpr std::int32_t BarRows = 47;
// 0x452b12: the rows' box measures the sign's width plus 4.
constexpr std::int32_t BoxMargin = 4;
// 0x451c41..0x451c50: +0x10 counts from 0 to 7; 0x451c71, the chosen one blinks with 0, 1 and 2.
constexpr std::int32_t LastBlinkStep = 6;
constexpr std::int32_t BlinkShown = 3;
// 0x451c6c: 0x6c8e30, the chosen one; in MuRet, slot 2 of WorldRuntime::selectedTargets.
constexpr std::size_t ChosenTarget = 2;
// 0x451c90: command 2 of the commands window (+0x20 of [0x6e592c]), the shop.
constexpr std::int32_t ShopCommand = 2;
// 0x452a90..0x452afa: the colors' PK values.
constexpr std::uint8_t PkFour = 4;
constexpr std::uint8_t PkFive = 5;
constexpr std::uint8_t PkSix = 6;
// 0x451afc and 0x451b00: +0x370 at 2 or 8 counts as PK 6.
constexpr std::uint8_t RelationTwo = 2;
constexpr std::uint8_t RelationEight = 8;
// 0x452c0d: the title's lines, in white on this background.
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

// 32-bit IMUL and the signed division by the constant (the mul by the magic number, which truncates toward 0).
std::int32_t Scaled(std::int32_t value, std::int32_t factor, std::int32_t divisor)
{
    return Wrapped(Bits(value) * Bits(factor)) / divisor;
}

bool HighByte(char letter)
{
    return (static_cast<unsigned char>(letter) & DoubleByteFlag) != 0;
}

// The first cut from `from` that does not split a two-byte letter. Past the end, operator[] gives the "" of 0x6b85ec
// (0x45195b), which counts 1.
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

// What the draw writes of the map: the live one or, in an intermediate draw, a copy.
struct SignState
{
    std::map<std::size_t, Social::ShopTitle>& titles;
    std::int32_t& blink;
};

// 0x452d50 and 0x452e50 for one row: the box with the text at (0, 0), without leading columns, in the quad shifted by
// ShopSignRowAt. 0x452d50 paints the box of the sign's width plus 4 (0x452b12) and the quad shows its first `width`
// columns; MuRet paints only those, which are the visible ones (the others, of the same color in each column, stay
// outside).
void DrawTheRow(ShopSignDrawContext& context, Ui::Point at, std::int32_t width, int rows, std::string_view text,
                const Render::Text::TextStyle& style, Core::PixelSize screen)
{
    const Ui::Point row = ShopSignRowAt(at, width, rows, screen);
    context.text.DrawBoxed(row.x, row.y, text, style,
                           Render::Text::BoxShape{.width = width, .rows = rows, .prefixColumns = 0, .prefixColor = 0},
                           false);
    context.dcFont = style.font;
}

// 0x452a10(PK), with the sign in its place.
void DrawTheSign(ShopSignDrawContext& context, const Social::ShopTitle& sign, const ShopSignLayout& layout,
                 Ui::Point at, std::uint8_t pk, std::string_view header, int rows, Core::PixelSize screen)
{
    const ShopSignColors colors = ShopSignColorsOf(pk, sign.hovered != 0);
    const std::int32_t box = Add(layout.width, BoxMargin);
    // 0x452b28..0x452bb7: text 1104 in bold, of its width (+0x58).
    DrawTheRow(context, at, layout.header, rows, header,
               Render::Text::TextStyle{.font = FontKind::Bold,
                                       .colors = {.text = colors.header, .background = colors.background}},
               screen);
    // 0x452bbc..0x452bfc: the name in bold, to its right, with what is left of the box.
    DrawTheRow(context, Ui::Point{.x = Add(at.x, layout.header), .y = at.y}, Subtract(box, layout.header), rows,
               sign.name,
               Render::Text::TextStyle{.font = FontKind::Bold,
                                       .colors = {.text = colors.name, .background = colors.background}},
               screen);
    // 0x452c01..0x452cbb: with the second line, both; otherwise, the first if there is one; in the normal font.
    const Render::Text::TextStyle lineStyle{.font = FontKind::Normal,
                                            .colors = {.text = LineText, .background = LineBackground}};
    if (!layout.lines.second.empty())
    {
        DrawTheRow(context, Ui::Point{.x = at.x, .y = Add(at.y, rows)}, box, rows, layout.lines.first, lineStyle,
                   screen);
        DrawTheRow(context, Ui::Point{.x = at.x, .y = Add(at.y, 2 * rows)}, box, rows, layout.lines.second, lineStyle,
                   screen);
    }
    else if (!layout.lines.first.empty())
    {
        DrawTheRow(context, Ui::Point{.x = at.x, .y = Add(at.y, rows)}, box, rows, layout.lines.first, lineStyle,
                   screen);
    }
}
} // namespace

Core::Expected<std::string_view, std::string> ShopSignHeader(const Tables::TextTable* texts, std::string_view address)
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
    // 0x453142..0x45319b: the whole title and the second line at "" (0x6e57a4).
    if (title.size() <= OneLineBytes)
        return ShopTitleLines{.first = std::string(title), .second = {}};
    // 0x452f98..0x452fc3: from the middle; 0x452fc6..0x45307a, the first up to the cut; 0x45307e.., the rest.
    const std::size_t cut = std::min(CutAfter(title, title.size() >> 1), title.size());
    return ShopTitleLines{.first = std::string(title.substr(0, cut)), .second = std::string(title.substr(cut))};
}

std::string ShortShopTitle(std::string_view title)
{
    // 0x451a8c..0x451a9c: with 14 bytes or fewer, whole (0x4532f0).
    if (title.size() <= ShortTitleBytes)
        return std::string(title);
    // 0x451924..0x45196e: up to 12 or more, one letter at a time; 0x451970..0x451a85, that piece and "..".
    const std::size_t cut = std::min(CutAfter(title, ShortTitleCut), title.size());
    std::string shortTitle(title.substr(0, cut));
    shortTitle += ShortTitleTail;
    return shortTitle;
}

std::string ShortShopTitle(const Social::PersonalShopState& shops, std::size_t index)
{
    // 0x4518d8..0x4518e5: 0x453b60 looks for the character; without it, the output does not change.
    const auto found = shops.titles.find(index);
    if (found == shops.titles.end())
        return {};
    return ShortShopTitle(found->second.title);
}

ShopSignLayout MeasureTheShopSign(const Social::ShopTitle& sign, std::string_view header,
                                  const Render::Text::ITextMeasure& measure, int fontStep)
{
    ShopSignLayout layout;
    // 0x452760..0x452793: text 1104 in bold.
    layout.header = Width(measure, FontKind::Bold, header);
    // 0x4527a7: the two lines.
    layout.lines = SplitTheShopTitle(sign.title);
    // 0x4531b6..0x453245: the name in bold and the two lines in the normal font.
    const std::int32_t name = Add(layout.header, Width(measure, FontKind::Bold, sign.name));
    const std::int32_t first = Width(measure, FontKind::Normal, layout.lines.first);
    const std::int32_t second = Width(measure, FontKind::Normal, layout.lines.second);
    if (!layout.lines.second.empty())
    {
        // 0x45324e..0x453287: the largest, in three rows.
        layout.width = std::max(name, std::max(first, second));
        layout.height = Wrapped(Bits(3) * Bits(fontStep));
    }
    else if (!layout.lines.first.empty())
    {
        // 0x453295..0x4532b8: in two.
        layout.width = std::max(name, first);
        layout.height = Wrapped(Bits(2) * Bits(fontStep));
    }
    else
    {
        // 0x4532c2..0x4532da: the name alone.
        layout.width = name;
        layout.height = fontStep;
    }
    return layout;
}

Core::Expected<Ui::Point, std::string> PlaceTheShopSign(const Kernel::GameProjection& projection,
                                                        const Kernel::ActorState& owner, std::int32_t width,
                                                        Core::PixelSize screen)
{
    // 0x45211c..0x452143: FLD, FADD and FADD in the x87, and FSTP to float.
    const glm::vec3 above{
        owner.position.x, owner.position.y,
        static_cast<float>(static_cast<double>(owner.boundsTop) + static_cast<double>(owner.position.z) + AboveTheTop)};
    const auto projected = Kernel::ProjectGamePoint(projection, above);
    if (!projected.HasValue())
        return Core::Unexpected(projected.Error());
    // 0x45214b..0x45217f: x · W / 640 minus half the width (cdq, sub and sar, toward 0).
    const std::int32_t x = Subtract(Scaled(screen.width, projected.Value().x, DesignWidth), width / 2);
    // 0x452181..0x4521b8: y · H / 480 minus 36 · H / 480.
    const std::int32_t y = Subtract(Scaled(screen.height, projected.Value().y, DesignHeight),
                                    Scaled(screen.height, RaisedRows, DesignHeight));
    return Ui::Point{.x = x, .y = y};
}

void SpreadTheShopSigns(std::span<ShopSignBox> signs, Core::PixelSize screen)
{
    // 0x451e5e..0x451f13: each distinct pair (a, b), with a's rectangle read again after moving it.
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
            // 0x451eb7..0x451ed8: the other's middle with cdq, sub and sar; up, y − bottom + the other's top.
            const std::int32_t middle = Add(otherBottom, other.y) / 2;
            moved.y = movedBottom < middle ? Add(Subtract(moved.y, movedBottom), other.y) : otherBottom;
        }
    }
    // 0x451f1a..0x451fce: against the edges, with unsigned comparisons.
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

Ui::Point ShopSignRowAt(Ui::Point at, std::int32_t width, std::int32_t rows, Core::PixelSize screen)
{
    // 0x452eaa..0x452ed7.
    if (at.x < 0)
        at.x = 0;
    if (Bits(Add(width, at.x)) > Bits(screen.width))
        at.x = Subtract(screen.width, width);
    // 0x452eda..0x452f12: the cap is the bar's line, and y stays on it.
    if (at.y < 0)
        at.y = 0;
    const std::int32_t limit = Subtract(screen.height, Scaled(screen.height, BarRows, DesignHeight));
    if (Bits(Add(rows, at.y)) > Bits(limit))
        at.y = limit;
    return at;
}

ShopSignColors ShopSignColorsOf(std::uint8_t pk, bool hovered)
{
    // 0x452a2f..0x452a8a: neg, sbb, and, add with +0x60.
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
    // 0x452a9b..0x452b02: both texts in the same color and the background of the three cases.
    return ShopSignColors{.name = text, .header = text, .background = hovered ? 0x80527ab6u : 0x8029396cu};
}

ShopSignColors ShopBubbleColorsOf(const Kernel::ActorState& owner, bool hovered)
{
    const bool enemy = owner.byte370 == RelationTwo || owner.byte370 == RelationEight;
    return ShopSignColorsOf(enemy ? PkSix : owner.pk, hovered);
}

bool ShopSignHovered(const Social::PersonalShopState& shops, std::size_t index)
{
    // 0x4517cd..0x451806: lower_bound of the character; with its own, +0x60.
    const auto found = shops.titles.find(index);
    return found != shops.titles.end() && found->second.hovered != 0;
}

Core::Expected<void, std::string> DrawTheShopSigns(ShopSignDrawContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    // 0x451c27..0x451c2c
    if (shops.titles.empty())
        return {};
    // An intermediate draw writes +0x10, the +0x60 and the positions to a copy.
    std::optional<std::map<std::size_t, Social::ShopTitle>> scratchTitles;
    std::int32_t scratchBlink = shops.signBlink;
    if (context.projecting)
        scratchTitles.emplace(shops.titles);
    SignState state{.titles = scratchTitles ? *scratchTitles : shops.titles,
                    .blink = context.projecting ? scratchBlink : shops.signBlink};

    // 0x451d6a: 0x5bb8d0, the world's width, with its rand(); the rectangle is the world pass's.
    static_cast<void>(context.panels.WorldWidth());
    const Core::PixelSize screen = context.sprites.Screen();
    const int rows = Render::Text::FontStepFor(screen.width);
    const auto header = ShopSignHeader(context.texts, "0x452776");
    if (!header.HasValue())
        return Core::Unexpected(header.Error());
    const std::span<const Kernel::ActorState, Kernel::ActorPoolSize> actors = context.world.Actors();

    // 0x451daf..0x451e23: each one's position (0x452110), in the map's order.
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
        // 0x451de3 and 0x451e40: each one's position, already spread and against the edges, stays in +0x48 and +0x4c
        // (0x4527d0).
        sign.x = boxes[at].x;
        sign.y = boxes[at].y;
        // 0x451c67..0x451cc0: IsOpen(0x200) no saca rand().
        if (chosen != -1 && state.blink < BlinkShown && context.panels.IsOpen(Ui::CommandsPanel) &&
            runtime.panels.delegated.commandsWindow.chosen == ShopCommand)
        {
            if (static_cast<std::int64_t>(index) == chosen)
                sign.hovered = 1;
        }
        else
        {
            sign.hovered = 0;
        }
        // 0x451cc5..0x451cec: in view and with the signs on.
        if (sign.visible && shops.signsShown != 0)
        {
            const ShopSignBox& box = boxes[at];
            DrawTheSign(context, sign, layouts[at], Ui::Point{.x = box.x, .y = box.y}, actors[index].pk, header.Value(),
                        rows, screen);
        }
        ++at;
    }
    return {};
}
} // namespace Client::PersonalShop
