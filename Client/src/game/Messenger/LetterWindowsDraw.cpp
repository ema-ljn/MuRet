#include "Messenger/LetterWindowsDraw.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/Frame/ITextDrawer.h"
#include "Chat/ChatLog.h"
#include "Messenger/FriendWindowsDraw.h"
#include "Messenger/LetterModel.h"
#include "Messenger/LetterWindows.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindowSteps.h"
#include "Ui/OldWindows.h"
#include "Crt/SprintfFormat.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Messenger
{
namespace
{
using Ui::OldDraw::DrawText;
using Ui::OldDraw::DrawTheButton;
using Ui::OldDraw::RatioX;
using Ui::OldDraw::SetThePalette;
using Ui::OldDraw::ToFloat;
using Ui::OldDraw::Truncate;
using Ui::OldDraw::WhiteWithTexture;

// 0x491960's indices.
constexpr std::int32_t EdgeIndex = 1;
constexpr std::int32_t LineIndex = 2;
constexpr std::int32_t BlackIndex = 7;
// 0x460550: row `row` at y − 13 · row − 0x10 (its y, the list's bottom edge), the name at x + 8 and the text after the
// name's width.
constexpr std::int32_t ChatRowHeight = 13;
constexpr std::int32_t ChatRowDrop = 0x10;
constexpr std::int32_t ChatNameX = 8;
constexpr std::string_view NameEnd = ": ";
// 0x460631 and 0x46089b: with 0x6c7a88 at 1, a background of 0 in the color (ignoring the alpha) stays at 0.
constexpr std::uint32_t BackgroundColorMask = 0xffffffu;
// 0x493fc0: the members show with more than 2 (their vtable + 0x54, 0x4720e0) or with the invite list (+0xb0 at 2);
// their lines, 0xa0 and 0x50 before the right edge, 16 (0x6b8f00) shorter than the inner area; the palette 2 line, 16
// above the bottom edge, and the black, the bottom 15 (0x6b87bc and 0x41700000).
constexpr std::int32_t MembersShown = 2;
constexpr std::int32_t WithInvites = 2;
constexpr std::int32_t InvitesSeparatorX = 0xa0;
constexpr std::int32_t MembersSeparatorX = 0x50;
constexpr double SeparatorCut = 16.0;
constexpr double BottomLineRise = 16.0;
constexpr double BottomBarRise = 15.0;
constexpr float BottomBarHeight = 15.0f;
// 0x491ba0: the palette 2 edges at x and x + 4 (0x6b8a44), and the palette 1 middle, 3 wide (0x40400000), at x + 1
// (0x6b87d4).
constexpr double SeparatorFarEdge = 4.0;
constexpr double SeparatorMiddleX = 1.0;
constexpr float SeparatorMiddleWidth = 3.0f;

// 0x465770: the body line at x + 10, in 0xffc8dce6 without background.
constexpr std::int32_t BodyTextX = 10;
constexpr std::uint32_t BodyTextColor = 0xffc8dce6u;
// 0x499e44..0x49a0f4: the portrait takes the right 0x78 (0x6b86c8, 120); at the bottom, the palette 2 line 20
// (0x6b86d0) from the edge and the black of 18 (0x41900000) from 19 (0x6b8870), and the side line of 19 (0x41980000);
// at the top, the palette 2 line at y + 0xe and the black of 14 (0x41600000).
constexpr std::int32_t ReadPortraitShown = 2;
constexpr std::int32_t PortraitX = 0x78;
constexpr double PortraitWidth = 120.0;
constexpr double ButtonsLineRise = 20.0;
constexpr double ButtonsBarRise = 19.0;
constexpr float ButtonsBarHeight = 18.0f;
constexpr float PortraitLineHeight = 19.0f;
constexpr std::int32_t HeaderLineY = 0xe;
constexpr float HeaderBarHeight = 14.0f;
// 0x49a10e..0x49a164: text 1014 at (x + 3, y + 3).
constexpr std::size_t ReadHeaderText = 1014;
constexpr std::int32_t ReadHeaderX = 3;
constexpr std::int32_t ReadHeaderY = 3;

// 0x498537..0x4986e8: at the top, the black of 29 (0x41e80000) with the palette 2 lines at y + 0xe and y + 0x1d; at
// the bottom, the black of 19 (0x41980000) from 19 (0x6b8870) above the edge and its line.
constexpr float WriteHeaderHeight = 29.0f;
constexpr std::int32_t WriteToLineY = 0xe;
constexpr std::int32_t WriteSubjectLineY = 0x1d;
constexpr double WriteBottomRise = 19.0;
constexpr float WriteBottomHeight = 19.0f;
// 0x4986f5..0x49876d: the 1000 and the 1005 at x + 3 and at y + 3 and y + 0x12, in 0xffc8dce6, in the box of the
// 1000's width.
constexpr std::uint32_t WriteTextColor = 0xffc8dce6u;
constexpr std::size_t ToText = 1000;
constexpr std::size_t SubjectText = 1005;
constexpr std::int32_t WriteLabelX = 3;
constexpr std::int32_t ToLabelY = 3;
constexpr std::int32_t SubjectLabelY = 0x12;
// 0x4987c7: buttons +0xb54 and +0xbdc, with +0xb0 at 1.
constexpr std::int32_t WithThePortrait = 1;
// +0xa44, +0xacc, +0xb54 and +0xbdc (0x48f4ee).
constexpr std::size_t WriteButtonCount = 4;
// 0x4670ba..0x4670d0: an empty text measures 0x6c7ae4's "0".
constexpr std::string_view EmptyTextMeasure = "0";

// 0x4a5490 and 0x4a5750: the 0x491960 indices of the background (the shadow, with its 179), the edge, the chosen one
// and the waiting ones; the row's margin (0x6b8a44), its band (1 lower, 3 more, 0x6b86b8, from x + 1 and 2 narrower)
// and the text at x + 2 and 3 lower; the text's alpha, ftol(blink · 255) (0x6b9f98), over 0xc8dce6 or over black.
// 0x4a5120: the bar's icons. win_letter.jpg (0x64d) of 15 × 9 with 0.9375 × 0.5625; win_man.jpg (0x64e) of 25 × 14
// with 0.78125 × 0.4375 (with the friends window, from v = 0.4375); win_push.jpg (0x64f) with w · 1/64 and h · 1/32
// (0x6ba064 and 0x6b8880).
constexpr std::uint16_t LetterSlot = 0x64d;
constexpr std::uint16_t ManSlot = 0x64e;
constexpr std::uint16_t PushSlot = 0x64f;
constexpr float LetterWidth = 15.0f;
constexpr float LetterHeight = 9.0f;
constexpr float LetterDu = 0.9375f;
constexpr float LetterDv = 0.5625f;
constexpr float ManWidth = 25.0f;
constexpr float ManHeight = 14.0f;
constexpr float ManDu = 0.78125f;
constexpr float ManDv = 0.4375f;
constexpr float PushDu = 0.015625f;
constexpr float PushDv = 0.03125f;
constexpr std::int32_t BarBlinkPeriod = 0x18;
constexpr std::int32_t BarBlinkLit = 0xc;
constexpr std::int32_t LetterBlinkStep = 0xb;
constexpr std::int32_t LetterBlinks = 5;
constexpr std::int32_t ListBackIndex = 3;
constexpr std::int32_t ListEdgeIndex = 15;
constexpr std::int32_t ChosenChatIndex = 0x10;
constexpr std::int32_t WaitingChatIndex = 0x11;
constexpr double ChatRowMargin = 4.0;
constexpr double ChatBandDrop = 1.0;
constexpr double ChatBandExtra = 3.0;
constexpr std::int32_t ChatBandX = 1;
constexpr std::int32_t ChatBandCut = 2;
constexpr std::int32_t ChatTextX = 2;
constexpr double ChatTextDrop = 3.0;
constexpr double TextAlphaScale = 255.0;
constexpr std::uint32_t ChatRowColor = 0xc8dce6u;
constexpr std::uint32_t AlphaShift = 24;
constexpr std::uint32_t ByteMask = 0xffu;
// 0x4a5943..0x4a5a68: strncpy of 0x100 of the title, strtok with "," (0x6c7b88) and the rest in w − 8, one line.
constexpr std::size_t ChatTitleCopy = 0x100;
constexpr char TitleDelimiter = ',';
constexpr std::int32_t ChatTextCut = 8;
// 0x4a5ac2..0x4a5b5a: with more than two members, win_mark.tga of 4 × 6 (0, 0, 0.5, 0.75) at x + w − 7 (0x6b8878) and
// 5 lower (0x6b87f8) than the row, with glColor3f(255, 0, 0) and then glColor3f(255, 255, 255).
constexpr std::int32_t ManyMembers = 2;
constexpr std::uint16_t MarkSlot = 0x64c;
constexpr double MarkInset = 7.0;
constexpr double MarkDrop = 5.0;
constexpr float MarkWidth = 4.0f;
constexpr float MarkHeight = 6.0f;
constexpr float MarkDu = 0.5f;
constexpr float MarkDv = 0.75f;
constexpr float FullColor = 255.0f;

using Math::F32;

// 0x46057d..0x460631 with 0x5763510 + 8 at 0 (this main.exe's, like the chat record of 0x45cbb0): the name's colors by
// the kind (+0x110); 0x460550 has no case for 6 or more and the previous ones stay.
void ChatNameColors(std::int32_t kind, Render::Text::TextColors& colors)
{
    switch (kind)
    {
    case 0:
        colors = {.text = 0xff000000u, .background = 0x32c8ffu};
        break;
    case 1:
        colors = {.text = 0xffff9664u, .background = 0};
        break;
    case 2:
        colors = {.text = 0xff001effu, .background = 0};
        break;
    case 3:
        colors = {.text = 0xffefdccdu, .background = 0};
        break;
    case 4:
        colors = {.text = 0xff000000u, .background = 0xffc800u};
        break;
    case 5:
        colors = {.text = 0xff000000u, .background = 0x96ff00u};
        break;
    default:
        break;
    }
}

// 0x460819..0x46089b: the text's, with 1 and 3 lighter.
void ChatTextColors(std::int32_t kind, Render::Text::TextColors& colors)
{
    if (kind == 1)
        colors = {.text = 0xff46a5d2u, .background = 0};
    else if (kind == 3)
        colors = {.text = 0xffc8dce6u, .background = 0};
    else
        ChatNameColors(kind, colors);
}

// 0x460550, the vtable + 0x80 of the chat's lines (0x45f240, vtable 0x6b9030) with 0x5763510 + 8 at 0: with the
// normal blend, the name with ": " (sprintf "%s: ", 0x6c7aa8) and its width in 640 × 480 (GetTextExtentPointA with the
// DC, ftol(cx / rx)), and the text (sprintf "%s") after it, with 0x465950 directly. Returns 1.
std::int32_t DrawTheChatRow(const Ui::OldWindowDrawContext& context, const Ui::OldList& list, const Ui::ChatLine& line,
                            std::int32_t row, const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    const std::int32_t y = list.base.y - row * ChatRowHeight - ChatRowDrop;
    std::int32_t nameWidth = 0;
    if (!line.name.empty())
    {
        ChatNameColors(line.kind, context.textColors);
        const std::string name = line.name + std::string(NameEnd);
        static_cast<void>(DrawText(context, list.base.x + ChatNameX, y, name));
        nameWidth =
            Truncate(static_cast<double>(context.text.Measure(context.dcFont, name).width) / RatioX(sprites.Screen()));
    }
    ChatTextColors(line.kind, context.textColors);
    if (view.chatPlacement == 1 && (context.textColors.background & BackgroundColorMask) == 0)
        context.textColors.background = 0;
    static_cast<void>(DrawText(context, list.base.x + nameWidth + ChatNameX, y, line.text));
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x45f9c0, the lines' vtable + 0x14 (0x47a810 with the chat record's 0x11c elements): the bar (0x45fac0, equal to
// 0x461890) and the lines. With +0xc8 at 1 (0x45f240) the list shows its second deque (+0xcc), which 0x460130 fills
// with push_front: the first is the last piece of the newest line, split at the list's width minus 0x1e (0x459c70).
// The iterator runs to the end of that deque (+0xe8, 0x45f9fd).
void DrawTheChatLines(const Ui::OldWindowDrawContext& context, Ui::OldChatPart& chat, const Ui::OldWindowDrawView& view)
{
    Ui::OldList& list = chat.lines;
    const std::size_t size = chat.wrappedRows.size();
    Ui::OldDraw::DrawTheOldList(
        context, list, size,
        Ui::OldDraw::OldListDrawing{.head = [&] { Ui::OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                    .row = [&](std::int32_t row, std::size_t at)
                                    { return DrawTheChatRow(context, list, chat.wrappedRows[at], row, view); },
                                    .tail = {}});
}

// 0x491ba0(x, y, h): the line between two lists, without changing the blend: the palette 2 edges and the palette 1
// middle.
void DrawTheListSeparator(Render::SpriteBatch& sprites, float x, float y, float height)
{
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(x, y, 1.0f, height);
    sprites.ColorQuad(F32(static_cast<double>(x) + SeparatorFarEdge), y, 1.0f, height);
    SetThePalette(sprites, EdgeIndex, 1.0f);
    sprites.ColorQuad(F32(static_cast<double>(x) + SeparatorMiddleX), y, SeparatorMiddleWidth, height);
}

// The line `fromRight` from the inner area's right edge: FILD of x + w − fromRight and of y, and h − 16 in float.
void DrawTheMembersSeparator(Render::SpriteBatch& sprites, const Ui::OldWindow& chat, std::int32_t fromRight)
{
    DrawTheListSeparator(sprites, ToFloat(Ui::Steps::ClientX(chat, 0) + Ui::Steps::ClientWidth(chat) - fromRight),
                         ToFloat(Ui::Steps::ClientY(chat, 0)),
                         F32(static_cast<double>(Ui::Steps::ClientHeight(chat)) - SeparatorCut));
}

// A quad's y from the bottom edge: FILD h, FADD of y in float and FSUB.
float FromTheBottom(const Ui::OldWindow& window, double rise)
{
    return F32(static_cast<double>(Ui::Steps::ClientHeight(window)) +
               static_cast<double>(ToFloat(Ui::Steps::ClientY(window, 0))) - rise);
}

std::string TextAt(const Ui::OldWindowDrawView& view, std::size_t index)
{
    return view.texts != nullptr ? view.texts->At(index) : std::string();
}

// Whether window `id` of the list is minimized (0x490a00 and 0x45a9c0 with 5). One that does not exist, which the
// original does not see (it reads that of 0), is not.
bool IsMinimized(Ui::OldWindowManager& manager, std::int32_t id)
{
    const Ui::OldWindow* window = Ui::FindTheOldWindow(manager, id);
    return window != nullptr && window->base.state == Ui::MinimizedWindow;
}

// 0x4a5943..0x4a5a68: the text of a chat's row, from its window's title (+0x98, strncpy of 0x100): strtok(","), the
// first piece without the leading commas; if it is longer than the 994, what follows the 994 (and the 995 if it starts
// with it) cut by 0x459c70 at w − 8 and one line (of 0x40 bytes: MuRet's titles do not reach it); otherwise, the 995.
// Without a piece (a title of commas, which the original does not see: strlen of NULL) or past the piece's end, MuRet
// reads nothing (*inferred*).
Core::Expected<std::string, std::string> TextOfTheChatRow(const Ui::OldWindow* chat, const Ui::OldWindow& window,
                                                          const Ui::OldWindowDrawContext& context,
                                                          const Ui::OldWindowDrawView& view)
{
    std::string title = chat != nullptr ? chat->frame.title.value_or(std::string()) : std::string();
    title = title.substr(0, std::min(title.find('\0'), ChatTitleCopy));
    const std::size_t from = std::min(title.find_first_not_of(TitleDelimiter), title.size());
    const std::string_view token = std::string_view(title).substr(from, title.find(TitleDelimiter, from) - from);
    const std::string chatting = TextAt(view, ChatTitleText);
    const std::string closed = TextAt(view, ChatClosedText);
    if (token.size() <= chatting.size())
        return closed;
    const std::size_t skip = token.starts_with(closed) ? closed.size() + chatting.size() : chatting.size();
    auto wrapped = Chat::WrapByWidth(token.substr(std::min(skip, token.size())), context.text, context.dcFont,
                                     context.sprites.Screen(), window.base.width - ChatTextCut, 1, 0);
    if (!wrapped.HasValue())
        return Core::Unexpected(std::move(wrapped).Error());
    return wrapped.Value().lines.empty() ? std::string() : wrapped.Value().lines.front();
}

// 0x4a5750, from 0x4a5490 with the textured white: the chats' rows from bottom to top, the n of (row + 4) · n over
// +0x104 (a minimized one does not count).
Core::Expected<void, std::string> DrawTheChatRowsOfTheList(Ui::OldWindowManager& manager, const Ui::OldWindow& window,
                                                           const Ui::OldWindowDrawContext& context,
                                                           const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    const Ui::OldWindowListPart& list = window.list;
    const Ui::OldWindowBase& base = window.base;
    const double step = static_cast<double>(list.rowHeight) + ChatRowMargin;
    const double bottom = static_cast<double>(list.bottom);
    std::int32_t row = 1;
    for (std::size_t at = 0; at < list.chats.size(); ++at, ++row)
    {
        const std::int32_t id = list.chats[at];
        if (IsMinimized(manager, id))
        {
            --row;
            continue;
        }
        const auto alpha =
            static_cast<std::uint32_t>(Truncate(static_cast<double>(list.blink) * TextAlphaScale)) & ByteMask;
        bool waiting = false;
        for (const std::int32_t other : list.waiting)
            if (other == id)
                waiting = true;
        const bool chosen = list.cursor == at;
        const double rows = static_cast<double>(row);
        std::uint32_t color = (alpha << AlphaShift) + ChatRowColor;
        if (chosen || waiting)
        {
            sprites.SetBlend(Render::BlendMode::Alpha);
            SetThePalette(sprites, chosen ? ChosenChatIndex : WaitingChatIndex, list.blink);
            sprites.ColorQuad(ToFloat(base.x + ChatBandX), F32(bottom - step * rows + ChatBandDrop),
                              ToFloat(base.width - ChatBandCut),
                              F32(static_cast<double>(list.rowHeight) + ChatBandExtra));
            WhiteWithTexture(sprites);
            color = alpha << AlphaShift;
        }
        context.textColors = Render::Text::TextColors{.text = color, .background = 0};
        const Ui::OldWindow* chat = Ui::FindTheOldWindow(manager, id);
        auto text = TextOfTheChatRow(chat, window, context, view);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        // FILD of n and FSTP as float: the same n.
        static_cast<void>(DrawText(
            context, base.x + ChatTextX,
            Truncate(bottom - step * static_cast<double>(static_cast<float>(row)) + ChatTextDrop), text.Value()));
        if (chat == nullptr || Ui::ListCount(chat->chat.users, chat->chat.members.size()) <= ManyMembers)
            continue;
        sprites.SetColor(Render::Color{.red = FullColor, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f});
        Ui::OldDraw::Quad(sprites, MarkSlot, F32(static_cast<double>(base.width + base.x) - MarkInset),
                          F32(bottom - step * rows + MarkDrop), MarkWidth, MarkHeight, 0.0f, 0.0f, MarkDu, MarkDv);
        sprites.SetColor(Render::Color{.red = FullColor, .green = FullColor, .blue = FullColor, .alpha = 1.0f});
    }
    return {};
}

// 0x5542f0(x, y, text, box, 3, 0) → 0x465950 → 0x467060 with mode 3 (0x467134..0x467144): TextOutA puts the text at
// column box − cx of the DIB, which is box wide: right-aligned in the box, cut on the left if wider. Without text and
// without a box it draws nothing (0x5542f8..0x55430f); empty and with a box, the background only.
void DrawRightAligned(const Ui::OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                      std::int32_t box)
{
    if (text.empty() && box == 0)
        return;
    const std::int32_t width = context.text.Measure(context.dcFont, text.empty() ? EmptyTextMeasure : text).width;
    context.text.DrawColumns(x, y, text, Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors},
                             width - box, box);
}

// 0x465770, the body list's vtable + 0x80 (0x464620, vtable 0x6b93d4): with the normal blend, the line (sprintf "%s",
// 0x6c7084) at x + 10 and at its y (0x465710, equal to 0x461c70), with 0x465950 directly. With 0x6c7a88 at 1 it sets
// the background to 0 again, which is already 0 (0x4657a5..0x4657be). Returns 1.
std::int32_t DrawTheBodyRow(const Ui::OldWindowDrawContext& context, const Ui::OldList& list, std::size_t size,
                            const std::string& line, std::int32_t row)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.textColors = Render::Text::TextColors{.text = BodyTextColor, .background = 0};
    static_cast<void>(DrawText(context, list.base.x + BodyTextX, Ui::ListRowY(list, size, row), line));
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x464d90, the body's vtable + 0x14 (0x47a810 with 0x3f0 elements): the bar (0x464e90, equal to 0x461890) and the
// lines. Like the chat's lines, it shows its second deque, which 0x465450 fills with push_front, wrapped.
void DrawTheBodyLines(const Ui::OldWindowDrawContext& context, Ui::OldLetterPart& letter,
                      const Ui::OldWindowDrawView& view)
{
    Ui::OldList& list = letter.lines;
    const std::size_t size = letter.wrappedRows.size();
    Ui::OldDraw::DrawTheOldList(
        context, list, size,
        Ui::OldDraw::OldListDrawing{.head = [&] { Ui::OldDraw::DrawTheListBar(context.sprites, list, size, view); },
                                    .row = [&](std::int32_t row, std::size_t at)
                                    { return DrawTheBodyRow(context, list, size, letter.wrappedRows[at], row); },
                                    .tail = {}});
}

// What the lines' 9 measures when wrapping them again during the drawing: GetTextExtentPointA over the DC, with
// whatever font it has at that moment.
Ui::OldWindowMeasure MeasureOfTheDraw(const Ui::OldWindowDrawContext& context)
{
    return Ui::OldWindowMeasure{.measure = context.text, .dcFont = context.dcFont, .screen = context.sprites.Screen()};
}
} // namespace

Core::Expected<void, std::string> DrawTheChatWindow(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                    const Ui::OldWindowDrawContext& context,
                                                    const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto settled = SettleTheLetterKindsForTheDraw(manager, chat, MeasureOfTheDraw(context)); !settled.HasValue())
        return settled;
    Ui::OldChatPart& part = chat.chat;
    DrawTheChatLines(context, part, view);
    if (Ui::ListCount(part.users, part.members.size()) > MembersShown || part.mode >= WithInvites)
    {
        DrawTheFriendList(context, part.users, part.members, view);
        if (part.mode >= WithInvites)
        {
            DrawTheFriendList(context, part.invite, part.invitable, view);
            sprites.SetBlend(Render::BlendMode::Alpha);
            DrawTheMembersSeparator(sprites, chat, InvitesSeparatorX);
        }
    }
    if (Ui::ListCount(part.users, part.members.size()) > MembersShown || part.mode >= WithInvites)
        DrawTheMembersSeparator(sprites, chat, MembersSeparatorX);
    sprites.SetBlend(Render::BlendMode::Alpha);
    const float left = ToFloat(Ui::Steps::ClientX(chat, 0));
    const float width = ToFloat(Ui::Steps::ClientWidth(chat));
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(chat, BottomLineRise), width, 1.0f);
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(chat, BottomBarRise), width, BottomBarHeight);
    WhiteWithTexture(sprites);
    DrawTheButton(context, part.inviteButton, view);
    Ui::OldEdit& edit = part.edit;
    Ui::OldDraw::DrawTheOldEdit(context, edit, view.focusedEdit != 0 && view.focusedEdit == edit.base.id, view);
    if (part.mode >= WithInvites)
        DrawTheButton(context, part.sendButton, view);
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

Core::Expected<void, std::string> DrawTheReadWindow(Ui::OldWindowManager& manager, Ui::OldWindow& read,
                                                    const Ui::OldWindowDrawContext& context,
                                                    const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto settled = SettleTheLetterKindsForTheDraw(manager, read, MeasureOfTheDraw(context)); !settled.HasValue())
        return settled;
    Ui::OldLetterPart& letter = read.letter;
    DrawTheBodyLines(context, letter, view);
    const float left = ToFloat(Ui::Steps::ClientX(read, 0));
    const std::int32_t width = Ui::Steps::ClientWidth(read);
    if (letter.portraitMode >= ReadPortraitShown)
    {
        const float toThePortrait = F32(static_cast<double>(width) - PortraitWidth);
        SetThePalette(sprites, LineIndex, 1.0f);
        sprites.ColorQuad(ToFloat(Ui::Steps::ClientX(read, 0) + width - PortraitX), FromTheBottom(read, ButtonsBarRise),
                          1.0f, PortraitLineHeight);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsLineRise), toThePortrait, 1.0f);
        SetThePalette(sprites, BlackIndex, 1.0f);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsBarRise), toThePortrait, ButtonsBarHeight);
    }
    else
    {
        SetThePalette(sprites, LineIndex, 1.0f);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsLineRise), ToFloat(width), 1.0f);
        SetThePalette(sprites, BlackIndex, 1.0f);
        sprites.ColorQuad(left, FromTheBottom(read, ButtonsBarRise), ToFloat(width), ButtonsBarHeight);
    }
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(read, HeaderLineY)), ToFloat(width), 1.0f);
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(read, 0)), ToFloat(width), HeaderBarHeight);
    WhiteWithTexture(sprites);
    // sprintf(1014, +0x780, +0x88b, +0x89b) with whatever colors there are: those of the body's last line.
    const LetterEntry& entry = letter.entry;
    static_cast<void>(DrawText(context, Ui::Steps::ClientX(read, ReadHeaderX), Ui::Steps::ClientY(read, ReadHeaderY),
                               Crt::Sprintf(TextAt(view, ReadHeaderText), entry.name, entry.date, entry.time)));
    for (const Ui::OldButton& button : letter.buttons)
        DrawTheButton(context, button, view);
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}

Core::Expected<void, std::string> DrawTheWriteWindow(Ui::OldWindowManager& manager, Ui::OldWindow& write,
                                                     const Ui::OldWindowDrawContext& context,
                                                     const Ui::OldWindowDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto settled = SettleTheLetterKindsForTheDraw(manager, write, MeasureOfTheDraw(context)); !settled.HasValue())
        return settled;
    Ui::OldLetterPart& letter = write.letter;
    const float left = ToFloat(Ui::Steps::ClientX(write, 0));
    const float width = ToFloat(Ui::Steps::ClientWidth(write));
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(write, 0)), width, WriteHeaderHeight);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(write, WriteToLineY)), width, 1.0f);
    sprites.ColorQuad(left, ToFloat(Ui::Steps::ClientY(write, WriteSubjectLineY)), width, 1.0f);
    SetThePalette(sprites, BlackIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(write, WriteBottomRise), width, WriteBottomHeight);
    SetThePalette(sprites, LineIndex, 1.0f);
    sprites.ColorQuad(left, FromTheBottom(write, WriteBottomRise), width, 1.0f);
    WhiteWithTexture(sprites);
    // Only the text color (0x6c8e5c): the background stays whatever it is.
    context.textColors.text = WriteTextColor;
    // GetTextExtentPointA of the 1000 with the DC (0x498702..0x49871b): the box for both.
    const std::string to = TextAt(view, ToText);
    const std::int32_t box = context.text.Measure(context.dcFont, to).width;
    DrawRightAligned(context, Ui::Steps::ClientX(write, WriteLabelX), Ui::Steps::ClientY(write, ToLabelY), to, box);
    DrawRightAligned(context, Ui::Steps::ClientX(write, WriteLabelX), Ui::Steps::ClientY(write, SubjectLabelY),
                     TextAt(view, SubjectText), box);
    // 0x498772..0x4987a1: the vtable + 0x14 (0x468320) of the name, the subject and the body, with the normal blend.
    const auto drawTheEdit = [&](Ui::OldEdit& edit)
    { Ui::OldDraw::DrawTheOldEdit(context, edit, view.focusedEdit != 0 && view.focusedEdit == edit.base.id, view); };
    drawTheEdit(letter.to);
    drawTheEdit(letter.subject);
    drawTheEdit(letter.text);
    sprites.SetBlend(Render::BlendMode::Opaque);
    if (letter.buttons.size() != WriteButtonCount)
        return {};
    DrawTheButton(context, letter.buttons[0], view);
    DrawTheButton(context, letter.buttons[1], view);
    if (letter.withPortrait != WithThePortrait)
        return {};
    DrawTheButton(context, letter.buttons[2], view);
    DrawTheButton(context, letter.buttons[3], view);
    return {};
}

void DrawTheFriendBarIcons(Ui::OldWindowManager& manager, Render::SpriteBatch& sprites)
{
    Ui::OldWindow& windowList = manager.windowList;
    Ui::OldWindowListPart& list = windowList.list;
    // 0x4a512f..0x4a5146: signed IDIV and SETL.
    const bool lit = list.barBlink % BarBlinkPeriod < BarBlinkLit;
    // The += of +0x148 on every exit; in 32 bits, like the original.
    const auto nextDraw = [&list]
    { list.barBlink = static_cast<std::int32_t>(static_cast<std::uint32_t>(list.barBlink) + 1u); };
    const std::int32_t x = windowList.base.x;
    const std::int32_t y = list.bottom;
    const bool waiting = !list.waiting.empty();
    const auto unread = [&manager]
    {
        return std::ranges::any_of(manager.letterModel.letters,
                                   [](const LetterEntry& letter) { return letter.type == 0; });
    };
    std::int32_t letterX = 0;
    std::int32_t letterY = 0;
    if (Ui::FindTheOldWindow(manager, manager.friends) != nullptr)
    {
        // FILD and FMUL of the floats: w / 64 and h / 32, exact.
        const float width = ToFloat(windowList.base.width);
        const float height = ToFloat(list.rowStep);
        Ui::OldDraw::Quad(sprites, PushSlot, ToFloat(x), ToFloat(y), width, height, 0.0f, 0.0f, width * PushDu,
                          height * PushDv);
        if (waiting && lit)
            Ui::OldDraw::Quad(sprites, ManSlot, ToFloat(x + 7), ToFloat(y + 3), ManWidth, ManHeight, 0.0f, ManDv, ManDu,
                              ManDv);
        letterX = x + 0x24;
        letterY = y + 7;
        if (list.newLetter == 0)
        {
            if (unread())
                Ui::OldDraw::Quad(sprites, LetterSlot, ToFloat(letterX), ToFloat(letterY), LetterWidth, LetterHeight,
                                  0.0f, 0.0f, LetterDu, LetterDv);
            nextDraw();
            return;
        }
        // 0x4a5230: with no unread one, 0x4a6420(0); it draws anyway.
        if (!unread())
            list.newLetter = 0;
    }
    else
    {
        if (waiting && lit)
            Ui::OldDraw::Quad(sprites, ManSlot, ToFloat(x + 4), ToFloat(y + 2), ManWidth, ManHeight, 0.0f, 0.0f, ManDu,
                              ManDv);
        letterX = x + 0x21;
        letterY = y + 5;
        if (list.newLetter == 0)
        {
            if (unread())
                Ui::OldDraw::Quad(sprites, LetterSlot, ToFloat(letterX), ToFloat(letterY), LetterWidth, LetterHeight,
                                  0.0f, 0.0f, LetterDu, LetterDv);
            nextDraw();
            return;
        }
    }
    if (!lit)
    {
        nextDraw();
        return;
    }
    Ui::OldDraw::Quad(sprites, LetterSlot, ToFloat(letterX), ToFloat(letterY), LetterWidth, LetterHeight, 0.0f, 0.0f,
                      LetterDu, LetterDv);
    if (list.barBlink % BarBlinkPeriod == LetterBlinkStep)
        ++list.letterBlinks;
    if (list.letterBlinks > LetterBlinks)
    {
        list.letterBlinks = 0;
        list.newLetter = 0;
    }
    nextDraw();
}

Core::Expected<void, std::string> DrawTheChatsInTheList(Ui::OldWindowManager& manager, const Ui::OldWindow& window,
                                                        const Ui::OldWindowDrawContext& context,
                                                        const Ui::OldWindowDrawView& view)
{
    const Ui::OldWindowListPart& list = window.list;
    if (list.chats.empty() || window.base.state != Ui::ShownWindow)
        return {};
    std::int32_t count = 0;
    for (const std::int32_t id : list.chats)
        if (!IsMinimized(manager, id))
            ++count;
    if (count == 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    // ftol((row + 4) · count), and the height in float (FILD and FSTP).
    const std::int32_t height =
        Truncate((static_cast<double>(list.rowHeight) + ChatRowMargin) * static_cast<double>(count));
    const float tall = ToFloat(height);
    const Ui::OldWindowBase& base = window.base;
    const float x = ToFloat(base.x);
    const float top = ToFloat(list.bottom - height);
    const float width = ToFloat(base.width);
    SetThePalette(sprites, ListBackIndex, list.blink);
    sprites.ColorQuad(x, top, width, tall);
    SetThePalette(sprites, ListEdgeIndex, list.blink);
    sprites.ColorQuad(x, top, width, 1.0f);
    sprites.ColorQuad(x, top, 1.0f, tall);
    sprites.ColorQuad(ToFloat(base.width + base.x - 1), top, 1.0f, tall);
    sprites.ColorQuad(x, ToFloat(list.bottom), width, 1.0f);
    WhiteWithTexture(sprites);
    return DrawTheChatRowsOfTheList(manager, window, context, view);
}

Core::Expected<void, std::string> DrawTheLetterPortrait(Ui::OldWindow& window, const Ui::OldWindowDrawView& view)
{
    std::string_view where;
    if (window.kind == Ui::OldWindowKind::LetterReadWindow && window.letter.portraitMode >= ReadPortraitShown)
        where = "0x49a1d0";
    else if (window.kind == Ui::OldWindowKind::LetterWriteWindow && window.letter.withPortrait == 1)
        where = "0x498800";
    else
        return {};
    if (!view.portrait)
        return Core::Unexpected(std::format("{}: el dibujo del retrato de las cartas (su vtable + 0x14, 0x497c10) sin "
                                            "el juego",
                                            where));
    return view.portrait(window.letter.portrait);
}
} // namespace Client::Messenger
