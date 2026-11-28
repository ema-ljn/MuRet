#include "Events/Crywolf/CrywolfInterfaceDrawing.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Protocol/Events/Events.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Events/Crywolf/CrywolfContract.h"
#include "Ui/GameHudContext.h"
#include "Hud/HudText.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"
#include "Crt/OriginalFormat.h"
#include "Ui/Widget.h"
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

namespace Client::Events::Crywolf
{
namespace
{
using Render::Text::FontKind;
using Result = Core::Expected<void, std::string>;
constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;

using Math::F32;

// [ebp-0x1d8]..[ebp-0xac]: the 300 bytes of the wsprintfA buffer ([0x6b851c]).
constexpr std::size_t TextBuffer = 300;

// 0x4366de..0x436708: the results frame.
constexpr std::uint16_t RankingBoard = 0x6e2;
// 0x436734, 0x4367ed and 0x436a32: the titles in 0xff1594ff, the rows in white and the notice in 0xffff00ff, over 0.
constexpr std::uint32_t TitleColor = 0xff1594ffu;
constexpr std::uint32_t WhiteText = 0xffffffffu;
constexpr std::uint32_t NoticeColor = 0xffff00ffu;
// 0x436723..0x4367db: "%s" (0x6c7084) with 680, 681, 1973 and 1977 centered at y 0xa0.
struct Header
{
    int x;
    std::size_t text;
};
constexpr std::array<Header, 4> Headers{{{0xf0, 680}, {0x11d, 681}, {0x14d, 1973}, {0x183, 1977}}};
constexpr int HeaderTop = 0xa0;
// 0x4367fd..0x436a27: five rows from y 0xaf in steps of 15, with "%d" (0x6c7440) of the place at 0xf0, "%s" of the
// name at 0x11d, the class at 0x14f and "%d" of the points at 0x181.
constexpr int FirstRow = 0xaf;
constexpr int RowStep = 0xf;
constexpr int PlaceColumn = 0xf0;
constexpr int NameColumn = 0x11d;
constexpr int ClassColumn = 0x14f;
constexpr int ScoreColumn = 0x181;
constexpr std::int32_t EmptyRow = -1;
constexpr std::string_view TextFormat = "%s";   // 0x6c7084
constexpr std::string_view NumberFormat = "%d"; // 0x6c7440
// 0x43688b..0x4369c1: the class by the 3 low bits, 20 to 24, and with something in the upper ones (>> 3) 25 to 27 for
// the first three; with 5 or more, the buffer keeps the name.
constexpr std::array<std::size_t, 5> ClassTexts{20, 21, 22, 23, 24};
constexpr std::array<std::size_t, 3> SecondClassTexts{25, 26, 27};
constexpr std::uint8_t ClassBits = 7;
constexpr unsigned SecondClassShift = 3;
// 0x436a3c..0x436a9a: with the notice at 1, 2000 and 2001; otherwise 2009 and 2010, at (0x14a, 17 · 5 + 0xaf) and
// (0x148, 17 · 5 + 0xaf + 0xc0).
constexpr std::size_t WonText = 2000;
constexpr std::size_t WonSecondText = 2001;
constexpr std::size_t LostText = 2009;
constexpr std::size_t LostSecondText = 2010;
constexpr int NoticeX = 0x14a;
constexpr int NoticeSecondX = 0x148;
constexpr int NoticeTop = 260;
constexpr int NoticeSecondTop = 277;
// 0x436a9f..0x436b8b: the button at (300, 300, 54, 30), with the mouse inside (300, 354) × (300, 330), edges excluded.
constexpr float ButtonX = 300.0f;
constexpr float ButtonY = 300.0f;
constexpr float ButtonWidth = 54.0f;
constexpr float ButtonHeight = 30.0f;
constexpr int ButtonLeft = 0x12c;
constexpr int ButtonRight = 0x162;
constexpr int ButtonTop = 0x12c;
constexpr int ButtonBottom = 0x14a;
constexpr std::uint16_t ButtonSlot = 0x6c9;
constexpr std::uint16_t ButtonHoverSlot = 0x6ca;
constexpr std::uint16_t ButtonPushSlot = 0x6cb;
constexpr std::int32_t ButtonPushed = 5;

// 0x436274..0x43656f: the contract window (0x723a44 at 1 or 2), background 0x6cc at (212, 206, 209, 80) and its (54,
// 30) buttons at y 250, with the mouse inside (left, right) × (0xfa, 0x118), edges excluded: with 1, the No
// (0x6c3..0x6c5 at 330, which closes with 0x723bdc at 1) and the Yes (0x6c9..0x6cb at 250, with 2); with 2, the OK
// (0x6c6..0x6c8 at 290, with 3). The lines are centered at x 0x13d: the first at y 0xe3 with a second line and at 0xef
// without, and the second at 0xee.
constexpr std::uint8_t QuestionWindow = 1;
constexpr std::uint8_t NoticeWindow = 2;
constexpr std::uint16_t ContractBoardSlot = 0x6cc;
constexpr float ContractButtonY = 250.0f;
constexpr int ContractButtonTop = 0xfa;
constexpr int ContractButtonBottom = 0x118;
struct ContractButton
{
    float x;
    int left;
    int right;
    std::uint16_t slot;
    std::uint16_t hoverSlot;
    std::uint16_t pushSlot;
    std::int32_t pushed;
};
constexpr std::array<ContractButton, 2> QuestionButtons{
    {{330.0f, 0x14a, 0x180, 0x6c3, 0x6c4, 0x6c5, 1}, {250.0f, 0xfa, 0x130, 0x6c9, 0x6ca, 0x6cb, 2}}};
constexpr std::array<ContractButton, 1> NoticeButtons{{{290.0f, 0x122, 0x158, 0x6c6, 0x6c7, 0x6c8, 3}}};
constexpr int ContractTextX = 0x13d;
constexpr int ContractFirstLineY = 0xe3;
constexpr int ContractSingleLineY = 0xef;
constexpr int ContractSecondLineY = 0xee;

// 0x436b97..0x436bc3: with the notice at 1, the count rises and at 400 goes back to 0 with the notice at 0.
constexpr std::int32_t NoticeFrames = 0x190;
// 0x436bd3..0x436c0c: the alpha wave, (2 · count) % 140, peaking at 70.
constexpr std::int32_t AlphaPeriod = 0x8c;
constexpr std::int32_t AlphaPeak = 0x46;
constexpr double AlphaStep = static_cast<double>(0.01f); // 0x6b88d4
constexpr double AlphaBase = static_cast<double>(0.3f);  // 0x6b89dc
// 0x436c0c..0x436c27: what the rank has left drops by 15 down to 0.
constexpr std::int32_t RankStep = 0xf;
// 0x436c27..0x436d37: banner 0x6b6 + 0x6c73dc at (x, 50, 329, 94): still at 150 with 15 · count over 479; with the
// notice at 0, at 15 · count + 150 and the count rises (and past 479, the notice at −1 and the rank at 0x186); with
// another, it enters from 15 · count − 329.
constexpr std::int32_t BannerStep = 15;
constexpr std::int32_t BannerEnd = 0x1df;
constexpr std::uint16_t BannerSlot = 0x6b6;
constexpr float BannerRestX = 150.0f;
constexpr std::int32_t BannerEnterX = 150;
constexpr std::int32_t BannerWidth = 329;
constexpr std::int32_t RankStart = 0x186;
// 0x436d3a..0x436da5: the rank table and the rank, shifted 0x6c73f8 + 250.
constexpr std::uint16_t RankTableSlot = 0x6d0;
constexpr std::uint16_t RankSlot = 0x6d1;
constexpr std::int32_t RankLeft = 0xfa;
// 0x436dba..0x43707e: with the rank arrived, its letter (0x6d2 + 0x723bed), label 0x6e1 and the nine digits of
// 0x723bf0 (0x6d7 + digit) right to left from 409, in steps of 15; the ones it does not compute at 0
// (0x436e1e..0x436e57).
constexpr std::uint16_t RankLetterSlot = 0x6d2;
constexpr std::uint16_t ExperienceLabelSlot = 0x6e1;
constexpr std::uint16_t FirstDigitSlot = 0x6d7;
constexpr std::size_t ExperienceDigits = 9;
constexpr float LastDigitX = 409.0f;
constexpr float DigitStep = 15.0f;

// 0x4365e3..0x4366cf: the five altars, (x, y, 13, 13) with 0.8125 of UV.
struct Altar
{
    float x;
    float y;
};
constexpr std::array<Altar, 5> Altars{{{4.0f, 29.0f}, {16.0f, 17.0f}, {30.0f, 9.0f}, {46.0f, 5.0f}, {62.0f, 4.0f}}};
constexpr float AltarSize = 13.0f;
constexpr float AltarUv = 0.8125f;
constexpr std::uint8_t AltarHighBits = 0xf0;
constexpr std::uint8_t AltarContracted = 0x10;
constexpr std::uint8_t AltarLowBits = 0x0f;
// 0x437130..0x4371db: 0x6cd, 0x6ce or 0x6cf with 0x1? in the high nibble; 0x6bd or 0x6be (or nothing) otherwise.
constexpr std::uint16_t ContractedFirstSlot = 0x6cd;
constexpr std::uint16_t ContractedSecondSlot = 0x6ce;
constexpr std::uint16_t ContractedOtherSlot = 0x6cf;
constexpr std::uint16_t AltarFirstSlot = 0x6bd;
constexpr std::uint16_t AltarSecondSlot = 0x6be;
// 0x4370a8..0x43710b: frame 0x6b9 and decoration 0x6b8 at 98 + 0x723a40 (0x6b8b14).
constexpr std::uint16_t StateFrameSlot = 0x6b9;
constexpr std::uint16_t DecorationSlot = 0x6b8;
constexpr double DecorationTop = 98.0;
// 0x4371fb..0x437267: 0x6bb (0x6bc with 0x723bd8 at 0) and the bottom bar 0x6c2.
constexpr std::uint16_t CountSlot = 0x6bb;
constexpr std::uint16_t NoCountSlot = 0x6bc;
constexpr std::uint16_t BottomBarSlot = 0x6c2;
// 0x43726f..0x4372c7: glColor3f(1, 0.6, 0.3) and "1948" with 0x723bd8 at (57, 85), centered, in font 0x5763554.
constexpr Render::Color StateColor{1.0f, 0.6f, 0.3f, 1.0f};
constexpr std::size_t CountText = 1948;
// 0x4372dc..0x4373f0: the Balgass bar drops to 21 (0x6b8b10) by 1 (0x6b87d4) with 0x723a3d at 1; once down,
// background 0x6b6, icon 0x6ba, 1949 (with wsprintfA and no arguments) at (38, 101) and bar 0x6b7 of 0x6c73d4 · 0.68f
// wide and 0x6c73d4 · 0.67f / 128 of UV.
constexpr float DecorationDown = 21.0f;
constexpr double DecorationStep = 1.0;
constexpr std::size_t BossText = 1949;
constexpr std::uint16_t BossBackSlot = 0x6b6;
constexpr std::uint16_t BossIconSlot = 0x6ba;
constexpr std::uint16_t BossBarSlot = 0x6b7;
constexpr double BossBarWidth = static_cast<double>(0.68f); // 0x6b8b04
constexpr double BossBarUv = static_cast<double>(0.67f);    // 0x6b8b0c
constexpr double UvPerPixel = 0.0078125;                    // 0x6b8b08
// 0x4373f6..0x437490: the state clock from 0x7239f4: the minutes with the MUL by 0x45e7b273 (>> 46, unsigned / 60000),
// the seconds by _ftol(ms · 1.6666667e-5f · 60f).
constexpr std::uint64_t MinuteMagic = 0x45e7b273u;
constexpr unsigned MinuteShift = 46;
constexpr double SecondPerMs = static_cast<double>(1.6666667e-5f); // 0x6b8b00
constexpr double SecondsPerMinute = 60.0;                          // 0x6b86cc
constexpr std::int32_t ClockSeconds = 0x3c;
// 0x437496..0x43750b: in white, or (1, 0.3, 0.3) with 0x723a3d, the minutes at (300, 414) and 60 − seconds at (327,
// 414).
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Render::Color AlarmColor{1.0f, 0.3f, 0.3f, 1.0f};
constexpr float ClockY = 414.0f;
constexpr float MinutesX = 300.0f;
constexpr float SecondsX = 327.0f;
constexpr float ClockDigit = 14.0f;
// 0x437512..0x437557: the light, 0x6bf of 0x723c08 · 0.89f wide and 0x723c08 · 0.88f / 128 of UV.
constexpr std::uint16_t LightSlot = 0x6bf;
constexpr double LightWidth = static_cast<double>(0.89f); // 0x6b8af8
constexpr double LightUv = static_cast<double>(0.88f);    // 0x6b8afc

// 0x430180: with font 0x5763558, glColor3f(1, 1, 1), background 0xaa000000 and 0x63b0e3(10000), the four lines of
// 1957 + 4 · tip at (0xbe, 0x3f + 13 · row), the first in 0xffffc864 and the others in 0xffff9664.
constexpr std::uint32_t TipBackground = 0xaa000000u;
constexpr std::uint32_t TipTitleColor = 0xffffc864u;
constexpr std::uint32_t TipColor = 0xffff9664u;
constexpr std::uint32_t TipPeriod = 10000;
constexpr std::int32_t TipCount = 4;
constexpr std::int64_t FirstTipText = 1957;
constexpr int TipX = 0xbe;
constexpr int TipTop = 0x3f;
constexpr int TipStep = 13;

Render::OriginalQuad QuadOf(std::uint16_t slot, float x, float y, float width, float height, float du, float dv,
                            float alpha = 0.0f)
{
    return Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                .x = x,
                                .y = y,
                                .width = width,
                                .height = height,
                                .u = 0.0f,
                                .v = 0.0f,
                                .du = du,
                                .dv = dv,
                                .scaleSize = true,
                                .scalePosition = true,
                                .alpha = alpha};
}

Core::Expected<std::string_view, std::string> TextOf(const Ui::GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// wsprintfA into the 300-byte stack buffer.
Core::Expected<std::string, std::string>
Formatted(std::string_view format, std::span<const Crt::FormatArgument> arguments, std::string_view where)
{
    auto text = Crt::FormatOriginal(Wsprintf, format, arguments, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (text.Value().size() >= TextBuffer)
        return Core::Unexpected(std::format("{}: \"{}\" ocupa {} bytes con su '\\0' y el buffer de la pila tiene {}",
                                            where, text.Value(), text.Value().size() + 1, TextBuffer));
    return text;
}

// wsprintfA(buffer, "%s", text) of text `index`.
Core::Expected<std::string, std::string> TextInBuffer(const Ui::GameHudContext& context, std::size_t index,
                                                      std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    const std::array<Crt::FormatArgument, 1> arguments{Crt::TextArgument{.index = index, .text = text.Value()}};
    return Formatted(TextFormat, arguments, where);
}

void DrawCentered(Ui::GameHudContext& context, int x, int y, std::string_view text)
{
    Ui::DrawHudCentered(context.text, context.sprites.Screen(), context.hud, x, y, text);
}

std::string NameOf(const std::array<char, 11>& name)
{
    std::string text;
    for (const char letter : name)
    {
        if (letter == '\0')
            break;
        text.push_back(letter);
    }
    return text;
}

// 0x43688b..0x4369c1: the class text, or nothing (the buffer keeps the name).
std::optional<std::size_t> ClassTextOf(std::uint8_t classByte)
{
    const std::size_t base = classByte & ClassBits;
    if (base >= ClassTexts.size())
        return std::nullopt;
    const bool second = (classByte >> SecondClassShift) != 0;
    if (second && base < SecondClassTexts.size())
        return SecondClassTexts[base];
    return ClassTexts[base];
}

// 0x4366dc..0x436b93: the results, with 0x723bf9 at 1.
Result DrawTheResults(Ui::GameHudContext& context, Kernel::CrywolfInterface& ui)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.Quad(QuadOf(RankingBoard, 200.0f, 110.0f, 252.0f, 240.0f, 0.984375f, 0.630859375f));
    Kernel::HudState& hud = context.hud;
    hud.dcFont = FontKind::Normal;
    hud.textColors = Render::Text::TextColors{.text = TitleColor, .background = 0};
    for (const Header& header : Headers)
    {
        auto text = TextInBuffer(context, header.text, "0x436744");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawCentered(context, header.x, HeaderTop, text.Value());
    }
    hud.textColors = Render::Text::TextColors{.text = WhiteText, .background = 0};
    int y = FirstRow;
    for (std::size_t place = 0; place < ui.rankingNames.size(); ++place, y += RowStep)
    {
        const std::int32_t score = ui.dwords6c73e0[place + 1];
        if (score == EmptyRow)
            continue;
        const std::array<Crt::FormatArgument, 1> placeArguments{static_cast<std::int32_t>(place + 1)};
        auto placeText = Formatted(NumberFormat, placeArguments, "0x43683c");
        if (!placeText.HasValue())
            return Core::Unexpected(std::move(placeText).Error());
        DrawCentered(context, PlaceColumn, y, placeText.Value());
        const std::string name = NameOf(ui.rankingNames[place]);
        const std::array<Crt::FormatArgument, 1> nameArguments{Crt::StringArgument{.text = name}};
        auto buffer = Formatted(TextFormat, nameArguments, "0x436863");
        if (!buffer.HasValue())
            return Core::Unexpected(std::move(buffer).Error());
        DrawCentered(context, NameColumn, y, buffer.Value());
        std::string classText = std::move(buffer).Value();
        if (const std::optional<std::size_t> index = ClassTextOf(ui.rankingClasses[place]))
        {
            auto text = TextInBuffer(context, *index, "0x4368a0");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            classText = std::move(text).Value();
        }
        DrawCentered(context, ClassColumn, y, classText);
        const std::array<Crt::FormatArgument, 1> scoreArguments{score};
        auto scoreText = Formatted(NumberFormat, scoreArguments, "0x4369eb");
        if (!scoreText.HasValue())
            return Core::Unexpected(std::move(scoreText).Error());
        DrawCentered(context, ScoreColumn, y, scoreText.Value());
    }
    hud.textColors = Render::Text::TextColors{.text = NoticeColor, .background = 0};
    const bool won = ui.message == 1;
    struct Line
    {
        std::size_t text;
        int x;
        int y;
        std::string_view where;
    };
    const std::array<Line, 2> lines{
        {{won ? WonText : LostText, NoticeX, NoticeTop, "0x436a62"},
         {won ? WonSecondText : LostSecondText, NoticeSecondX, NoticeSecondTop, "0x436a9a"}}};
    for (const Line& line : lines)
    {
        auto text = TextOf(context, line.text, line.where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawCentered(context, line.x, line.y, text.Value());
    }
    const Ui::Point mouse = context.mouse;
    const bool over = mouse.x > ButtonLeft && mouse.x < ButtonRight && mouse.y > ButtonTop && mouse.y < ButtonBottom;
    std::uint16_t slot = ButtonSlot;
    if (over)
        slot = ui.dwords723bdc[0] == ButtonPushed ? ButtonPushSlot : ButtonHoverSlot;
    sprites.Quad(QuadOf(slot, ButtonX, ButtonY, ButtonWidth, ButtonHeight, 0.84375f, 0.9375f));
    if (over && ui.dwords723bdc[0] == ButtonPushed)
    {
        // 0x436b15..0x436b21.
        ui.byte723a44 = 0;
        ui.dwords723bdc[0] = 0;
        ui.byte723bf9 = 0;
    }
    return {};
}

// 0x436dba..0x43707e: the rank letter, the label and the nine digits of 0x723bf0.
void DrawTheRank(Render::SpriteBatch& sprites, const Kernel::CrywolfInterface& ui)
{
    sprites.Quad(QuadOf(static_cast<std::uint16_t>(RankLetterSlot + ui.byte723bed), 370.0f, 188.0f, 29.0f, 28.0f,
                        0.90625f, 0.84375f));
    // 0x436e1e..0x436e57: digit k with divisor 10^k while the value is not below it (signed); the first, with the
    // divisor at 0, is value % 10.
    std::array<std::int32_t, ExperienceDigits> digits{};
    const std::int32_t value = ui.dword723bf0;
    std::int32_t divisor = 0;
    for (std::size_t digit = 0; digit < ExperienceDigits; ++digit)
    {
        if (value < divisor)
            break;
        if (divisor > 0)
            digits[digit] = (value / divisor) % 10;
        else
        {
            digits[digit] = value % 10;
            divisor = 1;
        }
        divisor *= 10;
    }
    sprites.Quad(QuadOf(ExperienceLabelSlot, 229.0f, 235.0f, 60.0f, 19.0f, 0.9375f, 0.59375f));
    for (std::size_t digit = 0; digit < ExperienceDigits; ++digit)
    {
        const float x = LastDigitX - (DigitStep * static_cast<float>(digit));
        sprites.Quad(QuadOf(static_cast<std::uint16_t>(FirstDigitSlot + digits[digit]), x, 235.0f, 15.0f, 19.0f,
                            0.9375f, 0.59375f));
    }
}

// 0x436b97..0x43708c: the notice banner and the rank.
void DrawTheNotice(Render::SpriteBatch& sprites, Kernel::CrywolfInterface& ui)
{
    if (ui.message == 1)
    {
        ++ui.dword6c73d8;
        if (ui.dword6c73d8 >= NoticeFrames)
        {
            ui.dword6c73d8 = 0;
            ui.message = 0;
        }
    }
    else if (ui.message < 0)
        return;
    const std::int32_t count = ui.dword6c73d8;
    // 0x436bd3..0x436c11: signed IDIV.
    const std::int32_t wave = (count * 2) % AlphaPeriod;
    const float alpha = wave > AlphaPeak ? F32(1.0 - (static_cast<double>(wave - AlphaPeak) * AlphaStep))
                                         : F32((static_cast<double>(wave) * AlphaStep) + AlphaBase);
    if (ui.dword6c73f8 <= 0)
        ui.dword6c73f8 = 0;
    else
        ui.dword6c73f8 -= RankStep;
    const auto banner = static_cast<std::uint16_t>(BannerSlot + ui.dword6c73dc);
    const auto drawBanner = [&](float x)
    { sprites.Quad(QuadOf(banner, x, 50.0f, 329.0f, 94.0f, 0.642578125f, 0.734375f, alpha)); };
    if (count * BannerStep > BannerEnd)
        drawBanner(BannerRestX);
    else if (ui.message == 0)
    {
        // 0x436c7b..0x436c90: 5 · (3 · cuenta + 30).
        drawBanner(static_cast<float>((count * BannerStep) + BannerEnterX));
        ++ui.dword6c73d8;
        if (ui.dword6c73d8 * BannerStep > BannerEnd)
        {
            ui.dword6c73d8 = 0;
            ui.message = -1;
            ui.dword6c73f8 = RankStart;
        }
    }
    else
        drawBanner(static_cast<float>((count * BannerStep) - BannerWidth));
    sprites.Quad(QuadOf(RankTableSlot, 230.0f, 150.0f, 196.0f, 141.0f, 0.76171875f, 0.546875f));
    sprites.Quad(
        QuadOf(RankSlot, static_cast<float>(ui.dword6c73f8 + RankLeft), 188.0f, 110.0f, 28.0f, 0.859375f, 0.84375f));
    if (ui.dword6c73f8 == 0)
        DrawTheRank(sprites, ui);
}

// 0x63b0e3(10000): the timeGetTime clock of 0x7d3a0e4 and 0x7d3a0e8; fires with start + period <= now (signed) and
// then starts again on the next call.
bool TipTimerFires(Kernel::CrywolfInterface& ui, std::uint32_t now)
{
    if (ui.tipTimerRestart != 0)
    {
        ui.tipTimerStart = now;
        ui.tipTimerRestart = 0;
    }
    const auto due = static_cast<std::int32_t>(ui.tipTimerStart + TipPeriod);
    if (due <= static_cast<std::int32_t>(now))
    {
        ui.tipTimerRestart = 1;
        return true;
    }
    return false;
}

// 0x430180: the tips of stage 3.
Result DrawTheTips(Ui::GameHudContext& context, Kernel::CrywolfInterface& ui, std::uint8_t stage)
{
    if (stage != Protocol::Events::CrywolfReadyStage)
        return {};
    Kernel::HudState& hud = context.hud;
    hud.dcFont = FontKind::Bold;
    context.sprites.SetColor(White);
    hud.textColors.background = TipBackground;
    std::int32_t& tip = ui.dwords6c73e0[0];
    if (TipTimerFires(ui, context.tickCount))
    {
        ++tip;
        if (tip >= TipCount)
            tip = 0;
    }
    for (std::int32_t row = 0; row < TipCount; ++row)
    {
        hud.textColors.text = row == 0 ? TipTitleColor : TipColor;
        const std::int64_t index = FirstTipText + (std::int64_t{tip} * TipCount) + row;
        if (index < 0)
            return Core::Unexpected(std::format("0x430225: el consejo {} lee el texto {} de Text.bmd", tip, index));
        auto text = TextOf(context, static_cast<std::size_t>(index), "0x430237");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::DrawHudLine(context.text, context.sprites.Screen(), hud, TipX, TipTop + (TipStep * row), text.Value());
    }
    return {};
}

// 0x437113..0x4371f5: the five altars.
void DrawTheAltars(Render::SpriteBatch& sprites, const Kernel::CrywolfInterface& ui)
{
    for (std::size_t index = 0; index < Altars.size(); ++index)
    {
        const std::uint8_t altar = ui.altars[index];
        const std::uint8_t low = altar & AltarLowBits;
        std::optional<std::uint16_t> slot;
        if ((altar & AltarHighBits) == AltarContracted)
            slot = low == 1 ? ContractedFirstSlot : (low == 2 ? ContractedSecondSlot : ContractedOtherSlot);
        else if (low == 1)
            slot = AltarFirstSlot;
        else if (low == 2)
            slot = AltarSecondSlot;
        if (slot)
            sprites.Quad(QuadOf(*slot, Altars[index].x, Altars[index].y, AltarSize, AltarSize, AltarUv, AltarUv));
    }
}

// 0x4373f6..0x437490: the minutes left and the seconds gone, with the frame's GetTickCount in both reads.
Core::Expected<std::int32_t, std::string> StepTheClock(Kernel::CrywolfInterface& ui, std::uint32_t now)
{
    std::int32_t minutes = 0;
    const std::int32_t total = ui.dwords723bdc[3];
    if (total > 0)
    {
        const std::uint32_t elapsed = now - static_cast<std::uint32_t>(ui.dword7239f4);
        // FILD qword with the high part at 0 and FSTP float.
        const auto milliseconds = static_cast<float>(static_cast<double>(elapsed));
        const auto passed = static_cast<std::uint32_t>((std::uint64_t{elapsed} * MinuteMagic) >> MinuteShift);
        minutes = total - static_cast<std::int32_t>(passed);
        const double seconds = static_cast<double>(milliseconds) * SecondPerMs * SecondsPerMinute;
        const std::optional<std::int32_t> second = Math::OriginalFtolLow(seconds);
        if (!second)
            return Core::Unexpected(std::format("0x437453: los segundos {} fuera del perfil de ftol", seconds));
        if (ui.dword723c00 != *second)
        {
            ui.dword723c00 = *second;
            ++ui.dword723bfc;
            if (ui.dword723bfc >= ClockSeconds)
                ui.dword723bfc = 0;
        }
        if (minutes < 0)
            minutes += ClockSeconds;
    }
    if (ui.dword723bf4 != minutes)
    {
        ui.dword723bf4 = minutes;
        ui.dword723bfc = 0;
    }
    return minutes;
}

// 0x4370a8..0x437564: the defense state.
Result DrawTheState(Ui::GameHudContext& context, Kernel::CrywolfInterface& ui, std::uint8_t stage)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.Quad(QuadOf(StateFrameSlot, 0.0f, 1.0f, 122.0f, 98.0f, 0.953125f, 0.765625f));
    sprites.Quad(QuadOf(DecorationSlot, 4.0f, F32(static_cast<double>(ui.float723a40) + DecorationTop), 20.0f, 9.0f,
                        0.625f, 0.53125f));
    DrawTheAltars(sprites, ui);
    sprites.Quad(QuadOf(ui.dword723bd8 == 0 ? NoCountSlot : CountSlot, 4.0f, 81.0f, 15.0f, 15.0f, 0.9375f, 0.9375f));
    sprites.Quad(QuadOf(BottomBarSlot, 240.0f, 396.0f, 156.0f, 36.0f, 0.6015625f, 0.546875f));
    sprites.SetColor(StateColor);
    Kernel::HudState& hud = context.hud;
    hud.dcFont = FontKind::Normal;
    hud.textColors = Render::Text::TextColors{.text = TitleColor, .background = 0};
    auto countFormat = TextOf(context, CountText, "0x4372ba");
    if (!countFormat.HasValue())
        return Core::Unexpected(std::move(countFormat).Error());
    const std::array<Crt::FormatArgument, 1> countArguments{ui.dword723bd8};
    auto count = Formatted(countFormat.Value(), countArguments, "0x4372ba");
    if (!count.HasValue())
        return Core::Unexpected(std::move(count).Error());
    DrawCentered(context, 0x39, 0x55, count.Value());
    if (ui.byte723a3d == 1)
    {
        // FCOMP and C0: under 21 (or a NaN) drops one more.
        if (!(ui.float723a40 >= DecorationDown))
            ui.float723a40 = F32(static_cast<double>(ui.float723a40) + DecorationStep);
        else
        {
            sprites.Quad(QuadOf(BossBackSlot, 0.0f, 98.0f, 97.0f, 21.0f, 0.7578125f, 0.65625f));
            sprites.Quad(QuadOf(BossIconSlot, 4.0f, 101.0f, 15.0f, 15.0f, 0.9375f, 0.9375f));
            auto bossFormat = TextOf(context, BossText, "0x437369");
            if (!bossFormat.HasValue())
                return Core::Unexpected(std::move(bossFormat).Error());
            auto boss = Formatted(bossFormat.Value(), {}, "0x437369");
            if (!boss.HasValue())
                return Core::Unexpected(std::move(boss).Error());
            DrawCentered(context, 0x26, 0x65, boss.Value());
            const auto life = static_cast<double>(ui.dword6c73d4);
            sprites.Quad(QuadOf(BossBarSlot, 25.0f, 109.0f, F32(life * BossBarWidth), 8.0f,
                                F32(life * BossBarUv * UvPerPixel), 1.0f));
        }
    }
    // FCOMP and C0 or C3: above 0 (and not a NaN) rises one.
    if (ui.byte723a3d == 0 && ui.float723a40 > 0.0f)
        ui.float723a40 = F32(static_cast<double>(ui.float723a40) - DecorationStep);
    ui.byte723bf8 = 1;
    const auto clock = StepTheClock(ui, context.tickCount);
    if (!clock.HasValue())
        return Core::Unexpected(clock.Error());
    const std::int32_t minutes = clock.Value();
    sprites.SetColor(ui.byte723a3d == 0 ? White : AlarmColor);
    Ui::DrawNumber(sprites, MinutesX, ClockY, minutes, ClockDigit, ClockDigit);
    const std::int32_t seconds = ui.dword723bfc != 0 ? ClockSeconds - ui.dword723bfc : 0;
    Ui::DrawNumber(sprites, SecondsX, ClockY, seconds, ClockDigit, ClockDigit);
    const auto light = static_cast<double>(ui.light);
    sprites.Quad(
        QuadOf(LightSlot, 4.0f, 46.0f, F32(light * LightWidth), 30.0f, F32(light * LightUv * UvPerPixel), 0.9375f));
    if (auto tips = DrawTheTips(context, ui, stage); !tips.HasValue())
        return tips;
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace

Result DrawTheCrywolfInterface(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::CrywolfInterface& ui = context.crywolfInterface != nullptr ? *context.crywolfInterface : runtime.crywolf.ui;
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    if (ui.byte723bf9 == 1)
    {
        if (auto results = DrawTheResults(context, ui); !results.HasValue())
            return results;
    }
    DrawTheNotice(context.sprites, ui);
    if (!InTheCrywolfDefense(runtime))
        return {};
    return DrawTheState(context, ui, runtime.crywolf.stage);
}

Core::Expected<void, std::string> DrawTheCrywolfContract(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!InTheCrywolfDefense(runtime))
        return {};
    // 0x436264..0x43626e: before looking at the window.
    context.hud.textColors = Render::Text::TextColors{.text = TitleColor, .background = 0};
    Kernel::CrywolfInterface& ui = context.crywolfInterface != nullptr ? *context.crywolfInterface : runtime.crywolf.ui;
    if (ui.byte723a44 != QuestionWindow && ui.byte723a44 != NoticeWindow)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    sprites.Quad(QuadOf(ContractBoardSlot, 212.0f, 206.0f, 209.0f, 80.0f, 0.80859375f, 0.6015625f));
    const std::span<const ContractButton> buttons = ui.byte723a44 == QuestionWindow
                                                        ? std::span<const ContractButton>(QuestionButtons)
                                                        : std::span<const ContractButton>(NoticeButtons);
    for (const ContractButton& button : buttons)
    {
        const Ui::Point mouse = context.mouse;
        const bool over = mouse.x > button.left && mouse.x < button.right && mouse.y > ContractButtonTop &&
                          mouse.y < ContractButtonBottom;
        std::uint16_t slot = button.slot;
        if (over)
            slot = ui.dwords723bdc[0] == button.pushed ? button.pushSlot : button.hoverSlot;
        sprites.Quad(QuadOf(slot, button.x, ContractButtonY, ButtonWidth, ButtonHeight, 0.84375f, 0.9375f));
        if (over && ui.dwords723bdc[0] == button.pushed)
        {
            // 0x43630e, 0x4363b7 and 0x4364da: the rest is drawn the same.
            ui.byte723a44 = 0;
            ui.dwords723bdc[0] = 0;
        }
    }
    // 0x4363fe..0x43656c: the lines as C strings, the first higher when there is a second.
    const auto lineOf = [](const std::array<char, 200>& line)
    { return std::string_view(line.data(), std::char_traits<char>::length(line.data())); };
    const std::string_view first = lineOf(ui.contractLines[0]);
    const std::string_view second = lineOf(ui.contractLines[1]);
    if (!first.empty())
        DrawCentered(context, ContractTextX, second.empty() ? ContractSingleLineY : ContractFirstLineY, first);
    if (!second.empty())
        DrawCentered(context, ContractTextX, ContractSecondLineY, second);
    return {};
}
} // namespace Client::Events::Crywolf
