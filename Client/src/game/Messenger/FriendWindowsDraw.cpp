#include "Messenger/FriendWindowsDraw.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/Frame/ITextDrawer.h"
#include "Messenger/FriendModel.h"
#include "Messenger/FriendWindows.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindowSteps.h"
#include "Ui/OldWindows.h"
#include "Crt/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Messenger
{
namespace
{
using Ui::OldDraw::DrawText;
using Ui::OldDraw::DrawTheButton;
using Ui::OldDraw::RatioX;
using Ui::OldDraw::RatioY;
using Ui::OldDraw::SetThePalette;
using Ui::OldDraw::ToFloat;
using Ui::OldDraw::Truncate;
using Ui::OldDraw::WhiteWithTexture;

// The Text.bmd texts (the address − 0x76bd1dc, divided by 300): the friend list's columns (0x7707e58 and 0x7707f84)
// and the window list's (0x7708434), the state button (0x7708ec0) and a friend's place: offline with 0xff, 0xfe and
// 0xfc (0x7709370), 0xfd (0x77095c8) or the server + 1 (0x77096f4, a format).
constexpr std::size_t NameColumnText = 1021;
constexpr std::size_t PlaceColumnText = 1022;
constexpr std::size_t WindowColumnText = 1026;
constexpr std::size_t StateButtonText = 1035;
constexpr std::size_t OfflinePlaceText = 1039;
constexpr std::size_t FdPlaceText = 1041;
constexpr std::size_t ServerPlaceText = 1042;
constexpr std::uint8_t FdState = 0xfd;
constexpr std::uint8_t FeState = 0xfe;
constexpr std::uint8_t FfState = 0xff;
// 0x6c8e5c's colors: the white of the header on view or under the mouse and that of the others.
constexpr std::uint32_t LitColor = 0xffffffffu;
constexpr std::uint32_t DimColor = 0xffc8dce6u;
// 0x491960's indices.
constexpr std::int32_t BlackIndex = 7;
constexpr std::int32_t LineIndex = 2;
constexpr std::int32_t ColumnBarIndex = 5;
constexpr std::int32_t ColumnTickIndex = 14;
// 0x4a2620: the 20-high band (0x41a00000) above the inner area, the three 0x35 × 0x13 headers at y + 2 and the
// palette 2 line from x + 0x9f, 0x12 below them.
constexpr float HeaderBarHeight = 20.0f;
constexpr std::int32_t HeaderY = 2;
constexpr std::int32_t HeaderWidth = 0x35;
constexpr std::int32_t HeaderHeight = 0x13;
constexpr std::int32_t HeaderLineX = 0x9f;
constexpr std::int32_t HeaderLineBelow = 0x12;
// The titles, centered with ((52 − cx / rx) + 0.5) · 0.5 (0x6ba060) and ((24 − cy / ry) + 0.5) · 0.5 (0x6b8800) at
// x + 0, x + 0x36 and x + 0x6b: one point right of their header, except the first.
constexpr std::array<std::int32_t, FriendsTabCount> HeaderTextX{0, 0x36, 0x6b};
constexpr double HeaderTextWidth = 52.0;
constexpr double HeaderTextHeight = 24.0;
constexpr double Half = Math::HalfAsDouble;
// The state button's text, 2 before the right edge (0x6b8818), and the checkbox 14 (0x6b881c) and 1 (0x6b87d4)
// further left and 1 higher.
constexpr double StateTextInset = 2.0;
constexpr double CheckboxGap = 14.0;
constexpr double One = 1.0;
// 0x49cc20 and 0x49e7f0: the black under the list from 0x12 below its height (with 18 less, 0x6b886c); the columns'
// band of 16 (0x41800000) with its line below; the line over the buttons, 0x11 below the list's height.
constexpr std::int32_t BelowTheList = 0x12;
constexpr double BelowTheListCut = 18.0;
constexpr float ColumnBarHeight = 16.0f;
constexpr std::int32_t ColumnLineY = 0x10;
constexpr std::int32_t ListLineBelow = 0x11;
// 0x49cc20: the line between the columns from y + 0x11 with 22 and 17 less height (0x6b8820 and 0x6b8868), and its
// mark on the band, of 10 (0x41200000) from y + 3.
constexpr std::int32_t SeparatorY = 0x11;
constexpr double SeparatorCut = 22.0;
constexpr double SeparatorCutAgain = 17.0;
constexpr std::int32_t TickY = 3;
constexpr float TickHeight = 10.0f;
// The column titles at (x + 4, y + 3) and (x + 8, y + 3); the mouse takes them as 0x13 high.
constexpr std::int32_t ColumnTextX = 4;
constexpr std::int32_t WindowColumnTextX = 8;
constexpr std::int32_t ColumnTextY = 3;
constexpr std::int32_t ColumnHeight = 0x13;
// 0x461cd0 and 0x462a60: the name at x + 3 and the place 4 after the column; a window's title at x + 8.
constexpr std::int32_t FriendNameX = 3;
constexpr std::int32_t PlaceGap = 4;
constexpr std::int32_t WindowTitleX = 8;
// 0x4a0a30 (CL9 milestone 10): the letter list's column titles (0x770868c, 0x77087b8 and 0x77088e4).
constexpr std::size_t SenderColumnText = 1028;
constexpr std::size_t DateColumnText = 1029;
constexpr std::size_t SubjectColumnText = 1030;
// 0x4a0a30 and 0x4641f0: the checkbox at x + 1 (the all one at y + 3, a letter's 1 above its row) and win_mail.tga
// (0x64b) at x + 0xb, 13 × 9 from the top (0, 0, 0.8125 and 0.28125); a read letter's (+0x134 at 1), 13 × 10 from
// 0.28125 with 0.3125 of height, 2 above its row.
constexpr std::uint16_t MailSlot = 0x64b;
constexpr std::int32_t LetterCheckboxX = 1;
constexpr std::int32_t MailX = 0xb;
constexpr float MailWidth = 13.0f;
constexpr float MailHeight = 9.0f;
constexpr float MailDu = 0.8125f;
constexpr float MailDv = 0.28125f;
constexpr std::int32_t OpenedLetter = 1;
constexpr float OpenedMailHeight = 10.0f;
constexpr float OpenedMailV = 0.28125f;
constexpr float OpenedMailDv = 0.3125f;
constexpr std::int32_t OpenedMailRise = 2;
// 0x4641f0: the name and the subject 4 after their column, in a box 4 smaller (0x6b8a44); the date from +0x115,
// without its first two characters.
constexpr std::int32_t LetterTextGap = 4;
constexpr double LetterBoxCut = 4.0;
constexpr std::size_t DateSkip = 2;
// 0x4670ba..0x4670d0: an empty text measures 0x6c7ae4's "0".
constexpr std::string_view EmptyTextMeasure = "0";

std::string TextAt(const Ui::OldWindowDrawView& view, std::size_t index)
{
    return view.texts != nullptr ? view.texts->At(index) : std::string();
}

using Math::F32;

// 0x461e31..0x461ebd: sprintf of the place text into the row's buffer.
std::string PlaceOf(const Ui::OldWindowDrawView& view, std::uint8_t state)
{
    if (state == FfState || state == FeState || state == AllOfflineState)
        return Crt::Sprintf(TextAt(view, OfflinePlaceText));
    if (state == FdState)
        return Crt::Sprintf(TextAt(view, FdPlaceText));
    return Crt::Sprintf(TextAt(view, ServerPlaceText), static_cast<std::int32_t>(state) + 1);
}

// 0x462a60, the window list's vtable + 0x80: the row and the title (sprintf "%s"). Returns 1.
std::int32_t DrawTheWindowRow(const Ui::OldWindowDrawContext& context, const Ui::OldWindow& tab, std::int32_t row,
                              std::size_t at, const Ui::OldWindowDrawView& view)
{
    const Ui::OldList& list = tab.tab.list;
    const std::size_t size = tab.tab.windows.size();
    Ui::OldDraw::DrawTheChosenRow(context, list, size, row, view.focusedList);
    static_cast<void>(
        DrawText(context, list.base.x + WindowTitleX, Ui::ListRowY(list, size, row), tab.tab.windows[at].title));
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// The start of 0x49cc20 and 0x49e7f0: with the normal blend, the black under the list, down to the bottom edge; then,
// the white with texture.
void DrawTheTabBottom(Render::SpriteBatch& sprites, const Ui::OldWindow& tab)
{
    sprites.SetBlend(Render::BlendMode::Alpha);
    SetThePalette(sprites, BlackIndex, 1.0f);
    const std::int32_t listHeight = tab.tab.list.base.height;
    sprites.ColorQuad(
        ToFloat(Ui::Steps::ClientX(tab, 0)), ToFloat(Ui::Steps::ClientY(tab, listHeight + BelowTheList)),
        ToFloat(Ui::Steps::ClientWidth(tab)),
        F32(static_cast<double>(Ui::Steps::ClientHeight(tab)) - static_cast<double>(listHeight) - BelowTheListCut));
    WhiteWithTexture(sprites);
}

// The tab's list (its vtable + 0x14) with the bar (+0x7c) and its rows (+0x80), and its buttons (0x45b2e0).
template <typename RowDraw>
void DrawTheTabList(const Ui::OldWindowDrawContext& context, Ui::OldWindow& tab, std::size_t size,
                    const Ui::OldWindowDrawView& view, const RowDraw& rowDraw)
{
    Ui::OldList& list = tab.tab.list;
    Ui::OldDraw::DrawTheOldList(
        context, list, size,
        Ui::OldDraw::OldListDrawing{.head = [&] { Ui::OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                    .row = [&](std::int32_t row, std::size_t at)
                                    { return rowDraw(context, tab, row, at, view); },
                                    .tail = {}});
    for (const Ui::OldButton& button : tab.tab.buttons)
        DrawTheButton(context, button, view);
}

// With the normal blend: the palette 2 line at y + 16, the columns' band (the 5, of 16) and the line over the buttons.
// The friends tab places the first with ClientY(0x10); the windows tab, with ClientY(0) + 16 in float: the same.
void DrawTheColumnBar(Render::SpriteBatch& sprites, const Ui::OldWindow& tab)
{
    sprites.SetBlend(Render::BlendMode::Alpha);
    const float left = ToFloat(Ui::Steps::ClientX(tab, 0));
    const float width = ToFloat(Ui::Steps::ClientWidth(tab));
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(tab, ColumnLineY)), width, 1.0f);
    SetThePalette(sprites, ColumnBarIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(tab, 0)), width, ColumnBarHeight);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(tab, tab.tab.list.base.height + ListLineBelow)), width, 1.0f);
}

// 0x49cf3c..0x49d099: a column's title, in white with the mouse over it or with the column's order (+0 of the model,
// 0x5763518), and otherwise with whatever color there is (the tab's buttons').
void DrawTheColumnTitle(const Ui::OldWindowDrawContext& context, const Ui::OldWindowDrawView& view, std::int32_t hitX,
                        std::int32_t hitY, std::int32_t hitWidth, bool lit, std::int32_t textX, std::int32_t textY,
                        std::string_view text)
{
    if (!Ui::OldDraw::MouseIn(view, hitX, hitY, hitWidth, ColumnHeight) && !lit)
    {
        static_cast<void>(DrawText(context, textX, textY, text));
        return;
    }
    context.textColors.text = LitColor;
    static_cast<void>(DrawText(context, textX, textY, text));
    context.textColors.text = DimColor;
}

// 0x465950(x, y, text, box, 0, 5, 0, 0x280) → 0x467060 with mode 5 (0x4670df..0x4670fd): with a box, the text from its
// column 0 in a box of that width, cut on the right; without a box (0 or less), its own width. Empty and without a box
// it draws nothing (0x467077..0x467098).
void DrawInABox(const Ui::OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                std::int32_t box)
{
    if (text.empty() && box == 0)
        return;
    const std::int32_t width = context.text.Measure(context.dcFont, text.empty() ? EmptyTextMeasure : text).width;
    context.text.DrawColumns(x, y, text, Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors},
                             0, box > 0 ? box : width);
}

// 0x465950(x, y, text, box, 0, 2, 0, 0x280) → 0x467060 with mode 2: the text centered in the box.
void DrawCenteredInABox(const Ui::OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                        std::int32_t box)
{
    if (text.empty() && box == 0)
        return;
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.dcFont, text, box);
    context.text.DrawColumns(x, y, text, Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors},
                             -placed.textColumn, placed.width);
}

// 0x4641f0, the letter list's vtable + 0x80 (0x463190, CL9 milestone 10), at its y (the + 0x9c, 0x464190, equal to
// 0x461c70): the row (DrawTheChosenRow), its checkbox (0x463090, checked with +0 at 1), the envelope, the name (strncpy
// of 0xff) in its column's box (+0x134), the date centered in its own (+0x138) and the subject (+0x13) in what is left
// up to the bar, with 0x465950 directly: the boxes in pixels (ftol(width · rx)). It ends without blend; returns 1.
std::int32_t DrawTheLetterRow(const Ui::OldWindowDrawContext& context, const Ui::OldWindow& tab, std::int32_t row,
                              std::size_t at, const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const Ui::OldList& list = tab.tab.list;
    const std::size_t size = tab.tab.letters.size();
    Ui::OldDraw::DrawTheChosenRow(context, list, size, row, view.focusedList);
    const LetterEntry& entry = tab.tab.letters[at];
    const std::int32_t x = list.base.x;
    const std::int32_t y = Ui::ListRowY(list, size, row);
    Ui::OldDraw::DrawTheCheckbox(sprites, x + LetterCheckboxX, y - 1, entry.selected == 1);
    if (entry.type == OpenedLetter)
        Ui::OldDraw::Quad(sprites, MailSlot, ToFloat(x + MailX), ToFloat(y - OpenedMailRise), MailWidth,
                          OpenedMailHeight, 0.0f, OpenedMailV, MailDu, OpenedMailDv);
    else
        Ui::OldDraw::Quad(sprites, MailSlot, ToFloat(x + MailX), ToFloat(y - 1), MailWidth, MailHeight, 0.0f, 0.0f,
                          MailDu, MailDv);
    const double rx = RatioX(sprites.Screen());
    const std::int32_t mark = list.headers;
    DrawInABox(context, x + mark + LetterTextGap, y, entry.name,
               Truncate(static_cast<double>(list.nameWidth) * rx - LetterBoxCut));
    // strncpy from +0x115: the date (+0x113) without its first two characters (*inferred* for a shorter one).
    const std::string date = entry.date.size() > DateSkip ? entry.date.substr(DateSkip) : std::string();
    DrawCenteredInABox(context, x + mark + list.nameWidth, y, date,
                       Truncate(static_cast<double>(list.placeWidth) * rx));
    const std::int32_t columns = mark + list.nameWidth + list.placeWidth;
    const std::int32_t rest = Truncate(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) -
                                       static_cast<double>(columns) - LetterBoxCut);
    DrawInABox(context, x + columns + LetterTextGap, y, entry.subject, Truncate(static_cast<double>(rest) * rx));
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x4a0a30, the letters tab's vtable + 0x14 (CL9 milestone 10; the placing at the start, the 8 to the list and to the
// four buttons and the 9 to the list, is in SettleTheFriendsForTheDraw): the black at the bottom, the list without
// blend (0x47eed0, 0x47a810's template with 0x138 elements, with bar 0x463db0, equal to 0x461890, and rows 0x4641f0),
// the columns' band with its three lines and its marks at the end of the mark, the name and the date, the all checkbox
// (+0x414) and the envelope, the column titles (in white the mouse's or that of [0x576351c]'s order) and, without
// blend, the four buttons.
void DrawTheLettersTab(const Ui::OldWindowManager& manager, Ui::OldWindow& tab, const Ui::OldWindowDrawContext& context,
                       const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheTabBottom(sprites, tab);
    sprites.SetBlend(Render::BlendMode::Opaque);
    Ui::OldList& list = tab.tab.list;
    const std::size_t size = tab.tab.letters.size();
    Ui::OldDraw::DrawTheOldList(
        context, list, size,
        Ui::OldDraw::OldListDrawing{.head = [&] { Ui::OldDraw::DrawTheListBar(sprites, list, size, view); },
                                    .row = [&](std::int32_t row, std::size_t at)
                                    { return DrawTheLetterRow(context, tab, row, at, view); },
                                    .tail = {}});
    DrawTheColumnBar(sprites, tab);
    // 0x4a0c53..0x4a0e70: FILD of x and FIADD of the sum of the previous columns, with the palette 2 line that the band
    // left.
    const std::int32_t left = Ui::Steps::ClientX(tab, 0);
    const std::array<std::int32_t, 3> ends{list.headers, list.headers + list.nameWidth,
                                           list.headers + list.nameWidth + list.placeWidth};
    const float separatorHeight =
        F32(static_cast<double>(Ui::Steps::ClientHeight(tab)) - SeparatorCut - SeparatorCutAgain);
    for (const std::int32_t end : ends)
        sprites.ColorQuad(ToFloat(left + end), ToFloat(Ui::Steps::ClientY(tab, SeparatorY)), 1.0f, separatorHeight);
    SetThePalette(sprites, ColumnTickIndex, 1.0f);
    for (const std::int32_t end : ends)
        sprites.ColorQuad(ToFloat(left + end), ToFloat(Ui::Steps::ClientY(tab, TickY)), 1.0f, TickHeight);
    WhiteWithTexture(sprites);
    context.textColors = Render::Text::TextColors{.text = DimColor, .background = 0};
    Ui::OldDraw::DrawTheCheckbox(sprites, Ui::Steps::ClientX(tab, LetterCheckboxX), Ui::Steps::ClientY(tab, TickY),
                                 tab.tab.allChecked == 1);
    Ui::OldDraw::Quad(sprites, MailSlot, ToFloat(Ui::Steps::ClientX(tab, MailX)),
                      ToFloat(Ui::Steps::ClientY(tab, TickY)), MailWidth, MailHeight, 0.0f, 0.0f, MailDu, MailDv);
    const std::int32_t top = Ui::Steps::ClientY(tab, 0);
    const std::int32_t textX = Ui::Steps::ClientX(tab, ColumnTextX);
    const std::int32_t textY = Ui::Steps::ClientY(tab, ColumnTextY);
    const std::int32_t sort = manager.letterModel.sortMode;
    DrawTheColumnTitle(context, view, left + ends[0], top, list.nameWidth, sort == BySenderLetters, textX + ends[0],
                       textY, TextAt(view, SenderColumnText));
    DrawTheColumnTitle(context, view, left + ends[1], top, list.placeWidth, sort == ByDateLetters, textX + ends[1],
                       textY, TextAt(view, DateColumnText));
    DrawTheColumnTitle(context, view, left + ends[2], top, list.subjectWidth, sort == BySubjectLetters, textX + ends[2],
                       textY, TextAt(view, SubjectColumnText));
    sprites.SetBlend(Render::BlendMode::Opaque);
    for (const Ui::OldButton& button : tab.tab.buttons)
        DrawTheButton(context, button, view);
}

// 0x49cc20, the friends tab's vtable + 0x14 (the placing at the start is in SettleTheFriendsForTheDraw).
void DrawTheFriendTab(const Ui::OldWindowManager& manager, Ui::OldWindow& tab, const Ui::OldWindowDrawContext& context,
                      const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheTabBottom(sprites, tab);
    sprites.SetBlend(Render::BlendMode::Opaque);
    DrawTheFriendList(context, tab.tab.list, tab.tab.friends, view);
    for (const Ui::OldButton& button : tab.tab.buttons)
        DrawTheButton(context, button, view);
    DrawTheColumnBar(sprites, tab);
    const Ui::OldList& list = tab.tab.list;
    const std::int32_t left = Ui::Steps::ClientX(tab, 0);
    const float separatorX = F32(static_cast<double>(ToFloat(left)) + static_cast<double>(list.nameWidth));
    sprites.ColorQuad(separatorX, ToFloat(Ui::Steps::ClientY(tab, SeparatorY)), 1.0f,
                      F32(static_cast<double>(Ui::Steps::ClientHeight(tab)) - SeparatorCut - SeparatorCutAgain));
    SetThePalette(sprites, ColumnTickIndex, 1.0f);
    sprites.ColorQuad(separatorX, ToFloat(Ui::Steps::ClientY(tab, TickY)), 1.0f, TickHeight);
    WhiteWithTexture(sprites);
    context.textColors.background = 0;
    const std::int32_t top = Ui::Steps::ClientY(tab, 0);
    const std::int32_t textY = Ui::Steps::ClientY(tab, ColumnTextY);
    const std::int32_t sort = manager.friendModel.sortMode;
    DrawTheColumnTitle(context, view, left, top, list.nameWidth, sort == ByNameSort,
                       Ui::Steps::ClientX(tab, ColumnTextX), textY, TextAt(view, NameColumnText));
    DrawTheColumnTitle(context, view, left + list.nameWidth, top, list.placeWidth, sort == ByStateSort,
                       Ui::Steps::ClientX(tab, ColumnTextX) + list.nameWidth, textY, TextAt(view, PlaceColumnText));
    sprites.SetBlend(Render::BlendMode::Opaque);
}

// 0x49e7f0, the windows tab's vtable + 0x14.
void DrawTheWindowsTab(Ui::OldWindow& tab, const Ui::OldWindowDrawContext& context, const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheTabBottom(sprites, tab);
    DrawTheTabList(context, tab, tab.tab.windows.size(), view, DrawTheWindowRow);
    DrawTheColumnBar(sprites, tab);
    WhiteWithTexture(sprites);
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.textColors = Render::Text::TextColors{.text = DimColor, .background = 0};
    static_cast<void>(DrawText(context, Ui::Steps::ClientX(tab, WindowColumnTextX),
                               Ui::Steps::ClientY(tab, ColumnTextY), TextAt(view, WindowColumnText)));
    sprites.SetBlend(Render::BlendMode::Opaque);
}

// ClientY(0) + ((24 − cy / ry) + 0.5) · 0.5, without truncating.
double HeaderTextTop(const Ui::OldWindow& window, const Render::Text::TextExtent& extent, Core::PixelSize screen)
{
    return static_cast<double>(Ui::Steps::ClientY(window, 0)) +
           ((HeaderTextHeight - static_cast<double>(extent.height) / RatioY(screen)) + Half) * Half;
}

// 0x4a2827..0x4a2a98: the three tabs' titles, if they have one, in white the one on view or under the mouse (+0xb4).
void DrawTheHeaderTitles(const Ui::OldWindow& window, const Ui::OldWindowDrawContext& context)
{
    const Core::PixelSize screen = context.sprites.Screen();
    for (std::size_t at = 0; at < FriendsTabCount; ++at)
    {
        const std::optional<std::string>& title = window.tabs[at].frame.title;
        if (!title)
            continue;
        const auto number = static_cast<std::int32_t>(at);
        const bool lit = window.friends.currentTab == number || window.friends.hoverTab == number;
        context.textColors.text = lit ? LitColor : DimColor;
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, *title);
        const std::int32_t y = Truncate(HeaderTextTop(window, extent, screen));
        const std::int32_t x =
            Truncate(static_cast<double>(Ui::Steps::ClientX(window, HeaderTextX[at])) +
                     ((HeaderTextWidth - static_cast<double>(extent.width) / RatioX(screen)) + Half) * Half);
        static_cast<void>(DrawText(context, x, y, *title));
    }
}

// 0x4a2a9b..0x4a2bc6: the state button's text against the right edge and its checkbox to the left.
void DrawTheStateButton(const Ui::OldWindowManager& manager, const Ui::OldWindow& window,
                        const Ui::OldWindowDrawContext& context, const Ui::OldWindowDrawView& view)
{
    const Core::PixelSize screen = context.sprites.Screen();
    const std::string label = TextAt(view, StateButtonText);
    context.textColors.text = DimColor;
    const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, label);
    const double right = static_cast<double>(Ui::Steps::ClientX(window, 0) + Ui::Steps::ClientWidth(window));
    const double textLeft = right - static_cast<double>(extent.width) / RatioX(screen) - StateTextInset;
    const double top = HeaderTextTop(window, extent, screen);
    static_cast<void>(DrawText(context, Truncate(textLeft), Truncate(top), label));
    // FSTP as float of the checkbox's x before subtracting 1.
    const float boxLeft = F32(textLeft - CheckboxGap);
    Ui::OldDraw::DrawTheCheckbox(context.sprites, Truncate(static_cast<double>(boxLeft) - One), Truncate(top - One),
                                 manager.offlineSent == 1);
}
} // namespace

std::int32_t DrawTheFriendRow(const Ui::OldWindowDrawContext& context, const Ui::OldList& list,
                              const std::deque<FriendEntry>& friends, std::int32_t row, std::size_t at,
                              const Ui::OldWindowDrawView& view)
{
    const std::size_t size = friends.size();
    Ui::OldDraw::DrawTheChosenRow(context, list, size, row, view.focusedList);
    const FriendEntry& entry = friends[at];
    const std::int32_t x = list.base.x + FriendNameX;
    const std::int32_t y = Ui::ListRowY(list, size, row);
    // 0x465950 directly, without 0x5542f0's cut for the empty text: empty, it also draws nothing.
    static_cast<void>(DrawText(context, x, y, entry.name));
    if (list.headers == 1)
        static_cast<void>(DrawText(context, list.nameWidth + x + PlaceGap, y, PlaceOf(view, entry.state)));
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

void DrawTheFriendList(const Ui::OldWindowDrawContext& context, Ui::OldList& list,
                       const std::deque<FriendEntry>& friends, const Ui::OldWindowDrawView& view)
{
    const std::size_t size = friends.size();
    Ui::OldDraw::DrawTheOldList(
        context, list, size,
        Ui::OldDraw::OldListDrawing{.head = [&] { Ui::OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                    .row = [&](std::int32_t row, std::size_t at)
                                    { return DrawTheFriendRow(context, list, friends, row, at, view); },
                                    .tail = {}});
}

Core::Expected<void, std::string> DrawTheFriendsWindow(Ui::OldWindowManager& manager, Ui::OldWindow& window,
                                                       const Ui::OldWindowDrawContext& context,
                                                       const Ui::OldWindowDrawView& view)
{
    if (auto settled = SettleTheFriendsForTheDraw(manager, window, view); !settled.HasValue())
        return settled;
    const Ui::OldWindowDrawView currentView = Ui::OldDraw::WithCurrentFocus(manager, view);
    const std::int32_t current = window.friends.currentTab;
    if (current == FriendTabNumber)
        DrawTheFriendTab(manager, window.tabs[FriendTabNumber], context, currentView);
    else if (current == WindowsTabNumber)
        DrawTheWindowsTab(window.tabs[WindowsTabNumber], context, currentView);
    else if (current == LettersTabNumber)
        DrawTheLettersTab(manager, window.tabs[LettersTabNumber], context, currentView);
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    SetThePalette(sprites, BlackIndex, 1.0f);
    const std::int32_t width = Ui::Steps::ClientWidth(window);
    sprites.ColorQuad(ToFloat(Ui::Steps::ClientX(window, 0)), ToFloat(Ui::Steps::ClientY(window, 0)), ToFloat(width),
                      HeaderBarHeight);
    Ui::OldDraw::DrawTheTabHeaders(sprites, Ui::Steps::ClientX(window, 0), Ui::Steps::ClientY(window, HeaderY),
                                   HeaderWidth, HeaderHeight, static_cast<std::int32_t>(FriendsTabCount), current);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(ToFloat(Ui::Steps::ClientX(window, HeaderLineX)),
                      ToFloat(Ui::Steps::ClientY(window, HeaderY) + HeaderLineBelow), ToFloat(width - HeaderLineX),
                      1.0f);
    WhiteWithTexture(sprites);
    context.textColors.background = 0;
    DrawTheHeaderTitles(window, context);
    DrawTheStateButton(manager, window, context, view);
    return {};
}

Core::Expected<void, std::string> DrawTheAddFriendBox(Ui::OldWindowManager& manager, Ui::OldWindow& box,
                                                      const Ui::OldWindowDrawContext& context,
                                                      const Ui::OldWindowDrawView& view)
{
    if (auto settled = SettleTheFriendsForTheDraw(manager, box, view); !settled.HasValue())
        return settled;
    const Ui::OldWindowDrawView currentView = Ui::OldDraw::WithCurrentFocus(manager, view);
    for (const Ui::OldButton& button : box.add.buttons)
        DrawTheButton(context, button, view);
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    Ui::OldEdit& edit = box.add.edit;
    Ui::OldDraw::DrawTheOldEdit(context, edit, currentView.focusedEdit != 0 && currentView.focusedEdit == edit.base.id,
                                currentView);
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace Client::Messenger
