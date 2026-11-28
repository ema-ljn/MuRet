#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Ui/Widget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Ui
{
class PanelRegistry;
}

// The personal shops' signs over the heads (CL9 milestone 7, phase 6b; 06 § Lo social en pantalla, "Las tiendas
// personales", "Los carteles"): the HUD's child 13 (0x451c20) with the position (0x451d60 → 0x452110), the spread
// (0x451e40) and each sign (0x452a10, with its box 0x452d50 and its quad 0x452e50); what the add measures for it
// (0x452540: text 1104, 0x452f80's two lines and 0x4531b0's size), and what the chat bubbles read: the short title
// (0x4518b0) and the title's colors (0x4517c0, 0x451af0, 0x451b50 and 0x451bb0). Only 0x5bb8d0 draws rand().
namespace Client::Render::Frame
{
class ITextDrawer;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct GameProjection;
} // namespace Client::Scenes

namespace Client::Social
{
struct PersonalShopState;
struct ShopTitle;
} // namespace Client::Social

namespace Client::PersonalShop
{

// 0x770df9c: Text.bmd's text 1104, the head of the signs and of the bubble's title.
inline constexpr std::size_t ShopSignHeaderText = 1104;

// Text 1104, or the error with the address that reads it if there is no Text.bmd.
[[nodiscard]] Core::Expected<std::string_view, std::string> ShopSignHeader(const Tables::TextTable* texts,
                                                                           std::string_view address);

// +0x20 and +0x30 of the sign.
struct ShopTitleLines
{
    std::string first;
    std::string second;

    friend bool operator==(const ShopTitleLines&, const ShopTitleLines&) = default;
};

// 0x452f80(title, +0x20, +0x30): with 20 bytes or fewer, the whole title in the first and the second empty; with more,
// the first up to the first cut from the middle (length >> 1) that does not split a two-byte letter (a byte of 0x80 or
// more counts 2, with the one after it) and the second with the rest.
[[nodiscard]] ShopTitleLines SplitTheShopTitle(std::string_view title);

// 0x4518b0(character, output): without a sign, nothing (the output stays as it was: 0x556dd0's empty text). With one,
// the whole title if it has 14 bytes or fewer; otherwise, the first 12, or 13 if the 12th is the second half of a
// two-byte letter, and ".." (0x6c7970).
[[nodiscard]] std::string ShortShopTitle(const Social::PersonalShopState& shops, std::size_t index);
[[nodiscard]] std::string ShortShopTitle(std::string_view title);

// What 0x452540 leaves in the sign for the draw, with GetTextExtentPoint32A's measures.
struct ShopSignLayout
{
    // +0x58: the width of text 1104 in bold.
    std::int32_t header = 0;
    ShopTitleLines lines;
    // +0x50 and +0x54 (0x4531b0).
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// 0x452540 → 0x452f80 and 0x4531b0: the name (+0) in bold and the two lines in the normal font; the width is the
// largest of text 1104 plus the name and of the lines there are, and the height, the letter step times the rows (the
// name and one or two lines). The original measures it when placing the sign (0x4511f0); MuRet, on each draw: the fonts
// do not change in the game (*inferred*), so it is the same.
[[nodiscard]] ShopSignLayout MeasureTheShopSign(const Social::ShopTitle& sign, std::string_view header,
                                                const Render::Text::ITextMeasure& measure, int fontStep);

// +0x48..+0x54 of a sign, in pixels.
struct ShopSignBox
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;

    friend bool operator==(const ShopSignBox&, const ShopSignBox&) = default;
};

// 0x452110: the point 60 above the owner's top (+0x138 + z + 60, like the bubble) projected with 0x6205b0 to 640 × 480
// and turned into pixels with IMUL and the signed division; x, minus half the width, and y, minus 36 · H / 480.
[[nodiscard]] Core::Expected<Ui::Point, std::string> PlaceTheShopSign(const Scenes::GameProjection& projection,
                                                                      const Kernel::ActorState& owner,
                                                                      std::int32_t width, Core::PixelSize screen);

// 0x451e40, in the map's order: for each overlapping pair (signed comparisons), the first goes up to the other's top
// edge if its bottom edge is before the other's middle, and otherwise goes down to its bottom edge; x does not change.
// Then each against the edges, unsigned: x at 0 or, from W − width, at W − width − 4; y at 0 or, from H − height, at
// H − height.
void SpreadTheShopSigns(std::span<ShopSignBox> signs, Core::PixelSize screen);

// 0x452e50(position, size): a row's quad, in pixels. x at 0 and, if x + width passes W (unsigned), at W − width; y at
// 0 and, if y + height passes H − H · 47 / 480 (unsigned), at H − H · 47 / 480: without subtracting the height, and
// with 480 where 0x554000 has 640 (07 Q-C976).
[[nodiscard]] Ui::Point ShopSignRowAt(Ui::Point at, std::int32_t width, std::int32_t rows, Core::PixelSize screen);

// The colors of a sign (0x452a10, by the PK) and of a bubble's title (0x451af0, 0x451b50 and 0x451bb0, which also look
// at +0x370): the name and title's, text 1104's and the background.
struct ShopSignColors
{
    std::uint32_t name = 0;
    std::uint32_t header = 0;
    std::uint32_t background = 0;

    friend bool operator==(const ShopSignColors&, const ShopSignColors&) = default;
};

// 0x452a10: with PK 4, 5 or 6, text 1104 and the name in 0xff00b4e6, 0xff006ee6 or 0xff0014f0 and the background in
// 0x8029396c (0x80527ab6 with +0x60); with another, the name in 0xff00e6e6 (0xffffe6e6), text 1104 in 0xff0096fa
// (0xff29396c) and the background in 0x8029396c (0x800096fa).
[[nodiscard]] ShopSignColors ShopSignColorsOf(std::uint8_t pk, bool hovered);

// 0x451af0, 0x451b50 and 0x451bb0: like the sign with +0x370 at 2 or 8 taken as PK 6.
[[nodiscard]] ShopSignColors ShopBubbleColorsOf(const Kernel::ActorState& owner, bool hovered);

// 0x4517c0: the character's sign's +0x60; without a sign, false.
[[nodiscard]] bool ShopSignHovered(const Social::PersonalShopState& shops, std::size_t index);

struct ShopSignDrawContext
{
    Kernel::WorldState& world;
    // 0x5bb8d0 (with the 0x20 and 0x10000 queries and their rand()) and IsOpen(0x200).
    Ui::PanelRegistry& panels;
    // 0x6203c0 of this frame's world pass: 0x451d60 builds with 0x620db0 the same rectangle (0, 0, w, 432) with the
    // same camera, and 0x6205b0 reads the same (*inferred*). Without it, a sign stops.
    const Scenes::GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    // 0x452d50's SelectObject: the last drawn row's font stays.
    Render::Text::FontKind& dcFont;
    const Tables::TextTable* texts = nullptr;
    // A MuRet intermediate draw does not write +0x10, the +0x60 or the positions (+0x48 and +0x4c): they go to a copy.
    bool projecting = false;
};

// 0x451c20, the HUD's child 13: with the map empty (+0xc at 0), nothing. Otherwise, 0x451d60 (0x5bb8d0 with its
// rand(), and each sign's position), 0x451e40 (the spread position stays in each one's +0x48 and +0x4c) and the +0x10
// count (from 0 to 7). Each sign, in the map's order: with a chosen one (0x6c8e30), +0x10 below 3, the commands window
// (0x200) open and its command 2 (the shop), +0x60 at 1 if it is the chosen one's (the others keep it, 07 Q-C977);
// otherwise, +0x60 at 0. In view (+0x44) and with the map's +0x14 (which "/no ver puestos" turns off, 0x56d776),
// 0x452a10 with its PK: text 1104 and the name in bold in the first row and the title's lines below, in white on
// 0xc829396c with the normal font, each with its 0x452e50 quad.
[[nodiscard]] Core::Expected<void, std::string> DrawTheShopSigns(ShopSignDrawContext& context);
} // namespace Client::PersonalShop
