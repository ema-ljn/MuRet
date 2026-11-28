#include "Ui/OldWindowDraw.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/OriginalQuad.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/Frame/ITextDrawer.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatLogWindow.h"
#include "Chat/ChatInput.h"
#include "Messenger/FriendWindowsDraw.h"
#include "Messenger/LetterWindowsDraw.h"
#include "Ui/OldControls.h"
#include "Ui/OldEditLines.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindows.h"
#include "Ui/TextField.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Ui
{
namespace
{
using OldDraw::DrawText;
using OldDraw::DrawTheButton;
using OldDraw::Gray;
using OldDraw::Quad;
using OldDraw::RatioY;
using OldDraw::SetThePalette;
using OldDraw::ToFloat;
using OldDraw::Truncate;
using OldDraw::WhiteWithTexture;

// 0x459f00
constexpr std::int32_t TopLeftRect = 1;
// 0x491960: the 19 (r, g, b) colors of glColor4ub; the 3 and the 7 are black with their alpha, and the 3 takes it
// from alpha · 179 (0x6b9f94) instead of alpha · 255 (0x6b9f98).
struct PaletteEntry
{
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};
constexpr std::array<PaletteEntry, 19> Palette{{
    {146, 134, 121}, // 0, the outer edge of the border
    {37, 37, 37},    // 1, the middle of the border
    {106, 97, 88},   // 2, the inner edge and the line under the title
    {0, 0, 0},       // 3, the shadow
    {173, 167, 150}, // 4, the four corners
    {53, 49, 48},    // 5
    {26, 22, 21},    // 6
    {0, 0, 0},       // 7
    {153, 156, 166}, // 8, the bar's top left tip
    {136, 138, 147}, // 9, the bar's left
    {83, 85, 93},    // 10, the bottom left tip (and the top ones, without the focus)
    {102, 104, 112}, // 11, the top right tip
    {0, 0, 8},       // 12, the bar's right
    {0, 0, 0},       // 13, the bottom right tip
    {185, 185, 185}, // 14
    {194, 194, 194}, // 15
    {194, 194, 194}, // 16
    {209, 188, 134}, // 17
    {205, 209, 133}, // 18
}};
constexpr std::int32_t ShadowIndex = 3;
constexpr float FullAlpha = 255.0f;
constexpr float ShadowAlpha = 179.0f;
// 0x492500: the shadow falls 5 lower; with the border, 10 shorter.
constexpr float ShadowDrop = 5.0f;
constexpr float ShadowCut = 10.0f;
// 0x492500: the title bar, 15 high at (x + 6, y + 5) and 12 narrower, with 1 × 0.9375 of the texture.
constexpr float TitleBarX = 6.0f;
constexpr float TitleBarY = 5.0f;
constexpr float TitleBarCut = 12.0f;
constexpr float TitleBarHeight = 15.0f;
constexpr float TitleBarDv = 0.9375f;
// The bar's edges: points at y + 5 and y + 19 and a 13 line at y + 6, at x + 5 and at x + w − 6.
constexpr float TitleEdgeLeft = 5.0f;
constexpr float TitleEdgeRight = 6.0f;
constexpr float TitleEdgeTop = 5.0f;
constexpr float TitleEdgeMiddle = 6.0f;
constexpr float TitleEdgeLength = 13.0f;
constexpr float TitleEdgeBottom = 19.0f;
// The line under the title, at (x + 5, y + 20), 10 narrower.
constexpr float TitleLineX = 5.0f;
constexpr float TitleLineY = 20.0f;
constexpr float TitleLineCut = 10.0f;
// The title, at (x + 9, y + 8), cut to one row of w − 0x32.
constexpr std::int32_t TitleTextX = 9;
constexpr std::int32_t TitleTextY = 8;
constexpr int TitleWrapCut = 0x32;
// The title colors (0x6c8e5c): the top window's and, for the rest, the sum at 0x492838 (0xff9b918d + 0xffc8dce6).
constexpr std::uint32_t TitleColor = 0xffc8dce6u;
constexpr std::uint32_t DimTitleColor = 0xff646e73u;
// The bar's buttons: 9 × 9 at y + 8, with 0.28125 of the texture per side. Minimize is at x + w − 0x1b or, with
// stretch, at x + w − 0x26; stretch at x + w − 27, and the cross at x + w − 16.
constexpr float TitleButtonSize = 9.0f;
constexpr float TitleButtonY = 8.0f;
constexpr std::int32_t MinimizeAloneX = 0x1b;
constexpr std::int32_t MinimizeWithStretchX = 0x26;
constexpr float StretchButtonX = 27.0f;
constexpr float CloseButtonX = 16.0f;
constexpr float TitleButtonUv = 0.28125f;
// 0x492460: the three buttons' rectangle, 0x26 × 9 from x + w − 0x26.
constexpr std::int32_t TitleButtonsWidth = 0x26;
constexpr std::int32_t TitleButtonsHeight = 9;
// The resize grip, 9 × 9 at (x + w − 10, y + h − 10), with 0.5625 of the texture.
constexpr float ResizeGripInset = 10.0f;
constexpr float ResizeGripSize = 9.0f;
constexpr float ResizeGripUv = 0.5625f;
// The glColor4f grays: for windows not on top (0.5), for a button without the mouse (0.8, 0x3f4ccccd) and pressed
// (0.6, 0x3f19999a).
constexpr float DimGray = 0.5f;
constexpr float IdleGray = 0.8f;
constexpr float PressedGray = 0.6f;
// 0x45b2e0: the disabled button, in red (1, 0.4, 0.4).
constexpr float DisabledGreen = 0.4f;
// 0x45b2e0: win_button.tga, 0.765625 × 1 of the texture; pressed, 1 inside and 0.75 × 0.9375.
constexpr float ButtonDu = 0.765625f;
constexpr float ButtonDv = 1.0f;
constexpr float PressedButtonDu = 0.75f;
constexpr float PressedButtonDv = 0.9375f;
// 0x45b2e0: the button's text, centered with ((side − text) + 0.5) · 0.5 (0x6b882c).
constexpr double Half = Math::HalfAsDouble;
constexpr std::uint32_t ButtonTextColor = 0xffc8dce6u;
// 0x4a3a90: the box's text, in white, at (5, 8) of the inner area; the second row 8 below the first's height
// (0x6b86c4).
constexpr std::uint32_t BoxTextColor = 0xffffffffu;
constexpr std::int32_t BoxTextX = 5;
constexpr std::int32_t BoxTextY = 8;
constexpr double BoxLineGap = 8.0;
// 0x48f890 and 0x48f8b0: the inner area starts at (x + 5, y + 0x15).
constexpr std::int32_t ClientLeft = 5;
constexpr std::int32_t ClientTop = 0x15;
// 0x4a5490: the list's row is measured with "0" (0x6c7ae4).
constexpr std::string_view ListRowSample = "0";
// 0x6b87d4, 0x6b8818 and 0x6b86b8: the draws' float 1, 2 and 3.
constexpr double One = 1.0;
constexpr double Two = 2.0;
constexpr double Three = 3.0;
// 0x4a2460: the view's header with the edges of the 2 and the fill of the 5; the others, with the fill of the 6.
constexpr std::int32_t HeaderEdgeIndex = 2;
constexpr std::int32_t CurrentHeaderIndex = 5;
constexpr std::int32_t OtherHeaderIndex = 6;
// 0x463090: the 9 × 9 frame with palette 0, its opposite side at +8, and the win_check.tga mark (0x64a), 5 × 5 at
// (x + 2, y + 2), with 0.625 of the texture.
constexpr std::uint16_t CheckSlot = 0x64a;
constexpr std::int32_t CheckboxEdgeIndex = 0;
constexpr float CheckboxSize = 9.0f;
constexpr std::int32_t CheckboxFar = 8;
constexpr std::int32_t CheckInset = 2;
constexpr float CheckSize = 5.0f;
constexpr float CheckUv = 0.625f;
// 0x461890: the win_scrollbar.jpg arrows (0x649), 13 × 13 at 12 from the right edge (0x6b8864); the top one a point
// above the top edge and the bottom one at 12 from the bottom. The mouse takes them from 12 off the right edge.
constexpr std::uint16_t ScrollbarSlot = 0x649;
constexpr std::int32_t ArrowInset = 12;
constexpr std::int32_t ArrowSize = 13;
constexpr float ArrowSide = 13.0f;
// The arrows' UVs: the top one from (0, 0.09375) and the bottom one from (0, 0.5), 0.8125 × 0.40625; with the click,
// flipped from (0.8125, 0.90625) and (0.8125, 0.5).
constexpr float ArrowDu = 0.8125f;
constexpr float ArrowDv = 0.40625f;
constexpr float UpArrowV = 0.09375f;
constexpr float PressedUpArrowV = 0.90625f;
constexpr float DownArrowV = 0.5f;
// The track's edges with palette 2, 1 inside the thumb and on the border; the thumb, 2 inside and 2 narrower, with
// the middle from (0, 0.03125), the top tip from (0, 0) and the bottom one from (0, 0.0625), 0.6875 × 0.03125.
constexpr std::int32_t TrackEdgeIndex = 2;
constexpr float ThumbDu = 0.6875f;
constexpr float ThumbDv = 0.03125f;
constexpr float ThumbMiddleV = 0.03125f;
constexpr float ThumbBottomV = 0.0625f;
// 0x461cd0: the chosen one's band, 13 high and 3 above the row, with alpha 0x3f333333 with the arrow keys and
// 0x3e99999a without; its text in black and the others' in 0xffc8dce6.
constexpr std::int32_t ChosenBandRise = 3;
constexpr float ChosenBandHeight = 13.0f;
constexpr float FocusedBandAlpha = 0.7f;
constexpr float BandAlpha = 0.3f;
constexpr std::uint32_t ChosenRowTextColor = 0xff000000u;
constexpr std::uint32_t RowTextColor = 0xffc8dce6u;
// 0x468320: the caret 1.5 wide without IME (+0xa8, 0x3fc00000) and the bitmap box 1.5 wider than the text and the
// caret (0x6b8b94); the caret's height, that of "Q" (0x6c7af4); on with the remainder mod 24 below 12, and its
// pixels in 0xffc8c8c8.
constexpr double CaretWidth = 1.5;
constexpr double BoxSlack = 1.5;
constexpr std::uint32_t FocusedSelectionBackground = 0xff969696u;
constexpr std::string_view CaretSample = "Q";
constexpr std::int32_t BlinkPeriod = 0x18;
constexpr std::int32_t BlinkOn = 0xc;
constexpr std::uint32_t CaretColor = 0xffc8c8c8u;
// The EDIT's +0x4c & 8 (the subclass's filter, which with 8 does not filter): the black before (glColor3f(0, 0, 0)).
constexpr std::int32_t EditBlackFlag = 0x8;
// 0x468970: the multiline EDIT's bar, 13 wide (+0xd4, 0x41500000), with the track from y + 9 to y + h − 9, the
// arrows at x + w − 15 (0x6b87bc), at y − 4 (0x6b8a44) and at y + h − 9 (0x6b887c); the edges at x + w − 13 − 2 and
// x + w − 3.
constexpr float EditBarWidth = 13.0f;
constexpr std::int32_t EditBarEnd = 9;
constexpr double EditArrowInset = 15.0;
constexpr std::int32_t EditArrowRise = 4;
constexpr std::int32_t EditArrowDrop = 9;

struct OldEditTextLayout
{
    std::int32_t pixelLeft;
    std::int32_t pixelTop;
    std::int32_t visibleColumns;
    std::int32_t caretTextColumn;
    bool caretVisible;
};

void DrawOldEditTextColumns(const OldWindowDrawContext& context, std::string_view text, const OldEditTextLayout& layout,
                            std::int32_t firstColumn, std::int32_t columns, Render::Text::TextColors colors)
{
    if (columns <= 0)
        return;
    context.text.DrawColumnsAtPixels(layout.pixelLeft + EditTextMargin + firstColumn, layout.pixelTop, text,
                                     Render::Text::TextStyle{.font = Render::Text::FontKind::Normal, .colors = colors},
                                     firstColumn, columns);
}

void DrawOldEditTextSpan(const OldWindowDrawContext& context, std::string_view text, const OldEditTextLayout& layout,
                         std::int32_t firstColumn, std::int32_t columns, Render::Text::TextColors colors)
{
    if (!layout.caretVisible || layout.caretTextColumn < firstColumn || layout.caretTextColumn >= firstColumn + columns)
    {
        DrawOldEditTextColumns(context, text, layout, firstColumn, columns, colors);
        return;
    }
    const std::int32_t beforeCaret = layout.caretTextColumn - firstColumn;
    DrawOldEditTextColumns(context, text, layout, firstColumn, beforeCaret, colors);
    DrawOldEditTextColumns(context, text, layout, layout.caretTextColumn + 1, columns - beforeCaret - 1, colors);
}

// From `firstByte` to `lastByte` (in bytes of `text`).
void DrawOldEditSelection(const OldWindowDrawContext& context, const OldEdit& edit, std::string_view text,
                          const OldEditTextLayout& layout, std::size_t firstByte, std::size_t lastByte)
{
    const std::int32_t start = context.text.Measure(context.dcFont, text.substr(0, firstByte)).width;
    const std::int32_t end = context.text.Measure(context.dcFont, text.substr(0, lastByte)).width;
    const std::int32_t selectionBegin = std::clamp(start, 0, layout.visibleColumns);
    const std::int32_t selectionEnd = std::clamp(end, selectionBegin, layout.visibleColumns);
    DrawOldEditTextSpan(context, text, layout, 0, selectionBegin,
                        Render::Text::TextColors{.text = edit.textColor, .background = 0});
    DrawOldEditTextSpan(context, text, layout, selectionBegin, selectionEnd - selectionBegin,
                        Render::Text::TextColors{.text = edit.textColor, .background = FocusedSelectionBackground});
    DrawOldEditTextSpan(context, text, layout, selectionEnd, layout.visibleColumns - selectionEnd,
                        Render::Text::TextColors{.text = edit.textColor, .background = 0});
}

void DrawOldEditText(const OldWindowDrawContext& context, const OldEdit& edit, std::string_view text,
                     const OldEditTextLayout& layout, bool focused, std::size_t selectionBegin,
                     std::size_t selectionEnd)
{
    if (focused && selectionBegin < selectionEnd)
    {
        DrawOldEditSelection(context, edit, text, layout, selectionBegin, selectionEnd);
        return;
    }
    DrawOldEditTextSpan(context, text, layout, 0, layout.visibleColumns,
                        Render::Text::TextColors{.text = edit.textColor, .background = 0});
}

void DrawOldEditCaret(const OldWindowDrawContext& context, const OldEdit& edit, std::string_view text,
                      const OldEditTextLayout& layout)
{
    if (!layout.caretVisible)
        return;
    DrawOldEditTextColumns(context, text, layout, layout.caretTextColumn, 1,
                           Render::Text::TextColors{.text = edit.backgroundColor, .background = CaretColor});
}

using Math::F32;

// A 0xAABBGGRR color (Render/Text/TextStyle.h) as glColor.
Render::Color ColorOf(std::uint32_t abgr)
{
    const auto channel = [abgr](std::uint32_t shift) { return static_cast<float>((abgr >> shift) & 0xffu) / 255.0f; };
    return Render::Color{.red = channel(0), .green = channel(8), .blue = channel(16), .alpha = channel(24)};
}

// 0x492180(x, y, w, h): the three-edge border (palette 0, 1 and 2) on top, bottom, left and right, and the four
// corners in 4; not the bottom right one with the resize grip (+0x4c & 4).
void DrawTheEdges(Render::SpriteBatch& sprites, const OldWindowBase& base)
{
    const float x = ToFloat(base.x);
    const float y = ToFloat(base.y);
    const float w = ToFloat(base.width);
    const float h = ToFloat(base.height);
    const float bottom = y + h;
    const float right = x + w;
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(x, y, w, 1.0f);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(x, y + 1.0f, w, 3.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(x, y + 4.0f, w, 1.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(x, bottom - 5.0f, w, 1.0f);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(x, bottom - 4.0f, w, 3.0f);
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(x, bottom - 1.0f, w, 1.0f);
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(x, y, 1.0f, h);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(x + 1.0f, y + 5.0f, 3.0f, h - 10.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(x + 4.0f, y + 1.0f, 1.0f, h - 2.0f);
    SetThePalette(sprites, 2, 1.0f);
    sprites.ColorQuad(right - 5.0f, y + 1.0f, 1.0f, h - 2.0f);
    SetThePalette(sprites, 1, 1.0f);
    sprites.ColorQuad(right - 4.0f, y + 5.0f, 3.0f, h - 10.0f);
    SetThePalette(sprites, 0, 1.0f);
    sprites.ColorQuad(right - 1.0f, y, 1.0f, h);
    SetThePalette(sprites, 4, 1.0f);
    sprites.ColorQuad(x + 2.0f, y + 2.0f, 1.0f, 1.0f);
    sprites.ColorQuad(right - 3.0f, y + 2.0f, 1.0f, 1.0f);
    sprites.ColorQuad(x + 2.0f, bottom - 3.0f, 1.0f, 1.0f);
    if ((base.flags & ResizeFlag) == 0)
        sprites.ColorQuad(right - 3.0f, bottom - 3.0f, 1.0f, 1.0f);
}

// 0x492460(button): a bar button's gray: the mouse one (1, or 0.6 with the click) if it is +0xa0's and the mouse is
// over the three; otherwise 0.8.
void TitleButtonGray(Render::SpriteBatch& sprites, const OldWindow& window, std::int32_t button,
                     const OldWindowDrawView& view)
{
    const OldWindowBase& base = window.base;
    if (window.frame.hotButton == button &&
        Chat::MouseInRect(view.mouseX, view.mouseY, base.x - TitleButtonsWidth + base.width, base.y + TitleTextY,
                          TitleButtonsWidth, TitleButtonsHeight, TopLeftRect))
    {
        Gray(sprites, view.leftClick ? PressedGray : 1.0f);
        return;
    }
    Gray(sprites, IdleGray);
}

// 0x4a3a90, the box's vtable + 0x50: dragged or resizing, it places the buttons again; the text in white and the
// buttons (the second, only with two).
void DrawTheBox(OldWindowManager& manager, OldWindow& window, const OldWindowDrawContext& context,
                const OldWindowDrawView& view)
{
    if (window.base.state == DraggedWindow || window.base.state == ResizedWindow)
        SettleTheButtons(manager, window);
    context.textColors = Render::Text::TextColors{.text = BoxTextColor, .background = 0};
    const OldWindowBase& base = window.base;
    const std::int32_t x = base.x + BoxTextX + ClientLeft;
    const double height = DrawText(context, x, base.y + BoxTextY + ClientTop, window.box.lines[0]);
    if (!window.box.lines[1].empty())
        static_cast<void>(
            DrawText(context, x, base.y + Truncate(height + BoxLineGap) + ClientTop, window.box.lines[1]));
    DrawTheButton(context, window.box.buttons[0], view);
    if (window.box.oneButton == 0)
        DrawTheButton(context, window.box.buttons[1], view);
}

// 0x4a5490, the list's vtable + 0x50: measures the row the first time, runs the blink and, with entries and in view,
// draws them (the chats with a friend, milestone 10: DrawTheChatsInTheList).
Core::Expected<void, std::string> DrawTheListRows(OldWindowManager& manager, OldWindow& window,
                                                  const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    OldWindowListPart& list = window.list;
    if (list.rowHeight == 0.0f)
    {
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, ListRowSample);
        list.rowHeight = static_cast<float>(static_cast<double>(extent.height) / RatioY(context.sprites.Screen()));
    }
    const double blink = static_cast<double>(list.blink) + static_cast<double>(list.blinkStep);
    list.blink = static_cast<float>(blink);
    if (blink < 0.0)
    {
        list.blinkStep = 0.0f;
        list.blink = 0.0f;
    }
    else if (blink > 1.0)
    {
        list.blinkStep = 0.0f;
        list.blink = 1.0f;
    }
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto chats = Messenger::DrawTheChatsInTheList(manager, window, context, view); !chats.HasValue())
        return chats;
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

// 0x492500, the vtable + 0x14 of the frame, the box and the list.
Core::Expected<void, std::string> DrawTheFrame(OldWindowManager& manager, OldWindow& window,
                                               const OldWindowDrawContext& context, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    const std::int32_t flags = window.base.flags;
    if (flags != 0)
    {
        SetThePalette(sprites, ShadowIndex, 1.0f);
        const float height = ToFloat(window.base.height) - ((flags & BorderFlag) != 0 ? ShadowCut : 0.0f);
        sprites.ColorQuad(ToFloat(window.base.x), ToFloat(window.base.y) + ShadowDrop, ToFloat(window.base.width),
                          height);
    }
    WhiteWithTexture(sprites);
    context.dcFont = Render::Text::FontKind::Normal;
    // vtable + 0x50: 0x48fb00 (nothing) in the frame.
    if (window.kind == OldWindowKind::MessageBox)
        DrawTheBox(manager, window, context, view);
    else if (window.kind == OldWindowKind::AddFriendBox)
    {
        if (auto drawn = Messenger::DrawTheAddFriendBox(manager, window, context, view); !drawn.HasValue())
            return drawn;
    }
    else if (window.kind == OldWindowKind::FriendsWindow)
    {
        if (auto drawn = Messenger::DrawTheFriendsWindow(manager, window, context, view); !drawn.HasValue())
            return drawn;
    }
    else if (window.kind == OldWindowKind::WindowList)
    {
        if (auto rows = DrawTheListRows(manager, window, context, view); !rows.HasValue())
            return rows;
    }
    else if (window.kind == OldWindowKind::ChatWindow)
    {
        if (auto drawn = Messenger::DrawTheChatWindow(manager, window, context, view); !drawn.HasValue())
            return drawn;
    }
    else if (window.kind == OldWindowKind::LetterReadWindow)
    {
        if (auto drawn = Messenger::DrawTheReadWindow(manager, window, context, view); !drawn.HasValue())
            return drawn;
    }
    else if (window.kind == OldWindowKind::LetterWriteWindow)
    {
        if (auto drawn = Messenger::DrawTheWriteWindow(manager, window, context, view); !drawn.HasValue())
            return drawn;
    }
    const OldWindowBase& base = window.base;
    // 0x49258d..0x4925dd: the one at the end of the list with the friends window open is drawn as it is; the rest, in
    // gray.
    const bool dim = LastOldWindow(manager) != base.id || !view.friendsOpen;
    if (dim)
        Gray(sprites, DimGray);
    const float x = ToFloat(base.x);
    const float y = ToFloat(base.y);
    const float w = ToFloat(base.width);
    const float right = x + w;
    if ((flags & BorderFlag) != 0)
    {
        if ((flags & TitleFlag) != 0)
        {
            Quad(sprites, WindowTitleSlot, x + TitleBarX, y + TitleBarY, w - TitleBarCut, TitleBarHeight, 0.0f, 0.0f,
                 1.0f, TitleBarDv);
            SetThePalette(sprites, dim ? 10 : 8, 1.0f);
            sprites.ColorQuad(x + TitleEdgeLeft, y + TitleEdgeTop, 1.0f, 1.0f);
            SetThePalette(sprites, dim ? 10 : 9, 1.0f);
            sprites.ColorQuad(x + TitleEdgeLeft, y + TitleEdgeMiddle, 1.0f, TitleEdgeLength);
            SetThePalette(sprites, 10, 1.0f);
            sprites.ColorQuad(x + TitleEdgeLeft, y + TitleEdgeBottom, 1.0f, 1.0f);
            SetThePalette(sprites, 11, 1.0f);
            sprites.ColorQuad(right - TitleEdgeRight, y + TitleEdgeTop, 1.0f, 1.0f);
            SetThePalette(sprites, 12, 1.0f);
            sprites.ColorQuad(right - TitleEdgeRight, y + TitleEdgeMiddle, 1.0f, TitleEdgeLength);
            SetThePalette(sprites, 13, 1.0f);
            sprites.ColorQuad(right - TitleEdgeRight, y + TitleEdgeBottom, 1.0f, 1.0f);
        }
        DrawTheEdges(sprites, base);
        WhiteWithTexture(sprites);
    }
    if ((flags & TitleFlag) != 0)
    {
        sprites.SetBlend(Render::BlendMode::Alpha);
        SetThePalette(sprites, 2, 1.0f);
        sprites.ColorQuad(x + TitleLineX, y + TitleLineY, w - TitleLineCut, 1.0f);
        WhiteWithTexture(sprites);
        context.dcFont = Render::Text::FontKind::Bold;
        context.textColors = Render::Text::TextColors{.text = dim ? DimTitleColor : TitleColor, .background = 0};
        // 0x459c70(title, row, w − 0x32, 1, 0x100, 0, 0), with the DC's font.
        auto wrapped = Chat::WrapByWidth(window.frame.title.value_or(std::string()), context.text, context.dcFont,
                                         sprites.Screen(), base.width - TitleWrapCut, 1, 0);
        if (!wrapped.HasValue())
            return Core::Unexpected(std::move(wrapped).Error());
        const std::string title = wrapped.Value().lines.empty() ? std::string() : wrapped.Value().lines[0];
        static_cast<void>(DrawText(context, base.x + TitleTextX, base.y + TitleTextY, title));
        const float buttonY = y + TitleButtonY;
        if ((flags & MinimizeFlag) != 0)
        {
            TitleButtonGray(sprites, window, 1, view);
            const std::int32_t from = (flags & StretchFlag) != 0 ? MinimizeWithStretchX : MinimizeAloneX;
            Quad(sprites, WindowTitleButtonsSlot, ToFloat(base.x) + w - ToFloat(from), buttonY, TitleButtonSize,
                 TitleButtonSize, 0.0f, 0.0f, TitleButtonUv, TitleButtonUv);
        }
        if ((flags & StretchFlag) != 0)
        {
            TitleButtonGray(sprites, window, 2, view);
            Quad(sprites, WindowTitleButtonsSlot, right - StretchButtonX, buttonY, TitleButtonSize, TitleButtonSize,
                 TitleButtonUv, window.frame.stretched != 0 ? TitleButtonUv : 0.0f, TitleButtonUv, TitleButtonUv);
        }
        TitleButtonGray(sprites, window, 3, view);
        Quad(sprites, WindowTitleButtonsSlot, right - CloseButtonX, buttonY, TitleButtonSize, TitleButtonSize, 0.0f,
             TitleButtonUv, TitleButtonUv, TitleButtonUv);
        sprites.SetColor(Render::Color{});
        context.dcFont = Render::Text::FontKind::Normal;
    }
    if ((flags & ResizeFlag) != 0)
        Quad(sprites, WindowResizeSlot, right - ResizeGripInset, ToFloat(base.y + base.height) - ResizeGripInset,
             ResizeGripSize, ResizeGripSize, 0.0f, 0.0f, ResizeGripUv, ResizeGripUv);
    if (dim)
    {
        sprites.SetColor(Render::Color{});
        sprites.SetBlend(Render::BlendMode::Opaque);
    }
    // vtable + 0x54: 0x48f900 (nothing) in all, except a letter's (0x49a1d0 and 0x498800, the portrait).
    return Messenger::DrawTheLetterPortrait(window, view);
}
} // namespace

namespace OldDraw
{
OldWindowDrawView WithCurrentFocus(const OldWindowManager& manager, const OldWindowDrawView& view)
{
    OldWindowDrawView current = view;
    if (view.globals != nullptr)
    {
        current.focusedList = view.globals->focusedList;
        current.focusedEdit = view.globals->keyboardFocus == Chat::ChatFocus::OldWindow ? manager.focusedEdit : 0;
    }
    return current;
}
// _ftol (0x69fd3c): toward 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

// rx and ry (0x5762ea8 and 0x5762eac): W / 640 and H / 480 as float. Without a screen, 1.
double RatioX(Core::PixelSize screen)
{
    return screen.width > 0 ? static_cast<double>(Render::ScaleX(1.0f, screen)) : 1.0;
}

double RatioY(Core::PixelSize screen)
{
    return screen.height > 0 ? static_cast<double>(Render::ScaleY(1.0f, screen)) : 1.0;
}

float ToFloat(std::int32_t value)
{
    return static_cast<float>(value);
}

void Gray(Render::SpriteBatch& sprites, float gray)
{
    sprites.SetColor(Render::Color{.red = gray, .green = gray, .blue = gray, .alpha = 1.0f});
}

// 0x459ec0: glColor4f(1, 1, 1, 1) and glEnable(GL_TEXTURE_2D), after the untextured quads.
void WhiteWithTexture(Render::SpriteBatch& sprites)
{
    sprites.SetColor(Render::Color{});
    sprites.EnableTexture();
}

void SetThePalette(Render::SpriteBatch& sprites, std::int32_t index, float alpha)
{
    if (const std::optional<Render::Color> color = OldWindowPalette(index, alpha))
        sprites.SetColor(*color);
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): scaled in size and position, without alpha.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
          float v, float du, float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = u,
                                      .v = v,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

// 0x5542f0(x, y, text, 0, 1, 0) → 0x465950: the text with the DC's font and 0x6c8e5c's colors; an empty one is not
// drawn, nor its background (0x467077..0x467098: without text and with the box, the fourth argument, at 0, which is
// what 0x460550 and 0x465770 pass, it returns early). Returns what 0x467060 leaves on the x87: the text's height in
// 640 × 480 (cy / ry), half if it starts with '\n', or 0 without text.
double DrawText(const OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    if (text.empty())
        return 0.0;
    context.text.Draw(x, y, text, Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors});
    const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, text);
    const double height = static_cast<double>(extent.height) / RatioY(context.sprites.Screen());
    return text.front() == '\n' ? height / 2.0 : height;
}

// 0x45b2e0, the button's vtable + 0x14: win_button.tga in the mouse's gray and the text centered. Disabled, in red and
// without going back to the opaque blend.
void DrawTheButton(const OldWindowDrawContext& context, const OldButton& button, const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OldWindowBase& base = button.base;
    sprites.SetBlend(Render::BlendMode::Alpha);
    const bool disabled = base.state == DisabledWindow;
    const bool pressed = !disabled && button.pressed == 1;
    if (disabled)
        sprites.SetColor(Render::Color{.red = 1.0f, .green = DisabledGreen, .blue = DisabledGreen, .alpha = 1.0f});
    else if (!Chat::MouseInRect(view.mouseX, view.mouseY, base.x, base.y, base.width, base.height, TopLeftRect))
        Gray(sprites, IdleGray);
    else
        Gray(sprites, pressed ? PressedGray : 1.0f);
    if (pressed)
        Quad(sprites, WindowButtonSlot, ToFloat(base.x + 1), ToFloat(base.y + 1), ToFloat(base.width) - 1.0f,
             ToFloat(base.height) - 1.0f, 0.0f, 0.0f, PressedButtonDu, PressedButtonDv);
    else
        Quad(sprites, WindowButtonSlot, ToFloat(base.x), ToFloat(base.y), ToFloat(base.width), ToFloat(base.height),
             0.0f, 0.0f, ButtonDu, ButtonDv);
    if (button.label)
    {
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, *button.label);
        context.textColors = Render::Text::TextColors{.text = ButtonTextColor, .background = 0};
        const Core::PixelSize screen = sprites.Screen();
        const std::int32_t top = base.y + (pressed ? 2 : 1);
        const std::int32_t left = base.x + (pressed ? 1 : 0);
        const std::int32_t y = Truncate(
            (static_cast<double>(base.height) - static_cast<double>(extent.height) / RatioY(screen) + Half) * Half +
            static_cast<double>(top));
        const std::int32_t x = Truncate(
            (static_cast<double>(base.width) - static_cast<double>(extent.width) / RatioX(screen) + Half) * Half +
            static_cast<double>(left));
        static_cast<void>(DrawText(context, x, y, *button.label));
    }
    sprites.SetColor(Render::Color{});
    if (!disabled)
        sprites.SetBlend(Render::BlendMode::Opaque);
}

bool MouseIn(const OldWindowDrawView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    return Chat::MouseInRect(view.mouseX, view.mouseY, x, y, width, height, TopLeftRect);
}

void DrawTheTabHeaders(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, std::int32_t width,
                       std::int32_t height, std::int32_t count, std::int32_t current)
{
    std::int32_t left = x;
    for (std::int32_t at = 0; at < count; ++at, left += width)
    {
        SetThePalette(sprites, HeaderEdgeIndex, 1.0f);
        const double fx = ToFloat(left);
        const double fy = ToFloat(y);
        const double fw = ToFloat(width);
        const double fh = ToFloat(height);
        if (at == current)
        {
            sprites.ColorQuad(F32(fx), F32(fy), F32(fw), 1.0f);
            sprites.ColorQuad(F32(fx - One), F32(fy), 1.0f, F32(fh));
            sprites.ColorQuad(F32(fw + fx - One), F32(fy), 1.0f, F32(fh));
            SetThePalette(sprites, CurrentHeaderIndex, 1.0f);
            sprites.ColorQuad(F32(fx), F32(fy + One), F32(fw - One), F32(fh - One));
            continue;
        }
        const float below = F32(fy + One);
        sprites.ColorQuad(F32(fx), below, F32(fw), 1.0f);
        sprites.ColorQuad(F32(fw + fx - One), below, 1.0f, F32(fh - One));
        sprites.ColorQuad(F32(fx), F32(fh + fy - One), F32(fw), 1.0f);
        SetThePalette(sprites, OtherHeaderIndex, 1.0f);
        sprites.ColorQuad(F32(fx), F32(fy + Two), F32(fw - One), F32(fh - Three));
    }
}

void DrawTheCheckbox(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool checked)
{
    sprites.SetBlend(Render::BlendMode::Opaque);
    SetThePalette(sprites, CheckboxEdgeIndex, 1.0f);
    const float fx = ToFloat(x);
    const float fy = ToFloat(y);
    sprites.ColorQuad(fx, fy, CheckboxSize, 1.0f);
    sprites.ColorQuad(fx, ToFloat(y + CheckboxFar), CheckboxSize, 1.0f);
    sprites.ColorQuad(fx, fy, 1.0f, CheckboxSize);
    sprites.ColorQuad(ToFloat(x + CheckboxFar), fy, 1.0f, CheckboxSize);
    WhiteWithTexture(sprites);
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (checked)
        Quad(sprites, CheckSlot, ToFloat(x + CheckInset), ToFloat(y + CheckInset), CheckSize, CheckSize, 0.0f, 0.0f,
             CheckUv, CheckUv);
}

void DrawTheListBar(Render::SpriteBatch& sprites, OldList& list, std::size_t size, const OldWindowDrawView& view)
{
    PlaceTheListBar(list, size);
    const OldWindowBase& base = list.base;
    // FILD x and FIADD w.
    const double right = static_cast<double>(ToFloat(base.x)) + static_cast<double>(base.width);
    const float arrowX = F32(right - static_cast<double>(ArrowInset));
    const std::int32_t mouseX = base.x + base.width - ArrowInset;
    const bool up = view.leftClick && MouseIn(view, mouseX, base.y - base.height - 1, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX, F32(static_cast<double>(base.y) - static_cast<double>(base.height) - One),
         ArrowSide, ArrowSide, up ? ArrowDu : 0.0f, up ? PressedUpArrowV : UpArrowV, up ? -ArrowDu : ArrowDu,
         up ? -ArrowDv : ArrowDv);
    const bool down = view.leftClick && MouseIn(view, mouseX, base.y - ArrowInset, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX, F32(static_cast<double>(base.y) - static_cast<double>(ArrowInset)), ArrowSide,
         ArrowSide, down ? ArrowDu : 0.0f, DownArrowV, down ? -ArrowDu : ArrowDu, down ? -ArrowDv : ArrowDv);

    SetThePalette(sprites, TrackEdgeIndex, 1.0f);
    const float track = F32(static_cast<double>(list.barBottom) - static_cast<double>(list.barTop));
    sprites.ColorQuad(F32(right - static_cast<double>(list.thumbWidth) + One), list.barTop, 1.0f, track);
    sprites.ColorQuad(F32(right), list.barTop, 1.0f, track);
    WhiteWithTexture(sprites);

    // 0x461a53: with fewer than the rows, the thumb is the whole track.
    const bool full = ListCount(list, size) < list.rows;
    const float top = full ? list.barTop : list.thumbY;
    const float height = full ? track : list.thumbHeight;
    const float thumbX = F32(right - static_cast<double>(list.thumbWidth) + Two);
    const float thumbWidth = F32(static_cast<double>(list.thumbWidth) - Two);
    Quad(sprites, ScrollbarSlot, thumbX, top, thumbWidth, height, 0.0f, ThumbMiddleV, ThumbDu, ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX, top, thumbWidth, 1.0f, 0.0f, 0.0f, ThumbDu, ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX, F32(static_cast<double>(list.thumbHeight) + static_cast<double>(top) - One),
         thumbWidth, 1.0f, 0.0f, ThumbBottomV, ThumbDu, ThumbDv);
}

void DrawTheOldList(const OldWindowDrawContext& context, const OldList& list, std::size_t size,
                    const OldListDrawing& drawing)
{
    if (drawing.head)
        drawing.head();
    std::size_t at = FirstShownInTheList(list);
    context.sprites.SetColor(Render::Color{});
    context.dcFont = Render::Text::FontKind::Normal;
    // 0x47a859..0x47a8ed: +0x104 is read again each pass; the iterator advances one per row drawn.
    for (std::int32_t row = 0; row < list.rows && at < size; ++row, ++at)
    {
        const std::int32_t step = drawing.row ? drawing.row(row, at) : 1;
        if (step < 0)
            row -= step;
        else if (step == 0)
            --row;
    }
    if (drawing.tail)
        drawing.tail();
}

void DrawTheChosenRow(const OldWindowDrawContext& context, const OldList& list, std::size_t size, std::int32_t row,
                      std::int32_t focusedList)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.textColors.text = RowTextColor;
    if (list.selection == list.scroll + row + 1)
    {
        const float alpha = focusedList == list.base.id ? FocusedBandAlpha : BandAlpha;
        sprites.SetColor(Render::Color{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = alpha});
        sprites.ColorQuad(ToFloat(list.base.x), ToFloat(ListRowY(list, size, row) - ChosenBandRise),
                          F32(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + One),
                          ChosenBandHeight);
        WhiteWithTexture(sprites);
        context.textColors.text = ChosenRowTextColor;
    }
    context.textColors.background = 0;
}

void DrawTheEditBar(const OldWindowDrawContext& context, OldEdit& edit, std::int32_t lineCount,
                    const OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OldWindowBase& base = edit.base;
    // GetScrollPos(SB_VERT) and EM_GETLINECOUNT, on the x87; +0xd0 / rows, to a float.
    const double position = static_cast<double>(edit.firstLine);
    const double lines = static_cast<double>(lineCount);
    const auto shown = static_cast<float>(static_cast<double>(edit.visibleLines) / lines);
    edit.barWidth = EditBarWidth;
    edit.barTop = ToFloat(base.y + EditBarEnd);
    edit.barBottom = ToFloat(base.y + base.height - EditBarEnd);
    const double track = static_cast<double>(edit.barBottom) - static_cast<double>(edit.barTop);
    edit.thumbHeight = F32(track * std::min(static_cast<double>(shown), One));
    edit.thumbY = F32(static_cast<double>(edit.barTop) + track * std::min(position / lines, One));
    // FILD w, FIADD x: the arrows at x + w − 15, y − 4 and y + h − 9.
    const double right = static_cast<double>(base.width) + static_cast<double>(base.x);
    const float arrowX = F32(right - EditArrowInset);
    const std::int32_t mouseArrowX = base.x + base.width - static_cast<std::int32_t>(EditArrowInset);
    const bool up = view.leftClick && MouseIn(view, mouseArrowX, base.y - EditArrowRise, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX, F32(static_cast<double>(base.y) - static_cast<double>(EditArrowRise)),
         ArrowSide, ArrowSide, up ? ArrowDu : 0.0f, up ? PressedUpArrowV : UpArrowV, up ? -ArrowDu : ArrowDu,
         up ? -ArrowDv : ArrowDv);
    const bool down =
        view.leftClick && MouseIn(view, mouseArrowX, base.y + base.height - EditArrowDrop, ArrowSize, ArrowSize);
    Quad(sprites, ScrollbarSlot, arrowX,
         F32(static_cast<double>(base.height) + static_cast<double>(base.y) - static_cast<double>(EditArrowDrop)),
         ArrowSide, ArrowSide, down ? ArrowDu : 0.0f, DownArrowV, down ? -ArrowDu : ArrowDu, down ? -ArrowDv : ArrowDv);

    sprites.SetBlend(Render::BlendMode::Alpha);
    SetThePalette(sprites, TrackEdgeIndex, 1.0f);
    const float trackHeight = F32(track);
    const double inner = right - static_cast<double>(edit.barWidth);
    sprites.ColorQuad(F32(inner - Two), edit.barTop, 1.0f, trackHeight);
    sprites.ColorQuad(F32(right - Three), edit.barTop, 1.0f, trackHeight);
    WhiteWithTexture(sprites);
    sprites.SetBlend(Render::BlendMode::Opaque);
    const float thumbX = F32(inner - One);
    const float thumbWidth = F32(static_cast<double>(edit.barWidth) - Two);
    Quad(sprites, ScrollbarSlot, thumbX, edit.thumbY, thumbWidth, edit.thumbHeight, 0.0f, ThumbMiddleV, ThumbDu,
         ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX, edit.thumbY, thumbWidth, 1.0f, 0.0f, 0.0f, ThumbDu, ThumbDv);
    Quad(sprites, ScrollbarSlot, thumbX,
         F32(static_cast<double>(edit.thumbY) + static_cast<double>(edit.thumbHeight) - One), thumbWidth, 1.0f, 0.0f,
         ThumbBottomV, ThumbDu, ThumbDv);
}

void DrawTheOldEdit(const OldWindowDrawContext& context, OldEdit& edit, bool focused, const OldWindowDrawView& view)
{
    // IsWindowVisible: vtable + 4 (0x468260) hides it with state 3.
    if (edit.base.state == HiddenWindow)
        return;
    Render::SpriteBatch& sprites = context.sprites;
    const Core::PixelSize screen = sprites.Screen();
    const double rx = RatioX(screen);
    const double ry = RatioY(screen);
    // 0x468444: GetTextExtentPointA of "Q" in the DC, in pixels, the first time.
    if (edit.caretHeight == 0.0f)
        edit.caretHeight = ToFloat(context.text.Measure(context.dcFont, CaretSample).height);
    const OldWindowBase& base = edit.base;
    // 0x468477: the EDIT's +0x4c, the subclass's filter (OldEdit::filter).
    if ((edit.filter & EditBlackFlag) != 0)
    {
        sprites.SetColor(Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f});
        sprites.ColorQuad(ToFloat(base.x), ToFloat(base.y), ToFloat(base.width), edit.caretHeight);
        WhiteWithTexture(sprites);
    }
    // 0x4684bc..0x4684e4: GetFocus() is the EDIT and +0x80 % 0x18 < 0xc.
    const bool caretOn = focused && edit.draws % BlinkPeriod < BlinkOn;
    // 0x46854d: GetWindowTextA (vtable + 0x50, 0x467ec0) sends WM_GETTEXT to the EDIT, which goes through its subclass
    // (0x467b10): with an arrow down (0x467b35..0x467b69), +0x80 at 0. The INC at the end leaves it at 1: with an arrow
    // held, the caret stays on (Q-C1304).
    if (context.arrowHeld)
        edit.draws = 0;
    // 0x468550..0x4685ec: the text's cx in the DC with whatever font it has; with ES_PASSWORD (+0xb0,
    // TextField::Masked), that of as many '*' as bytes in bold (SelectObject with 0x5763558), and the DC is left with
    // the normal one (0x5763554). The EDIT paints with its font (WinMain's normal one) and one '*' per byte
    // (*inferred*: the ES_PASSWORD character of an EDIT without the comctl32 6 manifest), and its caret ends up at the
    // end of that.
    const std::string text = edit.text.Shown();
    std::int32_t textWidth = 0;
    if (edit.text.Masked())
    {
        textWidth = context.text.Measure(Render::Text::FontKind::Bold, text).width;
        context.dcFont = Render::Text::FontKind::Normal;
    }
    else
        textWidth = context.text.Measure(context.dcFont, text).width;
    // The rows (one without +0xc4), the first in view and the caret's (CL9 milestone 10).
    const std::vector<std::string_view> lines = EditLines(edit, context.text);
    const auto lineCount = static_cast<std::int32_t>(lines.size());
    const std::int32_t caretLine = EditCaretLine(edit, lines);
    FollowTheEditCaret(edit, lineCount, caretLine);
    std::int32_t width = 0;
    std::int32_t height = 0;
    if (edit.multiline == 0)
    {
        // 0x4685ee..0x468622: the bitmap's width, ftol(+0xa8 + 1.5 + cx), up to ftol(w · rx); the height, ftol(h · ry).
        width = Truncate(CaretWidth + BoxSlack + static_cast<double>(textWidth));
        const double edge = static_cast<double>(base.width) * rx;
        if (static_cast<double>(width) > edge)
            width = Truncate(edge);
        height = Truncate(static_cast<double>(base.height) * ry);
    }
    else
    {
        // 0x468638..0x468687: with +0xc4, the whole width (ftol(w · rx), in one or two textures); with 1 and fewer rows
        // than +0xd0, ftol(rows · +0xac) high, and otherwise ftol(h · ry).
        width = Truncate(static_cast<double>(base.width) * rx);
        if (edit.multiline == 1 && lineCount < edit.visibleLines)
            height = Truncate(static_cast<double>(lineCount) * static_cast<double>(edit.caretHeight));
        else
            height = Truncate(static_cast<double>(base.height) * ry);
    }
    // 0x4688d8..0x468915: 0x554000 at (ftol(x · rx), ftol(y · ry)), in pixels.
    const std::int32_t left = Truncate(static_cast<double>(base.x) * rx);
    const std::int32_t top = Truncate(static_cast<double>(base.y) * ry);
    sprites.SetColor(ColorOf(edit.backgroundColor));
    sprites.ColorQuad(F32(static_cast<double>(left) / rx), F32(static_cast<double>(top) / ry),
                      F32(static_cast<double>(width) / rx), F32(static_cast<double>(height) / ry));
    WhiteWithTexture(sprites);
    if (edit.multiline == 0)
    {
        const std::int32_t visibleTextWidth = std::min(textWidth, std::max(width - EditTextMargin, 0));
        // The caret's column (GetCaretPos): with ES_PASSWORD, that of the '*' in the normal font the DC has now.
        const std::int32_t cursorWidth =
            context.text.Measure(context.dcFont, std::string_view(text).substr(0, edit.text.Cursor())).width;
        const std::int32_t caretColumn = EditTextMargin + cursorWidth + 1;
        const OldEditTextLayout textLayout{left, top, visibleTextWidth, caretColumn - EditTextMargin,
                                           caretOn && caretColumn < width};
        DrawOldEditText(context, edit, text, textLayout, focused, edit.text.SelectionBegin(), edit.text.SelectionEnd());
        // 0x468320 changes the ink pixels' color to the background and the others' to c8.
        DrawOldEditCaret(context, edit, text, textLayout);
    }
    else
    {
        // What the multiline Windows EDIT paints: the whole rows that fit in the height from the first in view, each as
        // tall as "Q" (*inferred*, P-C217), with the single-line one's margin and caret (the caret's column in the
        // caret's row).
        const std::string_view value = edit.text.Value();
        const double lineHeight = static_cast<double>(edit.caretHeight);
        for (std::int32_t at = edit.firstLine, row = 0; at < lineCount; ++at, ++row)
        {
            if (static_cast<double>(row + 1) * lineHeight > static_cast<double>(height))
                break;
            const std::string_view line = lines[static_cast<std::size_t>(at)];
            const auto lineStart = static_cast<std::size_t>(line.data() - value.data());
            const auto relative = [&](std::size_t offset)
            { return std::min(offset > lineStart ? offset - lineStart : std::size_t{0}, line.size()); };
            const std::int32_t lineWidth = context.text.Measure(context.dcFont, line).width;
            const std::int32_t cursorWidth =
                context.text.Measure(context.dcFont, line.substr(0, relative(edit.text.Cursor()))).width;
            const std::int32_t caretColumn = EditTextMargin + cursorWidth + 1;
            const OldEditTextLayout lineLayout{left, top + Truncate(static_cast<double>(row) * lineHeight),
                                               std::min(lineWidth, std::max(width - EditTextMargin, 0)),
                                               caretColumn - EditTextMargin,
                                               caretOn && at == caretLine && caretColumn < width};
            DrawOldEditText(context, edit, line, lineLayout, focused, relative(edit.text.SelectionBegin()),
                            relative(edit.text.SelectionEnd()));
            DrawOldEditCaret(context, edit, line, lineLayout);
        }
    }
    // 0x46894e: with +0xc4 at 1, the bar (0x468970).
    if (edit.multiline == 1)
        DrawTheEditBar(context, edit, lineCount, view);
    ++edit.draws;
}
} // namespace OldDraw

void DrawTheOldButton(const OldWindowDrawContext& context, const OldButton& button, const OldWindowDrawView& view)
{
    OldDraw::DrawTheButton(context, button, view);
}

std::optional<Render::Color> OldWindowPalette(std::int32_t index, float alpha)
{
    if (index < 0 || index >= static_cast<std::int32_t>(Palette.size()))
        return std::nullopt;
    // ftol(alpha · 255) and ftol(alpha · 179), of which glColor4ub takes the low byte.
    const auto byteOf = [](double value)
    { return static_cast<float>(static_cast<std::uint32_t>(OldDraw::Truncate(value)) & 0xffu) / 255.0f; };
    const PaletteEntry& entry = Palette[static_cast<std::size_t>(index)];
    const double scale = index == ShadowIndex ? static_cast<double>(ShadowAlpha) : static_cast<double>(FullAlpha);
    return Render::Color{.red = static_cast<float>(entry.red) / 255.0f,
                         .green = static_cast<float>(entry.green) / 255.0f,
                         .blue = static_cast<float>(entry.blue) / 255.0f,
                         .alpha = byteOf(static_cast<double>(alpha) * scale)};
}

Core::Expected<void, std::string> DrawTheOldWindows(OldWindowManager& manager, const OldWindowDrawContext& context,
                                                    const OldWindowDrawView& view)
{
    for (const std::int32_t id : manager.order)
    {
        OldWindow* window = FindTheOldWindow(manager, id);
        if (window == nullptr || window->base.state == HiddenWindow || window->base.state == MinimizedWindow)
            continue;
        if (auto drawn = DrawTheFrame(manager, *window, context, OldDraw::WithCurrentFocus(manager, view));
            !drawn.HasValue())
            return drawn;
    }
    manager.drawn = 1;
    return {};
}

Core::Expected<void, std::string> DrawTheWindowList(OldWindowManager& manager, const OldWindowDrawContext& context,
                                                    const OldWindowDrawView& view)
{
    return DrawTheFrame(manager, manager.windowList, context, view);
}
} // namespace Client::Ui
