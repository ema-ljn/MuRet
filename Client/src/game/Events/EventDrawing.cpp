#include "Events/EventDrawing.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Events/EventGlobals.h"
#include "Events/EventMapObject.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Hud/HudText.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Crt/OriginalFormat.h"
#include "Math/Ftol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Events
{
namespace
{
using Render::Text::FontKind;
using Result = Core::Expected<void, std::string>;

// 0x5a7eb9..0x5a7f16: the F3 22 word in minutes and seconds (IDIV by 60), with up to 10 minutes.
constexpr std::int32_t SecondsPerMinute = 60;
constexpr std::int32_t MaxEventTimeMinutes = 10;
// 0x5a7f36..0x5a7f76: the text in 0xff0a0aff, or 0xff0affff with type 3 or under a minute, over 0.
constexpr std::uint32_t EventTimeColor = 0xff0a0affu;
constexpr std::uint32_t EventTimeLastMinuteColor = 0xff0affffu;
constexpr std::uint8_t EventTimeStartKind = 3;
// 0x5a7f52, 0x5a7f8a and 0x5a7f95: 1392 "…%d segundos" with type 3; 1390 "…%d:0%d" under 10 seconds and 1391 "…%d:%d"
// with 10 or more.
constexpr std::size_t EventTimeStartText = 1392;
constexpr std::size_t EventTimeShortText = 1390;
constexpr std::size_t EventTimeText = 1391;
constexpr std::int32_t EventTimeTwoDigits = 10;
// [ebp-0x1c]: 28 bytes, filled first by the strcpy of 0x77f3cbc (.bss, empty).
constexpr std::size_t EventTimeBuffer = 0x1c;
// 0x5a7fb2 and 0x5a7fe4: the two rows, with the x of the world's middle (0.5f at 0x6b882c) and one more (1f at
// 0x6b87d4).
constexpr int EventTimeTop = 0x15e;
constexpr int EventTimeShadowTop = 0x15f;
constexpr std::uint32_t WhiteText = 0xffffffffu;

// 0x40c322..0x40c339 and 0x4050e2..0x4050f9: the points in 0xff0096ff over 0, from y = 345f.
constexpr std::uint32_t CastlePointsColor = 0xff0096ffu;
constexpr float CastleFirstRow = 345.0f;
constexpr float CastleSecondRow = 357.0f;
constexpr float CastleTimeGap = 10.0f; // 0x6b87a8
constexpr int CastleColumn = 0x23a;
constexpr float CastleTimeColumn = 570.0f; // 0x440e8000
// 0x40c33f..0x40c348: Blood Castle with +0x10 at 1, 2 or 5; 0x405100..0x40510a: Chaos Castle from 6 to 8.
constexpr std::uint8_t BloodCastleLastPlay = 2;
constexpr std::uint8_t BloodCastleSoccerPlay = 5;
constexpr std::uint8_t ChaosCastleFirstPlay = 6;
constexpr std::uint8_t ChaosCastleLastPlay = 8;
constexpr std::int32_t NoKillTarget = 0xffff;
// 0x40c381, 0x40c393 and 0x40513e: "Monstruos: ( %d/%d )", the one of 5 and Chaos Castle's.
constexpr std::size_t BloodCastlePointsText = 864;
constexpr std::size_t BloodCastleSoccerPointsText = 866;
constexpr std::size_t ChaosCastlePointsText = 1161;
// 0x40c3ee and 0x405197: "Tiempo restante", by wsprintfA with no arguments.
constexpr std::size_t TimeLeftText = 865;
constexpr std::size_t CastleTextBuffer = 0x100;
// The maps of the two (0x6c9dd0).
using Maps::FirstBloodCastleMap;
using Maps::FirstChaosCastleMap;
using Maps::LastBloodCastleMap;
using Maps::LastChaosCastleMap;
// 0x4051d9..0x405214: the trap area by +0x20, in (40, 46], (30, 36] and (20, 26].
struct TrapArea
{
    std::int32_t above;
    std::int32_t upTo;
    std::uint8_t area;
};
constexpr std::array<TrapArea, 3> TrapAreas{{{0x28, 0x2e, 0}, {0x1e, 0x24, 3}, {0x14, 0x1a, 6}}};

// 0x40bbf0: " %.2d :" with the minutes, " %.2d" with the seconds (if not negative) and, under 15 minutes, ": %.2d"
// with ftol(0x5767b88) % 60; under 5, the text in 0xff2020ff.
constexpr std::string_view MinutesFormat = " %.2d :";   // 0x6c63ac
constexpr std::string_view SecondsFormat = " %.2d";     // 0x6c63a4
constexpr std::string_view HundredthsFormat = ": %.2d"; // 0x6c639c
constexpr std::int32_t RedMinutes = 5;
constexpr std::int32_t HundredthsMinutes = 15;
constexpr std::uint32_t LastMinutesColor = 0xff2020ffu;
// [ebp-0xc]: 12 bytes for the seconds and the hundredths.
constexpr std::size_t TimePartBuffer = 0xc;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x632a2b → method +0x10: the texts of the results boxes, *static*.
constexpr Render::Color ResultTitleColor{0.5f, 1.0f, 0.5f, 1.0f};   // 0x40bdfe, 0x40c452, 0x405232
constexpr Render::Color RankingHeaderColor{0.5f, 0.5f, 1.0f, 1.0f}; // 0x40be76
constexpr Render::Color OwnRowColor{0.9f, 0.9f, 0.0f, 1.0f};        // 0x40bf26
constexpr int BoxCenter = 0x140;
constexpr std::uint32_t ExperienceColor = 0xffd2ffd2u;
constexpr std::uint32_t ZenColor = 0xffd2d2ffu;
constexpr std::uint32_t PointsColor = 0xffffd2d2u;
// 0x40c46c..0x40c495: 857 and 858 with +0x24, 859 and 860 without it; 861, 862 (only with +0x24) and 863 with dwords
// +0x3c, +0x40 and +0x38.
constexpr std::size_t BloodCastleWonText = 857;
constexpr std::size_t BloodCastleQuestText = 858;
constexpr std::size_t BloodCastleLostText = 859;
constexpr std::size_t BloodCastleFailedText = 860;
constexpr std::size_t ExperienceText = 861;
constexpr std::size_t ZenText = 862;
constexpr std::size_t BloodCastlePointsResultText = 863;
// 0x40524c..0x405379: 1151 and "%s %s" with 1152 and 858 with +0x24; "%s %s" with 1152 and 860 and 1153 without it;
// 861, 1162 and 1163 with +0x3c, +0x38 and +0x40.
constexpr std::size_t ChaosCastleWonText = 1151;
constexpr std::size_t ChaosCastleQuestText = 1152;
constexpr std::size_t ChaosCastleRetryText = 1153;
constexpr std::size_t ChaosCastleMonstersText = 1162;
constexpr std::size_t ChaosCastlePlayersText = 1163;
constexpr std::string_view TwoTextsFormat = "%s %s"; // 0x6c6350
// 0x40bdd0: the columns from 0xdb (+0x10, +0x34, +0x3a, +0x26), 647 at (0xe5, 0x46), 648 with the hero's name at
// (0xe5, 0x56), titles 680..684 at y 0x6e (without 681, and 682 ten further right) and the rows from 0x7e in steps of
// 16; the last, the hero's own, with 685 at 0x11e and the row at 0x12e.
constexpr std::array<int, 5> RankingColumns{0xdb, 0xeb, 0x11f, 0x159, 0x17f};
constexpr int RankingTitleLeft = 0xe5;
constexpr std::size_t RankingCongratulationsText = 647;
constexpr std::size_t RankingHonorText = 648;
constexpr std::size_t FirstRankingHeaderText = 680;
constexpr std::size_t SkippedRankingHeaderText = 681;
constexpr std::size_t OwnRankingText = 685;
constexpr int RankingHeaderTop = 0x6e;
constexpr int RankingFirstRow = 0x7e;
constexpr int RankingRowStep = 0x10;
constexpr int OwnRankingTitleTop = 0x11e;
constexpr int OwnRankingRow = 0x12e;
constexpr std::string_view RankFormat = "%2d";   // 0x6c63c0
constexpr std::string_view ScoreFormat = "%10d"; // 0x6c63b8
constexpr std::string_view RewardFormat = "%6d"; // 0x6c63b4
// A results record: 24 bytes, the name in the first 10 and dwords +0xc, +0x10 and +0x14.
constexpr std::size_t ResultRecordBytes = 24;
constexpr std::size_t ResultNameBytes = 10;

Core::Expected<std::string, std::string> Formatted(Crt::FormatFunction function, std::string_view format,
                                                   std::span<const Crt::FormatArgument> arguments, std::size_t buffer,
                                                   std::string_view where)
{
    auto text = Crt::FormatOriginal(function, format, arguments, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (text.Value().size() >= buffer)
        return Core::Unexpected(std::format("{}: \"{}\" ocupa {} bytes con su '\\0' y el buffer de la pila tiene {}",
                                            where, text.Value(), text.Value().size() + 1, buffer));
    return text;
}

Core::Expected<std::string, std::string> FormattedText(const Ui::GameHudContext& context, Crt::FormatFunction function,
                                                       std::size_t index,
                                                       std::span<const Crt::FormatArgument> arguments,
                                                       std::size_t buffer, std::string_view where)
{
    const std::optional<std::string_view> format = Ui::TextAt(context.texts, index);
    if (!format)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return Formatted(function, *format, arguments, buffer, where);
}

void DrawCentered(Ui::GameHudContext& context, int x, int y, std::string_view text)
{
    Ui::DrawHudCentered(context.text, context.sprites.Screen(), context.hud, x, y, text);
}

// 0x40bbf0(x, y, seconds), in font 0x576355c.
Result DrawTheTimeLeft(Ui::GameHudContext& context, float x, float y, std::int32_t seconds)
{
    constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;
    const std::int32_t minutes = seconds / SecondsPerMinute;
    const std::array<Crt::FormatArgument, 1> minuteArguments{minutes};
    auto text = Formatted(Wsprintf, MinutesFormat, minuteArguments, CastleTextBuffer, "0x40bc27");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    std::string line = std::move(text).Value();
    // 0x40bc29..0x40bc37: the remainder by subtraction, and without it if negative (JS).
    const std::int32_t rest = seconds - minutes * SecondsPerMinute;
    if (rest >= 0)
    {
        const std::array<Crt::FormatArgument, 1> restArguments{rest};
        auto part = Formatted(Wsprintf, SecondsFormat, restArguments, TimePartBuffer, "0x40bc43");
        if (!part.HasValue())
            return Core::Unexpected(std::move(part).Error());
        line += part.Value();
    }
    if (minutes < RedMinutes)
        context.hud.textColors.text = LastMinutesColor;
    if (minutes < HundredthsMinutes)
    {
        // 0x40bc90..0x40bca1: _ftol of the world clock and the signed division's remainder.
        const std::optional<std::int32_t> clock = Math::OriginalFtolLow(static_cast<double>(context.worldClock));
        if (!clock)
            return Core::Unexpected(
                std::format("0x40bc96: el reloj del mundo {} fuera del perfil de ftol", context.worldClock));
        const std::array<Crt::FormatArgument, 1> clockArguments{*clock % SecondsPerMinute};
        auto part = Formatted(Wsprintf, HundredthsFormat, clockArguments, TimePartBuffer, "0x40bcad");
        if (!part.HasValue())
            return Core::Unexpected(std::move(part).Error());
        line += part.Value();
    }
    if (line.size() >= CastleTextBuffer)
        return Core::Unexpected(std::format("0x40bcde: el tiempo ocupa {} bytes con su '\\0'", line.size() + 1));
    context.hud.dcFont = FontKind::BigBold;
    DrawCentered(context, static_cast<int>(x), static_cast<int>(y), line);
    return {};
}

void DrawLine(Ui::GameHudContext& context, int x, int y, std::string_view text)
{
    Ui::DrawHudLine(context.text, context.sprites.Screen(), context.hud, x, y, text);
}

Core::Expected<std::string_view, std::string> TextOf(const Ui::GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// Dword `offset` of the 0x108 bytes of +0x2c (the 24-byte records that 93 copies).
std::int32_t ResultDword(const EventMapObject& event, std::size_t offset)
{
    return event.field2c[offset / sizeof(std::int32_t)];
}

// The 10 name bytes of record `record`, up to the first 0 (0x40bfa4..0x40bfe8: 11 bytes at 0 and the record's 10 on
// top).
std::string ResultName(const EventMapObject& event, std::size_t record)
{
    std::string name;
    for (std::size_t at = 0; at < ResultNameBytes; ++at)
    {
        const std::size_t byte = record * ResultRecordBytes + at;
        const auto word = static_cast<std::uint32_t>(event.field2c[byte / sizeof(std::int32_t)]);
        const auto value = static_cast<char>((word >> ((byte % sizeof(std::int32_t)) * 8U)) & 0xffU);
        if (value == '\0')
            break;
        name.push_back(value);
    }
    return name;
}

// 0x40bdd0: Devil Square.
Result DrawTheDevilSquareRanking(Ui::GameHudContext& context, const EventMapObject& event)
{
    constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(ResultTitleColor);
    auto title = TextOf(context, RankingCongratulationsText, "0x40be35");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    DrawLine(context, RankingTitleLeft, 0x46, title.Value());
    // 0x40be3a..0x40be6e: wsprintfA with the hero's name (+0x1d9 of [0x743a170]).
    const Kernel::ActorState& hero = context.world.Hero();
    const std::string heroName = Actors::ChatNameOf(hero);
    const std::array<Crt::FormatArgument, 1> nameArguments{Crt::StringArgument{.text = heroName}};
    auto honor = FormattedText(context, Wsprintf, RankingHonorText, nameArguments, CastleTextBuffer, "0x40be5c");
    if (!honor.HasValue())
        return Core::Unexpected(std::move(honor).Error());
    DrawLine(context, RankingTitleLeft, 0x56, honor.Value());
    sprites.SetColor(RankingHeaderColor);
    for (std::size_t column = 0; column < RankingColumns.size(); ++column)
    {
        const std::size_t index = FirstRankingHeaderText + column;
        if (index == SkippedRankingHeaderText)
            continue;
        auto header = TextOf(context, index, "0x40beb9");
        if (!header.HasValue())
            return Core::Unexpected(std::move(header).Error());
        const int x = RankingColumns[column] + (column == 2 ? 10 : 0);
        DrawLine(context, x, RankingHeaderTop, header.Value());
    }
    sprites.SetColor(White);
    int y = RankingFirstRow;
    const std::int32_t count = event.field24;
    for (std::int32_t row = 0; row < count; ++row)
    {
        // 0x40bf0b..0x40bf1b: record (row + 1) % count: record 0, the hero's own, goes in the last row.
        const auto record = static_cast<std::size_t>((row + 1) % count);
        if ((record + 1) * ResultRecordBytes > event.field2c.size() * sizeof(std::int32_t))
            return Core::Unexpected(std::format("0x40bf1b: el registro {} pasa los 0x108 bytes de +0x2c y lee fuera "
                                                "del objeto de 0x134 bytes",
                                                record));
        std::int32_t rank = row + 1;
        if (row == count - 1)
        {
            sprites.SetColor(OwnRowColor);
            auto own = TextOf(context, OwnRankingText, "0x40bf4c");
            if (!own.HasValue())
                return Core::Unexpected(std::move(own).Error());
            DrawLine(context, RankingColumns[0], OwnRankingTitleTop, own.Value());
            y = OwnRankingRow;
            rank = event.field28;
        }
        const std::array<Crt::FormatArgument, 1> rankArguments{rank};
        auto rankText = Formatted(Wsprintf, RankFormat, rankArguments, CastleTextBuffer, "0x40bf7e");
        if (!rankText.HasValue())
            return Core::Unexpected(std::move(rankText).Error());
        context.hud.textColors.text = WhiteText;
        DrawLine(context, RankingColumns[0], y, rankText.Value());
        DrawLine(context, RankingColumns[1], y, ResultName(event, record));
        struct Cell
        {
            std::uint32_t color;
            std::string_view format;
            std::size_t offset;
            std::string_view where;
        };
        const std::array<Cell, 3> cells{{{PointsColor, ScoreFormat, 0xc, "0x40c011"},
                                         {ExperienceColor, RewardFormat, 0x10, "0x40c047"},
                                         {ZenColor, RewardFormat, 0x14, "0x40c07d"}}};
        for (std::size_t cell = 0; cell < cells.size(); ++cell)
        {
            context.hud.textColors.text = cells[cell].color;
            const std::array<Crt::FormatArgument, 1> value{
                ResultDword(event, record * ResultRecordBytes + cells[cell].offset)};
            auto text = Formatted(Wsprintf, cells[cell].format, value, CastleTextBuffer, cells[cell].where);
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            DrawLine(context, RankingColumns[cell + 2], y, text.Value());
        }
        y += RankingRowStep;
    }
    return {};
}

// 0x40c440 and 0x405220: the two titles (with 0x623690 at x 0x140) and the rows in font 0x5763558.
Result DrawTheCastleResult(Ui::GameHudContext& context, const EventMapObject& event)
{
    constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;
    const bool bloodCastle = event.kind == EventMapKind::BloodCastle;
    const bool won = event.field24 != 0;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(ResultTitleColor);
    const auto line = [&](int y, std::string_view text) { DrawCentered(context, BoxCenter, y, text); };
    const auto textLine = [&](int y, std::size_t index, std::string_view where) -> Result
    {
        auto text = TextOf(context, index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        line(y, text.Value());
        return {};
    };
    if (bloodCastle)
    {
        if (auto drawn = textLine(0x46, won ? BloodCastleWonText : BloodCastleLostText, "0x40c478"); !drawn.HasValue())
            return drawn;
        if (auto drawn = textLine(0x56, won ? BloodCastleQuestText : BloodCastleFailedText, "0x40c4a1");
            !drawn.HasValue())
            return drawn;
    }
    else
    {
        auto quest = TextOf(context, ChaosCastleQuestText, "0x40527c");
        auto second = TextOf(context, won ? BloodCastleQuestText : BloodCastleFailedText, "0x40527c");
        if (!quest.HasValue())
            return Core::Unexpected(std::move(quest).Error());
        if (!second.HasValue())
            return Core::Unexpected(std::move(second).Error());
        const std::array<Crt::FormatArgument, 2> arguments{
            Crt::TextArgument{.index = ChaosCastleQuestText, .text = quest.Value()},
            Crt::TextArgument{.index = won ? BloodCastleQuestText : BloodCastleFailedText, .text = second.Value()}};
        auto joined = Formatted(Wsprintf, TwoTextsFormat, arguments, CastleTextBuffer, won ? "0x40527c" : "0x4052b2");
        if (!joined.HasValue())
            return Core::Unexpected(std::move(joined).Error());
        if (won)
        {
            if (auto drawn = textLine(0x50, ChaosCastleWonText, "0x405258"); !drawn.HasValue())
                return drawn;
            line(0x60, joined.Value());
        }
        else
        {
            line(0x50, joined.Value());
            if (auto drawn = textLine(0x60, ChaosCastleRetryText, "0x4052d3"); !drawn.HasValue())
                return drawn;
        }
    }
    // 0x40c4a9..0x40c4dd and 0x4052db..0x40530a: glColor3f(1, 1, 1), font 0x5763558 and the experience.
    sprites.SetColor(White);
    context.hud.textColors.text = WhiteText;
    context.hud.dcFont = FontKind::Bold;
    context.hud.textColors.text = ExperienceColor;
    const auto valueLine = [&](int y, std::size_t index, std::size_t offset, std::string_view where) -> Result
    {
        const std::array<Crt::FormatArgument, 1> value{ResultDword(event, offset - 0x2c)};
        auto text = FormattedText(context, Wsprintf, index, value, CastleTextBuffer, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        line(y, text.Value());
        return {};
    };
    if (!bloodCastle)
    {
        if (auto drawn = valueLine(0x7e, ExperienceText, 0x3c, "0x405324"); !drawn.HasValue())
            return drawn;
        if (auto drawn = valueLine(0x92, ChaosCastleMonstersText, 0x38, "0x40534c"); !drawn.HasValue())
            return drawn;
        return valueLine(0xa6, ChaosCastlePlayersText, 0x40, "0x405377");
    }
    if (auto drawn = valueLine(0x6a, ExperienceText, 0x3c, "0x40c4f7"); !drawn.HasValue())
        return drawn;
    int y = 0x82;
    if (won)
    {
        context.hud.textColors.text = ZenColor;
        if (auto drawn = valueLine(y, ZenText, 0x40, "0x40c535"); !drawn.HasValue())
            return drawn;
        y = 0x9a;
    }
    context.hud.textColors.text = PointsColor;
    return valueLine(y, BloodCastlePointsResultText, 0x38, "0x40c56b");
}
} // namespace

Result DrawTheEventTime(Ui::GameHudContext& context)
{
    const EventGlobals& events = context.world.Runtime().events;
    const std::uint16_t time = events.timeValue;
    if (time == 0)
        return {};
    const std::int32_t seconds = time % SecondsPerMinute;
    const std::int32_t minutes = time / SecondsPerMinute;
    if (minutes > MaxEventTimeMinutes)
        return {};
    // 0x5a7f1c..0x5a7fa0: font 0x576355c, the colors and the sprintf.
    Kernel::HudState& hud = context.hud;
    hud.dcFont = FontKind::BigBold;
    hud.textColors = Render::Text::TextColors{.text = EventTimeColor, .background = 0};
    constexpr auto Sprintf = Crt::FormatFunction::Sprintf;
    Core::Expected<std::string, std::string> text = std::string{};
    if (events.timeKind == EventTimeStartKind)
    {
        hud.textColors.text = EventTimeLastMinuteColor;
        const std::array<Crt::FormatArgument, 1> arguments{seconds};
        text = FormattedText(context, Sprintf, EventTimeStartText, arguments, EventTimeBuffer, "0x5a7f62");
    }
    else
    {
        if (time < SecondsPerMinute)
            hud.textColors.text = EventTimeLastMinuteColor;
        const std::array<Crt::FormatArgument, 2> arguments{minutes, seconds};
        text = FormattedText(context, Sprintf, seconds < EventTimeTwoDigits ? EventTimeShortText : EventTimeText,
                             arguments, EventTimeBuffer, "0x5a7f9b");
    }
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    // 0x5a7fa3: 0x5bb8d0, with its queries; FILD · 0.5f stored as a float.
    const std::int32_t worldWidth = Hud::GamePanels(context.world, context.random).Registry().WorldWidth();
    const auto half = static_cast<float>(static_cast<double>(worldWidth) * 0.5);
    DrawCentered(context, static_cast<int>(half), EventTimeTop, text.Value());
    hud.textColors.text = WhiteText;
    DrawCentered(context, static_cast<int>(half + 1.0f), EventTimeShadowTop, text.Value());
    // 0x5a7ff4..0x5a8006: the normal font and the white text.
    hud.dcFont = FontKind::Normal;
    hud.textColors.text = WhiteText;
    return {};
}

Result DrawTheCastleTimeAndPoints(Ui::GameHudContext& context, EventMapObject& event, bool write)
{
    const bool bloodCastle = event.kind == EventMapKind::BloodCastle;
    if (!bloodCastle && event.kind != EventMapKind::ChaosCastle)
        return {};
    if (event.field10 == 0)
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t map = runtime.map;
    if (bloodCastle ? map < FirstBloodCastleMap || map > LastBloodCastleMap
                    : map < FirstChaosCastleMap || map > LastChaosCastleMap)
        return {};
    // 0x40c2fe..0x40c32c: 0x620980, 0x620a00(0), glColor3f(1, 1, 1) and the colors.
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    context.sprites.SetColor(White);
    Kernel::HudState& hud = context.hud;
    hud.textColors = Render::Text::TextColors{.text = CastlePointsColor, .background = 0};
    float y = CastleFirstRow;
    const std::uint8_t state = event.field10;
    const bool playing = bloodCastle ? state <= BloodCastleLastPlay || state == BloodCastleSoccerPlay
                                     : state >= ChaosCastleFirstPlay && state <= ChaosCastleLastPlay;
    if (!playing)
        return {};
    const std::int32_t remaining = event.field14[1];
    if (remaining <= 0)
        return {};
    const std::int32_t killTarget = event.field14[2];
    const std::int32_t kills = event.field14[3];
    constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;
    if (killTarget != NoKillTarget)
    {
        const std::size_t format = !bloodCastle                     ? ChaosCastlePointsText
                                   : state == BloodCastleSoccerPlay ? BloodCastleSoccerPointsText
                                                                    : BloodCastlePointsText;
        const std::array<Crt::FormatArgument, 2> arguments{kills, killTarget};
        auto points = FormattedText(context, Wsprintf, format, arguments, CastleTextBuffer,
                                    bloodCastle ? "0x40c399" : "0x405144");
        if (!points.HasValue())
            return Core::Unexpected(std::move(points).Error());
        hud.dcFont = FontKind::Bold;
        hud.textColors.text = CastlePointsColor;
        DrawCentered(context, CastleColumn, static_cast<int>(CastleFirstRow), points.Value());
        y = CastleSecondRow;
    }
    hud.dcFont = FontKind::Bold;
    auto timeLeft =
        FormattedText(context, Wsprintf, TimeLeftText, {}, CastleTextBuffer, bloodCastle ? "0x40c3f4" : "0x40519d");
    if (!timeLeft.HasValue())
        return Core::Unexpected(std::move(timeLeft).Error());
    DrawCentered(context, CastleColumn, static_cast<int>(y), timeLeft.Value());
    if (auto drawn = DrawTheTimeLeft(context, CastleTimeColumn, y + CastleTimeGap, remaining); !drawn.HasValue())
        return drawn;
    if (bloodCastle || !write)
        return {};
    for (const TrapArea& trap : TrapAreas)
    {
        if (kills > trap.above && kills <= trap.upTo)
        {
            runtime.chaosCastleArea = trap.area;
            break;
        }
    }
    return {};
}
Result DrawTheEventBoxText(Ui::GameHudContext& context, const EventMapObject& event)
{
    switch (event.kind)
    {
    case EventMapKind::DevilSquare:
        return DrawTheDevilSquareRanking(context, event);
    case EventMapKind::BloodCastle:
    case EventMapKind::ChaosCastle:
        return DrawTheCastleResult(context, event);
    }
    return {};
}
} // namespace Client::Events
