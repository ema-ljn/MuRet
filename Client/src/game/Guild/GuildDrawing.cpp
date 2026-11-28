#include "Guild/GuildDrawing.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Hud/GameWindowDrawing.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildState.h"
#include "Render/Frame/ITextDrawer.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Chat/ChatInput.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Ui/PanelTimerFrame.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Chat/ChatRuntime.h"
#include "Events/EventGlobals.h"
#include "Ui/GameUiState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Guild
{
namespace
{
using Ui::WindowDrawing::DrawText;
using Ui::WindowDrawing::Quad;
using Ui::WindowDrawing::SelectFont;
using Render::BlendMode;
using Render::Text::FontKind;

// 0x5a764e..0x5a7653.
constexpr std::uint32_t ScoreBackground = 0x80000000u;
// 0x5a768a..0x5a76a7: world width · 0.5 (0x6b882c) − 20 (0x6b86d0), in float.
constexpr double HalfOfTheWorld = 0.5;
constexpr double ScoreLeft = 20.0;
// 0x5a76b0..0x5a76d3 and 0x5a7843..0x5a786a: NEG and SBB with the team (0x576796e).
constexpr std::uint32_t OrangeText = 0xffff9600u;
constexpr std::uint32_t RedText = 0xff003cffu;
// 0x5a76dd..0x5a7706 and 0x5a78c4..0x5a78ed: 0x621b60(0x22, x, y, 8, 8, 0, 0, 1, 1, 1, 1, 0).
constexpr float MarkSize = 8.0f;
constexpr float OursTop = 4.0f;
constexpr float TheirsTop = 19.0f;
constexpr std::int32_t OursTextTop = 4;
constexpr std::int32_t TheirsTextTop = 0x13;
// 0x5a775a and 0x5a77fb: the texts at _ftol(x) + 0xd and + 0x1e.
constexpr std::int32_t ScoreOffset = 0xd;
constexpr std::int32_t NameOffset = 0x1e;
// 0x5a785f..0x5a78b8: the rival search, from entry 0 to 1998 (EDI < 0x78a597d).
constexpr std::size_t RivalSearched = GuildTableSize - 1;

// 0x6576a0: Message_box1 (0xfb) and Message_box2 (0xfc) of 0x61ccb0, 213 wide at x = 214, with the UVs (0, 0,
// 0.83203125, 0.625) and glColor4f(1, 1, 1, 0.8); the edges of 5 (0x6b87f8) and the rows of 40 (0x6b8bf4) for every 40
// of (height − 10); the rest, (rest · 0.025 (0x6ba1fc)) · 40 high with dv = (rest · 0.025) · 0.625 (0x6baad0).
constexpr std::uint16_t BoxRowSlot = 0xfb;
constexpr std::uint16_t BoxEdgeSlot = 0xfc;
constexpr float BoxX = 214.0f;
constexpr float BoxWidth = 213.0f;
constexpr float BoxEdge = 5.0f;
constexpr float BoxRow = 40.0f;
constexpr std::int32_t BoxRowStep = 40;
constexpr std::int32_t BoxMargin = 10;
constexpr float BoxDu = 0.83203125f;
constexpr float BoxDv = 0.625f;
constexpr float BoxRestScale = 0.025f;
constexpr Render::Color BoxColor{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 0.8f};
constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};
// 0x6608c6..0x660968: colors −1 and 0, the rows centered on 0x140 from y + 0x14, 15 (0x6b87bc) apart.
constexpr Render::Text::TextColors DialogColors{.text = 0xffffffffu, .background = 0u};
constexpr std::int32_t DialogCenterX = 0x140;
constexpr std::int32_t FirstLineOffset = 0x14;
constexpr float LineStep = 15.0f;
// 0x65ff12..0x65ff9c: the texts 0x65feb0 copies to the buttons (vtable + 0x2c, 0x45b170): Accept or Yes, No, Confirm
// and Cancel.
constexpr std::array<std::size_t, Ui::PanelDialogButtonCount> ButtonTexts{228, 229, 1037, 1038};
// 0x64f1c0, the positions window: 0x6576a0(0x96, −1)'s box, at (0xd6, 0x82 − 0x4b) with a minimum of 40.
constexpr std::int32_t PositionsBoxHeight = 0x96;
constexpr std::int32_t PanelBoxLeft = 0xd6;
constexpr std::int32_t PanelCenterY = 0x82;
constexpr std::int32_t PanelBoxMinimumTop = 40;
constexpr std::int32_t PositionsBoxTop = PanelCenterY - PositionsBoxHeight / 2 < PanelBoxMinimumTop
                                             ? PanelBoxMinimumTop
                                             : PanelCenterY - PositionsBoxHeight / 2;
// 0x64f1f4..0x64f377: the two 170 × 20 boxes at x + 0x14 (y + 0x13 and y + 0x27; the chosen position's is filled) and
// the texts at x + 0x14 centered in _ftol(rx · 170) (0x6b899c): 1311 at y + 0x18, 1312 at y + 0x2e, 1314 with the
// name and 1301 or 1302 at y + 0x4b and 1315 at y + 0x5d.
constexpr std::int32_t PositionsLeft = 0x14;
constexpr std::int32_t AssistantFrameTop = 0x13;
constexpr std::int32_t BattleMasterFrameTop = 0x27;
constexpr float PositionsFrameWidth = 170.0f;
constexpr float PositionsFrameHeight = 20.0f;
constexpr double PositionsTextBox = 170.0;
constexpr std::size_t AssistantText = 1311;
constexpr std::int32_t AssistantTextTop = 0x18;
constexpr std::size_t BattleMasterText = 1312;
constexpr std::int32_t BattleMasterTextTop = 0x2e;
constexpr std::size_t PositionsQuestionText = 1314;
constexpr std::int32_t PositionsQuestionTop = 0x4b;
constexpr std::size_t AssistantNameText = 1301;
constexpr std::size_t BattleMasterNameText = 1302;
constexpr std::size_t PositionsHintText = 1315;
constexpr std::int32_t PositionsHintTop = 0x5d;
// 0x64f37c..0x64f3cb: the global buttons at (x + 0x37, y + 0x75) and (x + 0x6e, y + 0x75), with the texts 0x64f3e0
// gives them (0x45b150).
constexpr std::int32_t PositionsButtonsTop = 0x75;
constexpr std::array<std::int32_t, GuildPositionsButtonCount> PositionsButtonLefts{0x37, 0x6e};
constexpr std::array<std::size_t, GuildPositionsButtonCount> PositionsButtonTexts{228, 229};

using Math::F32;
using Math::Truncate;

template <std::size_t Size> std::string_view NameOf(const std::array<char, Size>& name)
{
    const auto end = std::ranges::find(name, '\0');
    return {name.data(), static_cast<std::size_t>(end - name.begin())};
}

Core::Expected<std::string_view, std::string> PositionsTextOf(const Ui::GameHudContext& context, std::size_t index,
                                                              std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x5542f0(x, y, text, box, 2, 0): the text centered in the box (0x467060 in mode 2), without measuring.
void DrawInTheMiddle(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                     std::int32_t box)
{
    if (text.empty() && box == 0)
        return;
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.hud.dcFont, text, box);
    context.text.DrawColumns(x, y, text,
                             Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                             -placed.textColumn, placed.width);
}

// 0x5a7870..0x5a78b8: strcmp of each entry's +0xd with the rival; with none, EAX stays at 0 (0x5a78ba).
std::int32_t MarkOf(const GuildState& guild, std::string_view rival)
{
    for (std::size_t index = 0; index < RivalSearched; ++index)
    {
        if (NameOf(guild.table[index].guildName) == rival)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}

// 0x5f16a0(index, 1) and the quad of the slot where it landed, at (x, top).
Core::Expected<void, std::string> DrawTheMark(Ui::GameHudContext& context, const GuildState& guild, std::int32_t index,
                                              float x, float top)
{
    auto slot = context.guildMarks->Upload(guild, index, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    Quad(context.sprites, slot.Value().value, x, top, MarkSize, MarkSize, 1.0f, 1.0f);
    return {};
}

// 0x6576a0(height, y): the box from the y it chose (an integer, without float rounding).
void DrawTheDialogBox(Render::SpriteBatch& sprites, std::int32_t height, std::int32_t top)
{
    sprites.SetBlendWithoutAlphaTest();
    sprites.SetColor(BoxColor);
    auto y = static_cast<float>(top);
    Quad(sprites, BoxEdgeSlot, BoxX, y, BoxWidth, BoxEdge, BoxDu, BoxDv);
    // 0x657741..0x657764: (height − 10) / 40 with the signed division's rounding toward 0.
    const std::int32_t rest = height - BoxMargin;
    const std::int32_t rows = rest / BoxRowStep;
    y = F32(static_cast<double>(y) + static_cast<double>(BoxEdge));
    for (std::int32_t row = 0; row < rows; ++row)
    {
        Quad(sprites, BoxRowSlot, BoxX, y, BoxWidth, BoxRow, BoxDu, BoxDv);
        y = F32(static_cast<double>(y) + static_cast<double>(BoxRow));
    }
    if (rest > rows * BoxRowStep)
    {
        // 0x6577c5..0x6577e8: the product stays on the x87 and is stored as a float twice; with a small rest, exact.
        const double scaled = static_cast<double>(rest - rows * BoxRowStep) * static_cast<double>(BoxRestScale);
        const float last = F32(scaled * static_cast<double>(BoxRow));
        Quad(sprites, BoxRowSlot, BoxX, y, BoxWidth, last, BoxDu, F32(scaled * static_cast<double>(BoxDv)));
        y = F32(static_cast<double>(last) + static_cast<double>(y));
    }
    Quad(sprites, BoxEdgeSlot, BoxX, y, BoxWidth, BoxEdge, BoxDu, BoxDv);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
}

// 0x5542f0(x, y, text, 0, 1, 0): 0x465950 with the DC's font and 0x6c8e5c's colors; an empty one is not drawn. It does
// not measure (0x77f31b8 is 0x554180's).
void DrawTheLine(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    if (text.empty())
        return;
    context.text.Draw(x, y, text,
                      Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors});
}
} // namespace

namespace
{
Core::Expected<void, std::string> DrawTheGuildWarScore(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::WorldState& world = context.world;
    const GuildState& guild = world.Runtime().guild;
    const auto heroGuild = static_cast<std::int16_t>(world.Hero().word1f2);
    if (guild.warOn == 0 || heroGuild == NoGuildIndex)
        return {};
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x5a76d8: la marca del gremio (0x5f16a0) sin la tabla de las marcas"));
    if (heroGuild < 0 || static_cast<std::size_t>(heroGuild) >= GuildTableSize)
        return Core::Unexpected(std::format("0x5a77c7: el gremio {} del héroe cae fuera de la tabla", heroGuild));
    sprites.SetBlend(BlendMode::Alpha);
    const auto x = static_cast<float>(static_cast<double>(panels.WorldWidth()) * HalfOfTheWorld - ScoreLeft);
    SelectFont(context, FontKind::Bold);
    const bool team = guild.warTeam != 0;
    context.hud.textColors.text = team ? OrangeText : RedText;
    if (auto drawn = DrawTheMark(context, guild, heroGuild, x, OursTop); !drawn.HasValue())
        return drawn;
    const auto left = static_cast<std::int32_t>(x);
    DrawText(context, left + ScoreOffset, OursTextTop, std::format("{}", guild.ourScore));
    DrawText(context, left + NameOffset, OursTextTop,
             NameOf(guild.table[static_cast<std::size_t>(heroGuild)].guildName));
    context.hud.textColors.text = team ? RedText : OrangeText;
    if (auto drawn = DrawTheMark(context, guild, MarkOf(guild, NameOf(guild.rival)), x, TheirsTop); !drawn.HasValue())
        return drawn;
    DrawText(context, left + ScoreOffset, TheirsTextTop, std::format("{}", guild.theirScore));
    // 0x5a79b0: 0x5542f0(…, 0, 1, 0) is 0x554180's mode 1 and measures the same as the 0x5a79ba measure that follows.
    DrawText(context, left + NameOffset, TheirsTextTop, NameOf(guild.rival));
    return {};
}

// 0x5a7a01..0x5a7cbd: the received names look up their marks in the current table, even without a war.
Core::Expected<void, std::string> DrawTheSoccerScore(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    const auto& soccer = context.world.Runtime().events.soccer;
    if (!soccer.active)
        return {};
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x5a7a81: la marca de Soccer sin la tabla de las marcas"));
    const GuildState& guild = context.world.Runtime().guild;
    context.sprites.SetBlend(BlendMode::Alpha);
    const auto x = static_cast<float>(static_cast<double>(panels.WorldWidth()) * HalfOfTheWorld - ScoreLeft);
    const auto left = static_cast<std::int32_t>(x);
    SelectFont(context, FontKind::Bold);
    context.hud.textColors.text = RedText;
    if (auto mark = DrawTheMark(context, guild, MarkOf(guild, NameOf(soccer.firstName)), x, OursTop); !mark.HasValue())
        return mark;
    DrawText(context, left + ScoreOffset, OursTextTop, std::format("{}", guild.ourScore));
    DrawText(context, left + NameOffset, OursTextTop, NameOf(soccer.firstName));
    context.hud.textColors.text = OrangeText;
    if (auto mark = DrawTheMark(context, guild, MarkOf(guild, NameOf(soccer.secondName)), x, TheirsTop);
        !mark.HasValue())
        return mark;
    DrawText(context, left + ScoreOffset, TheirsTextTop, std::format("{}", guild.theirScore));
    DrawText(context, left + NameOffset, TheirsTextTop, NameOf(soccer.secondName));
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheWarScore(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    context.sprites.SetBlend(BlendMode::Opaque);
    context.hud.textColors.background = ScoreBackground;
    if (auto war = DrawTheGuildWarScore(context, panels); !war.HasValue())
        return war;
    return DrawTheSoccerScore(context, panels);
}

Core::Expected<void, std::string> DrawTheEventTimeAndOutcome(Ui::GameHudContext& context)
{
    auto& events = context.world.Runtime().events;
    auto& hud = context.hud;
    // 0x5a7f1c..0x5a8006: both texts use the same world width and the big font; the second is the shadow.
    if (events.timeValue != 0 && events.timeValue / 60 <= 10)
    {
        SelectFont(context, FontKind::BigBold);
        hud.textColors.text = 0xff0a0affu;
        hud.textColors.background = 0;
        const std::size_t index = events.timeKind == 3 ? 1392 : events.timeValue % 60 < 10 ? 1390 : 1391;
        auto format = PositionsTextOf(context, index, "0x5a7f9b");
        if (!format.HasValue())
            return Core::Unexpected(std::move(format).Error());
        const std::string text = events.timeKind == 3
                                     ? Crt::Sprintf(format.Value(), events.timeValue % 60)
                                     : Crt::Sprintf(format.Value(), events.timeValue / 60, events.timeValue % 60);
        if (events.timeKind == 3 || events.timeValue < 60)
            hud.textColors.text = 0xff0affffu;
        Hud::GamePanels panels(context.world, context.random);
        const auto center = panels.Registry().WorldWidth() / 2;
        Ui::WindowDrawing::DrawCenteredText(context, center, 350, text);
        hud.textColors.text = 0xffffffffu;
        Ui::WindowDrawing::DrawCenteredText(context, center + 1, 351, text);
        SelectFont(context, FontKind::Normal);
    }
    const auto& outcome = events.guildWarOutcome;
    // 0x5a801e: the strcmp compares only one byte with the empty string, without checking present.
    if (outcome.firstName[0] == '\0')
        return {};
    auto& sprites = context.sprites;
    Quad(sprites, 0xfc, 170, 120, 300, 5, 0.5859375f, 0.625f);
    for (int row = 0; row < 5; ++row)
        Quad(sprites, 0xfb, 170, static_cast<float>(125 + row * 40), 300, 40, 0.83203125f, 0.625f);
    Quad(sprites, 0xfc, 170, 325, 300, 5, 0.5859375f, 0.625f);
    sprites.SetBlend(BlendMode::Additive);
    sprites.SetColor(White);
    SelectFont(context, FontKind::BigBold);
    const auto line = [&](std::int32_t x, std::int32_t y, std::size_t index) -> Core::Expected<void, std::string>
    {
        auto text = PositionsTextOf(context, index, "0x5a7eb0");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawText(context, x, y, Crt::Sprintf(text.Value()));
        return {};
    };
    hud.textColors.text = 0xfffff0c8u;
    if (auto drawn = line(270, 140, 1393); !drawn.HasValue())
        return drawn;
    hud.textColors.text = 0xff0affffu;
    if (auto drawn = line(307, 170, 1394); !drawn.HasValue())
        return drawn;
    hud.textColors.text = 0xffffffffu;
    const std::string first(NameOf(outcome.firstName));
    // 0x5767904 follows right after the second name: the sprintf reads its bytes if the zero is missing in the ten.
    std::array<char, 13> secondBytes{};
    std::ranges::copy(outcome.secondName, secondBytes.begin());
    secondBytes[10] = static_cast<char>(outcome.secondScore & 0xffu);
    secondBytes[11] = static_cast<char>(outcome.secondScore >> 8);
    const std::string second(NameOf(secondBytes));
    DrawText(context, 230 - static_cast<std::int32_t>(first.size()) * 5, 170, first);
    DrawText(context, 400 - static_cast<std::int32_t>(second.size()) * 5, 170, second);
    DrawText(context, 215, 195, std::format("({})", outcome.firstScore));
    DrawText(context, 385, 195, std::format("({})", outcome.secondScore));
    if (outcome.firstScore == outcome.secondScore)
    {
        hud.textColors.text = 0xff0affffu;
        if (auto drawn = line(285, 235, 1395); !drawn.HasValue())
            return drawn;
        SelectFont(context, FontKind::Normal);
        hud.textColors.text = 0xffffffffu;
    }
    else
    {
        const bool firstWon = outcome.firstScore > outcome.secondScore;
        hud.textColors.text = 0x0a0affffu;
        if (auto drawn = line(217, 235, firstWon ? 1396 : 1397); !drawn.HasValue())
            return drawn;
        hud.textColors.text = 0xff0a0affu;
        if (auto drawn = line(388, 235, firstWon ? 1397 : 1396); !drawn.HasValue())
            return drawn;
    }
    SelectFont(context, FontKind::Normal);
    const bool hover =
        context.mouse.x >= 285 && context.mouse.x < 355 && context.mouse.y >= 280 && context.mouse.y < 300;
    Quad(sprites, hover ? 0xf2 : 0xf1, 285, 280, 70, 20, 0.546875f, 0.625f);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}

Core::Expected<void, std::string> DrawTheGuildPositions(Ui::GameHudContext& context)
{
    GuildState& guild = context.world.Runtime().guild;
    GuildPositionsState& positions = guild.positions;
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheDialogBox(sprites, PositionsBoxHeight, PositionsBoxTop);
    positions.x = PanelBoxLeft;
    positions.y = PositionsBoxTop;
    const std::int32_t x = positions.x;
    const std::int32_t y = positions.y;
    // 0x64f1ef: 0x620a00(1).
    sprites.SetBlend(BlendMode::Alpha);
    const auto rx = static_cast<double>(Render::ScaleX(1.0f, sprites.Screen()));
    const std::int32_t box = Truncate(rx * PositionsTextBox);
    const std::int32_t left = x + PositionsLeft;
    Ui::WindowDrawing::DrawTheFrame(sprites, static_cast<float>(left), static_cast<float>(y + AssistantFrameTop),
                                     PositionsFrameWidth, PositionsFrameHeight, positions.choice == AssistantChoice);
    auto assistant = PositionsTextOf(context, AssistantText, "0x64f25c");
    if (!assistant.HasValue())
        return Core::Unexpected(std::move(assistant).Error());
    DrawInTheMiddle(context, left, y + AssistantTextTop, assistant.Value(), box);
    Ui::WindowDrawing::DrawTheFrame(sprites, static_cast<float>(left), static_cast<float>(y + BattleMasterFrameTop),
                                     PositionsFrameWidth, PositionsFrameHeight, positions.choice == BattleMasterChoice);
    auto battleMaster = PositionsTextOf(context, BattleMasterText, "0x64f2d9");
    if (!battleMaster.HasValue())
        return Core::Unexpected(std::move(battleMaster).Error());
    DrawInTheMiddle(context, left, y + BattleMasterTextTop, battleMaster.Value(), box);
    // 0x64f2e8..0x64f310: sprintf with 1314, the name from 0x7d535a8 and the position (1301 with 0x40, otherwise 1302).
    auto question = PositionsTextOf(context, PositionsQuestionText, "0x64f30a");
    if (!question.HasValue())
        return Core::Unexpected(std::move(question).Error());
    auto position = PositionsTextOf(
        context, positions.choice == AssistantChoice ? AssistantNameText : BattleMasterNameText, "0x64f2f3");
    if (!position.HasValue())
        return Core::Unexpected(std::move(position).Error());
    DrawInTheMiddle(context, left, y + PositionsQuestionTop,
                    Crt::Sprintf(question.Value(), NameOf(guild.chosenName), position.Value()), box);
    auto hint = PositionsTextOf(context, PositionsHintText, "0x64f36d");
    if (!hint.HasValue())
        return Core::Unexpected(std::move(hint).Error());
    DrawInTheMiddle(context, left, y + PositionsHintTop, hint.Value(), box);
    const Ui::OldWindowDrawContext buttons{
        .sprites = sprites, .text = context.text, .dcFont = context.hud.dcFont, .textColors = context.hud.textColors};
    const Ui::OldWindowDrawView view{.mouseX = context.mouse.x, .mouseY = context.mouse.y};
    for (std::size_t index = 0; index < positions.buttons.size(); ++index)
    {
        Ui::OldButton& button = positions.buttons[index];
        if (!button.label)
        {
            auto label = PositionsTextOf(context, PositionsButtonTexts[index], "0x64f5f7");
            if (!label.HasValue())
                return Core::Unexpected(std::move(label).Error());
            button.label = std::string(label.Value());
        }
        button.base.x = x + PositionsButtonLefts[index];
        button.base.y = y + PositionsButtonsTop;
        Ui::DrawTheOldButton(buttons, button, view);
    }
    return {};
}

Core::Expected<void, std::string> DrawThePanelDialog(Ui::GameHudContext& context, Ui::PanelDialogState& dialog)
{
    if (Ui::PanelDialogHandle(dialog) == 0)
        return {};
    // 0x660846: +0xc, the own drawing that 0x6602b0 sets, instead of everything.
    if (dialog.hooks == Ui::PanelDialogHooks::GuildPositions)
        return DrawTheGuildPositions(context);
    for (std::size_t button = 0; button < dialog.buttons.size(); ++button)
    {
        // 0x65feb0 copies them when it is built, after Text.bmd (0x4b9b9f); MuRet, the first time it draws.
        if (dialog.buttons[button].label)
            continue;
        const std::optional<std::string_view> label = Ui::TextAt(context.texts, ButtonTexts[button]);
        if (!label)
            return Core::Unexpected(
                std::format("0x65ff12: el botón {} del diálogo de los paneles sin el texto {} de Text.bmd", button,
                            ButtonTexts[button]));
        dialog.buttons[button].label = std::string(*label);
    }
    // 0x660856..0x6608a1 and the buttons' 0x45a9d0: the box's corner and the buttons' y.
    const Ui::Point corner = Ui::PlaceThePanelDialog(dialog);
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheDialogBox(sprites, dialog.height, corner.y);
    SelectFont(context, FontKind::Normal);
    sprites.SetBlend(BlendMode::Alpha);
    context.hud.textColors = DialogColors;
    auto y = static_cast<float>(corner.y + FirstLineOffset);
    const std::int32_t count = std::min(dialog.lineCount, static_cast<std::int32_t>(dialog.lines.size()));
    for (std::int32_t line = 0; line < count; ++line)
    {
        const std::string& text = dialog.lines[static_cast<std::size_t>(line)];
        // 0x6608f2..0x66093f: GetTextExtentPoint32A with the DC, cx / rx with _ftol and the signed half.
        const std::int32_t width =
            Render::Text::DesignX(context.text.Measure(context.hud.dcFont, text).width, sprites.Screen());
        DrawTheLine(context, DialogCenterX - width / 2, static_cast<std::int32_t>(y), text);
        y = F32(static_cast<double>(y) + static_cast<double>(LineStep));
    }
    if ((dialog.flags & Ui::PanelDialogTextField) != 0)
    {
        const auto width = dialog.inputWidth;
        Quad(sprites, 0x10f, static_cast<float>(320 - width / 2), y, static_cast<float>(width), 18, 0.8828125f,
             0.5625f);
        y = F32(static_cast<double>(y) + 7.0);
        auto& runtime = context.world.Runtime();
        Ui::OldEdit projected = runtime.chat.input.dialogEdit;
        auto& edit = context.projecting ? projected : runtime.chat.input.dialogEdit;
        edit.base.state = Ui::ShownWindow;
        edit.filter = dialog.inputFilter;
        edit.backgroundColor = 0;
        edit.text.SetLimit(dialog.inputMaxChars);
        edit.base.width = width;
        edit.base.height = 14;
        edit.base.x = 325 - width / 2;
        edit.base.y = static_cast<std::int32_t>(y);
        if (!context.projecting)
        {
            Chat::FocusTheDialogField(runtime.chat.input);
            runtime.ui.focusedList = edit.base.id;
        }
        Ui::OldDraw::DrawTheOldEdit({.sprites = sprites,
                                     .text = context.text,
                                     .dcFont = context.hud.dcFont,
                                     .textColors = context.hud.textColors},
                                    edit, true);
        y = F32(static_cast<double>(y) + 10.0);
    }
    if ((dialog.flags & Ui::PanelDialogTimed) != 0)
    {
        y = F32(static_cast<double>(y) + 7.0);
        const auto frame = Ui::PanelTimerFrameAt(context.tickCount, dialog.timerStart, dialog.timerEnd);
        if (!context.projecting)
            dialog.elapsedSeconds = frame.elapsedSeconds;
        Quad(sprites, 0x667, 245, y, 150, 12, 0.78125f, 1);
        sprites.SetBlend(BlendMode::Additive);
        Quad(sprites, 0x668, 241, y, frame.width, 12, frame.du, 1);
        sprites.SetBlend(BlendMode::Opaque);
    }
    const Ui::OldWindowDrawContext buttons{
        .sprites = sprites, .text = context.text, .dcFont = context.hud.dcFont, .textColors = context.hud.textColors};
    const Ui::OldWindowDrawView view{.mouseX = context.mouse.x, .mouseY = context.mouse.y};
    if ((dialog.flags & Ui::PanelDialogOk) != 0)
        Ui::DrawTheOldButton(buttons, dialog.buttons[Ui::PanelOkButton], view);
    else if ((dialog.flags & Ui::PanelDialogYesNo) != 0)
    {
        Ui::DrawTheOldButton(buttons, dialog.buttons[Ui::PanelOkButton], view);
        Ui::DrawTheOldButton(buttons, dialog.buttons[Ui::PanelNoButton], view);
    }
    else if ((dialog.flags & Ui::PanelDialogConfirm) != 0)
    {
        Ui::DrawTheOldButton(buttons, dialog.buttons[Ui::PanelConfirmButton], view);
        Ui::DrawTheOldButton(buttons, dialog.buttons[Ui::PanelCancelButton], view);
    }
    return {};
}
} // namespace Client::Guild
