#include "Ui/MessageArea.h"

#include "Core/PixelSize.h"
#include "Protocol/Events/Events.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/Frame/ITextDrawer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Ui
{
namespace
{
// 0x555190 and 0x554610 split from 256 pixels (cx < 0x100 and cx > 0xff).
constexpr int SplitWidth = 0x100;
// 0x555190 with the table full: leaves it at 0x77 and shifts the rows.
constexpr std::int32_t LastAreaRow = 0x77;
constexpr std::int32_t LastNotice = 5;
constexpr int RowHeight = 13;
// 0x5554e0 and 0x554810 end when the y passes 0x4d (6 rows) or when the pointer passes the sixth notice.
constexpr int LastAreaY = 0x4d;
// 0x5554e0: the y with Crywolf's state 3 or 4 (0x723c05).
constexpr int CrywolfOffset = 0x89;
// 0x554810: the first notice, and how much it rises with the chat log in its window.
constexpr int FirstNoticeY = 300;
constexpr int NoticeRise = 0x118;
constexpr int NoticeCenter = 0x140;
// The second half of a split row: 0x20202020, 0x20202020 and 0x2020 in the name.
constexpr std::string_view ContinuedName = "          ";

std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

std::string Bounded(std::string_view text, std::size_t bytes)
{
    return std::string(CString(text).substr(0, bytes));
}

// 0x555190: shifts rows 1..119 to 0..118 (name, text and type). Row 119 stays as it was.
void ShiftTheArea(MessageAreaState& area)
{
    for (std::size_t row = 0; row + 1 < MessageAreaRows; ++row)
        area.rows[row] = area.rows[row + 1];
}

// 0x554610: shifts notices 1..5 to 0..4 (the color and the text). Notice 5 stays as it was.
void ShiftTheNotices(MessageAreaState& area)
{
    for (std::size_t row = 0; row + 1 < NoticeRows; ++row)
        area.notices[row] = area.notices[row + 1];
}

void SetRow(MessageAreaState& area, std::int32_t index, std::string_view name, std::string_view text, std::int32_t type)
{
    MessageAreaRow& row = area.rows[static_cast<std::size_t>(index)];
    row.name = Bounded(name, MessageNameBytes);
    row.text = Bounded(text, MessageAreaTextBytes);
    row.type = type;
}

bool OnlyInTheList(std::string_view name, std::int32_t type)
{
    return !name.empty() && (type == WhisperMessage || type == ChatMessage || type == PartyMessage ||
                             type == GuildMessage || type == GuildNoticeMessage);
}

// 0x5554e0: type 6 and the others have no case and keep the previous text's colors.
void ColorsOf(std::int32_t type, Render::Text::TextColors& colors)
{
    switch (type)
    {
    case WhisperMessage:
        colors = {.text = 0xff000000u, .background = 0x9632c8ffu};
        break;
    case SystemMessage:
        colors = {.text = 0xffff9664u, .background = 0x96000000u};
        break;
    case ErrorMessage:
        colors = {.text = 0xff001effu, .background = 0x96000000u};
        break;
    case ChatMessage:
        colors = {.text = 0xffc8dce6u, .background = 0x96000000u};
        break;
    case PartyMessage:
        colors = {.text = 0xff000000u, .background = 0x96ffc800u};
        break;
    case GuildMessage:
        colors = {.text = 0xff000000u, .background = 0xc896ff00u};
        break;
    default:
        break;
    }
}

// 0x5554e0: "%s%s" (0x6c8f20) with two spaces at the start of the name, "%s" (0x6c8f30) with the name empty and
// "%s: %s" (0x6c8f28) otherwise.
std::string LineOf(const MessageAreaRow& row)
{
    if (row.name.size() >= 2 && row.name[0] == ' ' && row.name[1] == ' ')
        return row.name + row.text;
    if (row.name.empty())
        return row.text;
    return row.name + ": " + row.text;
}

// 0x77f31b8 and 0x77f31bc: ftol(cx / rx) and ftol(cy / ry).
void KeepTheSize(const MessageDrawContext& context, Render::Text::TextExtent extent)
{
    context.textWidth = Render::Text::DesignX(extent.width, context.sprites.Screen());
    context.textHeight = Render::Text::DesignY(extent.height, context.sprites.Screen());
}
} // namespace

HalvedText SplitInHalf(std::string_view text)
{
    const std::string_view source = CString(text);
    const int length = static_cast<int>(source.size());
    int split = 0;
    for (int at = 0; at < length; ++at)
    {
        split = at;
        if ((at >= length / 2 - 2 && source[static_cast<std::size_t>(at)] == ' ') || at >= length / 2 + 2)
            break;
        split = 0;
    }
    const auto cut = static_cast<std::size_t>(split);
    return HalvedText{.head = std::string(source.substr(0, cut)), .tail = std::string(source.substr(cut))};
}

void AddToTheMessageArea(MessageAreaState& area, const Render::Text::ITextMeasure& measure,
                         Render::Text::FontKind dcFont, std::string_view name, std::string_view text, std::int32_t type)
{
    name = CString(name);
    text = CString(text);
    area.clock = MessageClockSteps;
    if (OnlyInTheList(name, type))
        return;
    // sprintf("%s: %s") and GetTextExtentPointA on the DC, without selecting the font.
    const std::string line = std::string(name) + ": " + std::string(text);
    const Render::Text::TextExtent extent = measure.Measure(dcFont, line);
    std::int32_t index = area.count;
    if (area.count > LastAreaRow)
    {
        index = LastAreaRow;
        area.count = LastAreaRow;
        ShiftTheArea(area);
    }
    if (extent.width < SplitWidth)
        SetRow(area, index, name, text, type);
    else
    {
        const HalvedText halves = SplitInHalf(text);
        SetRow(area, area.count, name, halves.head, type);
        index = area.count + 1;
        if (index > LastAreaRow)
        {
            index = LastAreaRow;
            ShiftTheArea(area);
        }
        SetRow(area, index, ContinuedName, halves.tail, type);
    }
    area.clock = MessageClockSteps;
    area.count = index + 1;
}

void AddNotice(MessageAreaState& area, const Render::Text::ITextMeasure& measure, Render::Text::FontKind& dcFont,
               std::string_view text, std::uint8_t highlighted)
{
    text = CString(text);
    dcFont = Render::Text::FontKind::Bold;
    const Render::Text::TextExtent extent = measure.Measure(dcFont, text);
    if (area.noticeCount > LastNotice)
    {
        area.noticeCount = LastNotice;
        ShiftTheNotices(area);
    }
    std::int32_t index = area.noticeCount;
    area.notices[static_cast<std::size_t>(index)].highlighted = highlighted;
    std::string last(text);
    if (extent.width >= SplitWidth)
    {
        HalvedText halves = SplitInHalf(text);
        area.notices[static_cast<std::size_t>(area.noticeCount)].text = Bounded(halves.head, NoticeTextBytes);
        index = area.noticeCount + 1;
        if (index > LastNotice)
        {
            index = LastNotice;
            ShiftTheNotices(area);
        }
        last = std::move(halves.tail);
        area.notices[static_cast<std::size_t>(index)].highlighted = highlighted;
    }
    area.noticeCount = index + 1;
    area.noticeClock = NoticeClockSteps;
    area.notices[static_cast<std::size_t>(index)].text = Bounded(last, NoticeTextBytes);
}

void StepTheNoticeClock(MessageAreaState& area, const Render::Text::ITextMeasure& measure,
                        Render::Text::FontKind& dcFont)
{
    const bool expired = area.noticeClock < 1;
    --area.noticeClock;
    if (!expired)
        return;
    area.noticeClock = NoticeClockSteps;
    AddNotice(area, measure, dcFont, {}, 0);
}

void DrawTheMessageArea(const MessageAreaState& area, MessageFrameState& frame, const MessageDrawContext& context,
                        const MessageAreaView& view)
{
    context.sprites.SetColor(Render::Color{});
    context.dcFont = Render::Text::FontKind::Normal;
    const Core::PixelSize screen = context.sprites.Screen();
    for (int y = 0, shown = 0; y <= LastAreaY; y += RowHeight, ++shown)
    {
        const std::int32_t index = area.firstShown + shown;
        // 0x6c8eb4 is always 0x72: the original does not check the range.
        if (index < 0 || index >= static_cast<std::int32_t>(MessageAreaRows))
            continue;
        const MessageAreaRow& row = area.rows[static_cast<std::size_t>(index)];
        if (row.text.empty() || (!view.showChat && row.type == ChatMessage))
            continue;
        ColorsOf(row.type, context.textColors);
        const std::string line = LineOf(row);
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, line);
        frame.widths[static_cast<std::size_t>(index)] = extent.width;
        // The mouse over another's row, with the chat open: the text and the background swap three draws out of six.
        // The width goes to 640 with cx · 640 / W in unsigned 32 bits.
        const std::uint32_t right = screen.width > 0 ? static_cast<std::uint32_t>(extent.width) * 640u /
                                                           static_cast<std::uint32_t>(screen.width)
                                                     : static_cast<std::uint32_t>(extent.width);
        if (view.mouseX >= 0 && view.mouseX < static_cast<std::int32_t>(right) && y <= view.mouseY &&
            view.mouseY < y + RowHeight && view.chatOpen && view.heroSafeZone && row.name != view.heroName &&
            frame.areaBlink % 6 < 3)
        {
            const std::uint32_t background = context.textColors.background;
            context.textColors.background = context.textColors.text;
            context.textColors.text = background;
        }
        const int drawnY = view.crywolfStage == Protocol::Events::CrywolfReadyStage ||
                                   view.crywolfStage == Protocol::Events::CrywolfStartStage
                               ? y + CrywolfOffset
                               : y;
        if (!line.empty())
            context.text.Draw(0, drawnY, line,
                              Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors});
        KeepTheSize(context, context.text.Measure(context.dcFont, line));
    }
    ++frame.areaBlink;
}

void DrawTheNotices(const MessageAreaState& area, MessageFrameState& frame, const MessageDrawContext& context,
                    std::int32_t chatPlacement)
{
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    context.dcFont = Render::Text::FontKind::Bold;
    context.sprites.SetColor(Render::Color{});
    int y = FirstNoticeY;
    for (const NoticeRow& notice : area.notices)
    {
        context.textColors.background = 0x80000000u;
        if (notice.highlighted != 0)
            context.textColors.text = 0xffc8ff64u;
        else
            context.textColors.text = frame.noticeBlink % 10 > 4 ? 0xff50c8ffu : 0x8050c8ffu;
        const Render::Text::TextExtent extent = context.text.Measure(context.dcFont, notice.text);
        if (!notice.text.empty())
        {
            const int x = Render::Text::CenteredAt(NoticeCenter, extent, context.sprites.Screen());
            context.text.Draw(x, chatPlacement == 1 ? y - NoticeRise : y, notice.text,
                              Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors});
        }
        KeepTheSize(context, context.text.Measure(context.dcFont, notice.text));
        y += RowHeight;
    }
    ++frame.noticeBlink;
}
} // namespace Client::Ui
