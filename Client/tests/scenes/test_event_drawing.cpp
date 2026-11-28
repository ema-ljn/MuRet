#include "Events/EventDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Events/EventMapObject.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Events/EventGlobals.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// CL9 milestone 11, phase 2 (06 § Los eventos): the event drawings in the 2D pass, against the ASM of c32bbd5f with no
// GL trace (*static*; what the VM has to confirm is in 08, P-C294). Text.bmd is made up, with the specifiers of the
// original's texts.
namespace
{
using Client::Crt::CrtRandom;
using Client::Events::EventMapKind;
using Client::Events::EventMapObject;
using Client::Ui::GameHudContext;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::uint32_t WhiteText = 0xffffffffu;

std::map<std::size_t, std::string> EventTexts()
{
    return {
        {647, "bien hecho"},
        {648, "%s aguanto la prueba"},
        {680, "puesto"},
        {681, "quien"},
        {682, "puntos"},
        {683, "exp"},
        {684, "premio"},
        {685, "lo mio"},
        {857, "ganaste"},
        {858, "mision cumplida"},
        {859, "perdiste"},
        {860, "mision sin cumplir"},
        {861, "exp %d"},
        {862, "zen %d"},
        {863, "puntos %d"},
        {864, "bichos ( %d/%d )"},
        {865, "queda"},
        {866, "pelota ( %d/%d )"},
        {1151, "el guardia descansa"},
        {1152, "la mision"},
        {1153, "otra vez sera"},
        {1161, "rivales ( %d/%d )"},
        {1162, "monstruos %d"},
        {1163, "jugadores %d"},
        {1390, "faltan %d:0%d"},
        {1391, "faltan %d:%d"},
        {1392, "arranca en %d"},
    };
}

// The color of each text mark of the batch: the glColor there was when it was drawn.
std::vector<Color> MarkerColors(const SpriteBatch& sprites)
{
    std::vector<Color> colors;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            colors.push_back(vertices[command.firstVertex + quad * 6].color);
    }
    return colors;
}

// The color in the batch: that of an untextured rectangle drawn now (the test's last step).
Color ColorNow(SpriteBatch& sprites)
{
    sprites.ColorQuad(0.0f, 0.0f, 1.0f, 1.0f);
    return sprites.Vertices().back().color;
}

void ExpectColor(const Color& drawn, const Color& expected)
{
    EXPECT_FLOAT_EQ(drawn.red, expected.red);
    EXPECT_FLOAT_EQ(drawn.green, expected.green);
    EXPECT_FLOAT_EQ(drawn.blue, expected.blue);
    EXPECT_FLOAT_EQ(drawn.alpha, expected.alpha);
}

class EventDrawingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
    {
        Client::Kernel::WorldRuntime& runtime = world->Runtime();
        Client::Kernel::ActorState& hero = world->Hero();
        return GameHudContext{.world = *world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .projecting = projecting,
                              .sprites = sprites,
                              .text = text,
                              .texts = &texts,
                              .worldClock = worldClock};
    }

    // The x of 0x623690 with the measure of the tests' ITextDrawer.
    static int Centered(int x, std::string_view line)
    {
        return Client::Render::Text::CenteredAt(
            x, {static_cast<int>(line.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height},
            Screen640);
    }

    // A new frame: the batch empty and the text globals as the HUD leaves them.
    void Restart()
    {
        world->Runtime().hud.dcFont = FontKind::Normal;
        world->Runtime().hud.textColors = {};
        text.texts.clear();
        sprites.Begin(Screen640);
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{Seed};
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    RecordingTextDrawer text{sprites};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(EventTexts());
    float worldClock = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------
// 0x5a7eb0: the F3 22 time.

// With the word at 0, or with more than 10 minutes (660 s or more), it does not draw, draws no rand() (0x5bb8d0 does
// not run) and leaves the colors and the font as they were.
TEST_F(EventDrawingTest, TheEventTimeDrawsNothingWithoutTimeOrPastTenMinutes)
{
    for (const std::uint16_t time : {std::uint16_t{0}, std::uint16_t{660}, std::uint16_t{719}, std::uint16_t{0xffff}})
    {
        SCOPED_TRACE(time);
        world->Runtime().events.timeValue = time;
        const std::uint32_t before = random.State();
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheEventTime(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(text.texts.empty());
        EXPECT_EQ(random.State(), before);
        EXPECT_EQ(world->Runtime().hud.dcFont, FontKind::Normal);
        EXPECT_EQ(world->Runtime().hud.textColors.text, WhiteText);
    }
}

// Up to 10 minutes: 1390 ("%d:0%d") under 10 seconds and 1391 ("%d:%d") with 10 or more, in the big font, in
// 0xff0a0aff (0xff0affff under a minute) at (world middle, 350) and in white at (middle + 1, 351). The middle comes
// from 0x5bb8d0, with its rand(). The same in Lorencia, Devias and a castle.
TEST_F(EventDrawingTest, TheEventTimeDrawsTwiceCenteredOnTheWorld)
{
    struct Case
    {
        std::uint16_t time;
        std::string_view line;
        std::uint32_t color;
    };
    const std::array<Case, 7> cases{{{1, "faltan 0:01", 0xff0affffu},
                                     {59, "faltan 0:59", 0xff0affffu},
                                     {60, "faltan 1:00", 0xff0a0affu},
                                     {70, "faltan 1:10", 0xff0a0affu},
                                     {125, "faltan 2:05", 0xff0a0affu},
                                     {600, "faltan 10:00", 0xff0a0affu},
                                     {659, "faltan 10:59", 0xff0a0affu}}};
    for (const std::int32_t map : {0, 2, 11})
    {
        SCOPED_TRACE(map);
        world->Runtime().map = map;
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(probe.time);
            Restart();
            world->Runtime().events.timeValue = probe.time;
            world->Runtime().events.timeKind = 0;
            CrtRandom expected = random;
            const std::int32_t width = Client::Hud::GamePanels(*world, expected).Registry().WorldWidth();
            const int half = width / 2;

            GameHudContext context = Context();
            const auto drawn = Client::Events::DrawTheEventTime(context);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

            ASSERT_EQ(text.texts.size(), 2u);
            const RecordedText& first = text.texts[0];
            EXPECT_EQ(first.text, probe.line);
            EXPECT_EQ(first.x, Centered(half, probe.line));
            EXPECT_EQ(first.y, 350);
            EXPECT_EQ(first.style.font, FontKind::BigBold);
            EXPECT_EQ(first.style.colors.text, probe.color);
            EXPECT_EQ(first.style.colors.background, 0u);
            const RecordedText& second = text.texts[1];
            EXPECT_EQ(second.text, probe.line);
            EXPECT_EQ(second.x, Centered(half + 1, probe.line));
            EXPECT_EQ(second.y, 351);
            EXPECT_EQ(second.style.font, FontKind::BigBold);
            EXPECT_EQ(second.style.colors.text, WhiteText);
            EXPECT_EQ(random.State(), expected.State());
            // 0x5a7ff4..0x5a8006: the normal font and the white text.
            EXPECT_EQ(world->Runtime().hud.dcFont, FontKind::Normal);
            EXPECT_EQ(world->Runtime().hud.textColors.text, WhiteText);
        }
    }
}

// With type 3 (0x5767814), 1392 with the seconds and always in 0xff0affff, even with more than a minute left.
TEST_F(EventDrawingTest, TheStartKindDrawsTheSecondsInTheLastMinuteColor)
{
    world->Runtime().events.timeKind = 3;
    for (const std::uint16_t time : {std::uint16_t{30}, std::uint16_t{125}})
    {
        SCOPED_TRACE(time);
        Restart();
        world->Runtime().events.timeValue = time;
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheEventTime(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(text.texts.size(), 2u);
        EXPECT_EQ(text.texts[0].text, time == 30 ? "arranca en 30" : "arranca en 5");
        EXPECT_EQ(text.texts[0].style.colors.text, 0xff0affffu);
        EXPECT_EQ(text.texts[1].style.colors.text, WhiteText);
    }
}

// The sprintf goes to 28 stack bytes ([ebp-0x1c]): a text that does not fit stops before drawing.
TEST_F(EventDrawingTest, AnEventTimeLongerThanItsStackBufferCuts)
{
    std::map<std::size_t, std::string> longTexts = EventTexts();
    longTexts[1391] = "un tiempo demasiado largo %d:%d";
    texts = Client::Tests::MakeTextTable(longTexts);
    world->Runtime().events.timeValue = 70;
    GameHudContext context = Context();
    const auto drawn = Client::Events::DrawTheEventTime(context);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5a7f9b"), std::string::npos) << drawn.Error();
    EXPECT_TRUE(text.texts.empty());
}

// ---------------------------------------------------------------------------------------------------------------
// 0x40c2d0 and 0x405090: the time and the points of Blood Castle and Chaos Castle.

EventMapObject Castle(EventMapKind kind, std::uint8_t state, std::int32_t remaining, std::int32_t target,
                      std::int32_t kills)
{
    EventMapObject event;
    event.kind = kind;
    event.field10 = state;
    event.field14 = {900, remaining, target, kills};
    return event;
}

// Devil Square (0x40bdb0 is a ret), a castle without a state or outside its maps (Lorencia, Devias, the other castle)
// do not draw or touch the batch.
TEST_F(EventDrawingTest, TheCastleTimeDrawsNothingOutsideItsMaps)
{
    struct Case
    {
        EventMapKind kind;
        std::uint8_t state;
        std::int32_t map;
    };
    for (const Case& probe : {Case{EventMapKind::DevilSquare, 1, 11}, Case{EventMapKind::BloodCastle, 0, 11},
                              Case{EventMapKind::BloodCastle, 1, 0}, Case{EventMapKind::BloodCastle, 1, 2},
                              Case{EventMapKind::BloodCastle, 1, 10}, Case{EventMapKind::BloodCastle, 1, 18},
                              Case{EventMapKind::ChaosCastle, 0, 18}, Case{EventMapKind::ChaosCastle, 6, 0},
                              Case{EventMapKind::ChaosCastle, 6, 2}, Case{EventMapKind::ChaosCastle, 6, 17},
                              Case{EventMapKind::ChaosCastle, 6, 24}})
    {
        SCOPED_TRACE(static_cast<int>(probe.kind));
        SCOPED_TRACE(probe.map);
        world->Runtime().map = probe.map;
        sprites.SetColor(Color{0.25f, 0.5f, 0.75f, 1.0f});
        EventMapObject event = Castle(probe.kind, probe.state, 100, 10, 41);
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, true);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(text.texts.empty());
        ExpectColor(ColorNow(sprites), Color{0.25f, 0.5f, 0.75f, 1.0f});
        EXPECT_EQ(world->Runtime().hud.textColors.text, WhiteText);
        EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
    }
}

// In its map, the blend state, the white and the colors (0xff0096ff over 0) come before looking at the state: with a
// state that is not a play state, or without time, they stay set and it does not draw.
TEST_F(EventDrawingTest, TheCastleColorsAreSetBeforeTheStateIsRead)
{
    struct Case
    {
        EventMapKind kind;
        std::uint8_t state;
        std::int32_t remaining;
        std::int32_t map;
    };
    for (const Case& probe : {Case{EventMapKind::BloodCastle, 3, 100, 11}, Case{EventMapKind::BloodCastle, 4, 100, 17},
                              Case{EventMapKind::BloodCastle, 6, 100, 11}, Case{EventMapKind::BloodCastle, 1, 0, 11},
                              Case{EventMapKind::BloodCastle, 1, -1, 11}, Case{EventMapKind::ChaosCastle, 5, 100, 18},
                              Case{EventMapKind::ChaosCastle, 9, 100, 23}, Case{EventMapKind::ChaosCastle, 7, 0, 18}})
    {
        SCOPED_TRACE(static_cast<int>(probe.state));
        Restart();
        world->Runtime().map = probe.map;
        world->Runtime().hud.textColors = {.text = WhiteText, .background = 0x80000000u};
        sprites.SetColor(Color{0.25f, 0.5f, 0.75f, 1.0f});
        sprites.SetBlend(BlendMode::Opaque);
        EventMapObject event = Castle(probe.kind, probe.state, probe.remaining, 10, 41);
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, true);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(text.texts.empty());
        EXPECT_EQ(sprites.Blend(), BlendMode::Alpha);
        ExpectColor(ColorNow(sprites), White);
        EXPECT_EQ(world->Runtime().hud.textColors.text, 0xff0096ffu);
        EXPECT_EQ(world->Runtime().hud.textColors.background, 0u);
        EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
    }
}

// Blood Castle with +0x10 at 1, 2 or 5 and time: the points (864, or 866 with 5) centered at (570, 345) in font
// 0x5763558, 865 at (570, 357) and the time of 0x40bbf0 at (570, 367) in the big one. With 125 s, 2 minutes are left:
// " 02 : 05" in 0xff2020ff and the hundredths of the world clock (1234 ms → 34).
TEST_F(EventDrawingTest, BloodCastleDrawsThePointsTheTitleAndTheTime)
{
    worldClock = 1234.0f;
    for (const std::int32_t map : {11, 14, 17})
    {
        for (const std::uint8_t state : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{5}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(state));
            Restart();
            world->Runtime().map = map;
            EventMapObject event = Castle(EventMapKind::BloodCastle, state, 125, 40, 12);
            GameHudContext context = Context();
            const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, true);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

            ASSERT_EQ(text.texts.size(), 3u);
            const std::string points = state == 5 ? "pelota ( 12/40 )" : "bichos ( 12/40 )";
            EXPECT_EQ(text.texts[0].text, points);
            EXPECT_EQ(text.texts[0].x, Centered(570, points));
            EXPECT_EQ(text.texts[0].y, 345);
            EXPECT_EQ(text.texts[0].style.font, FontKind::Bold);
            EXPECT_EQ(text.texts[0].style.colors.text, 0xff0096ffu);
            EXPECT_EQ(text.texts[1].text, "queda");
            EXPECT_EQ(text.texts[1].x, Centered(570, "queda"));
            EXPECT_EQ(text.texts[1].y, 357);
            EXPECT_EQ(text.texts[1].style.font, FontKind::Bold);
            EXPECT_EQ(text.texts[1].style.colors.text, 0xff0096ffu);
            EXPECT_EQ(text.texts[2].text, " 02 : 05: 34");
            EXPECT_EQ(text.texts[2].x, Centered(570, " 02 : 05: 34"));
            EXPECT_EQ(text.texts[2].y, 367);
            EXPECT_EQ(text.texts[2].style.font, FontKind::BigBold);
            EXPECT_EQ(text.texts[2].style.colors.text, 0xff2020ffu);
            for (const Color& color : MarkerColors(sprites))
                ExpectColor(color, White);
            EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
        }
    }
}

// Without a target (+0x1c at 0xffff), no points: 865 at 345 and the time at 355. From 5 to 14 minutes, no red and with
// the hundredths; from 15 on, without them.
TEST_F(EventDrawingTest, TheCastleTimeWithoutATargetAndItsMinutes)
{
    worldClock = 59999.0f;
    struct Case
    {
        std::int32_t remaining;
        std::string_view line;
        std::uint32_t color;
    };
    for (const Case& probe : {Case{299, " 04 : 59: 59", 0xff2020ffu}, Case{300, " 05 : 00: 59", 0xff0096ffu},
                              Case{899, " 14 : 59: 59", 0xff0096ffu}, Case{900, " 15 : 00", 0xff0096ffu},
                              Case{3725, " 62 : 05", 0xff0096ffu}})
    {
        SCOPED_TRACE(probe.remaining);
        Restart();
        world->Runtime().map = 13;
        EventMapObject event = Castle(EventMapKind::BloodCastle, 1, probe.remaining, 0xffff, 3);
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, true);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(text.texts.size(), 2u);
        EXPECT_EQ(text.texts[0].text, "queda");
        EXPECT_EQ(text.texts[0].y, 345);
        EXPECT_EQ(text.texts[1].text, probe.line);
        EXPECT_EQ(text.texts[1].y, 355);
        EXPECT_EQ(text.texts[1].style.colors.text, probe.color);
    }
}

// Chaos Castle, from 6 to 8 in its maps: 1161 with the dead and the target, and then the trap area (0x6c634c) by
// +0x20: (40, 46] → 0, (30, 36] → 3, (20, 26] → 6; outside those ranges it stays as it was.
TEST_F(EventDrawingTest, ChaosCastleDrawsItsPointsAndPicksTheTrapArea)
{
    struct Case
    {
        std::int32_t kills;
        std::uint8_t area;
    };
    for (const std::int32_t map : {18, 20, 23})
    {
        for (const Case& probe : {Case{47, 0xff}, Case{46, 0}, Case{41, 0}, Case{40, 0xff}, Case{36, 3}, Case{31, 3},
                                  Case{30, 0xff}, Case{26, 6}, Case{21, 6}, Case{20, 0xff}, Case{0, 0xff}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(probe.kills);
            Restart();
            world->Runtime().map = map;
            world->Runtime().chaosCastleArea = 0xff;
            EventMapObject event = Castle(EventMapKind::ChaosCastle, 7, 1000, 70, probe.kills);
            GameHudContext context = Context();
            const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, true);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            ASSERT_EQ(text.texts.size(), 3u);
            EXPECT_EQ(text.texts[0].text, "rivales ( " + std::to_string(probe.kills) + "/70 )");
            EXPECT_EQ(text.texts[0].y, 345);
            EXPECT_EQ(text.texts[1].text, "queda");
            EXPECT_EQ(text.texts[2].text, " 16 : 40");
            EXPECT_EQ(text.texts[2].y, 367);
            EXPECT_EQ(world->Runtime().chaosCastleArea, probe.area);
        }
    }
}

TEST_F(EventDrawingTest, AnIntermediateChaosCastleDrawDoesNotWriteTheArea)
{
    world->Runtime().map = 18;
    EventMapObject event = Castle(EventMapKind::ChaosCastle, 6, 1000, 70, 45);
    GameHudContext context = Context(true);
    const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, false);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(text.texts.size(), 3u);
    EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
}

// 0x40bc96: the _ftol of the world clock outside MuRet's profile (an infinity) stops, under 15 minutes; a large one
// gives its low dword.
TEST_F(EventDrawingTest, ACastleClockOutsideTheFtolProfileCuts)
{
    worldClock = std::numeric_limits<float>::infinity();
    world->Runtime().map = 11;
    EventMapObject event = Castle(EventMapKind::BloodCastle, 1, 100, 0xffff, 0);
    GameHudContext context = Context();
    const auto drawn = Client::Events::DrawTheCastleTimeAndPoints(context, event, true);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x40bc96"), std::string::npos) << drawn.Error();
}

// ---------------------------------------------------------------------------------------------------------------
// 0x632a2b → method +0x10: the texts of the results boxes.

void PutRecord(EventMapObject& event, std::size_t record, std::string_view name, std::int32_t points,
               std::int32_t experience, std::int32_t zen)
{
    auto* bytes = reinterpret_cast<unsigned char*>(event.field2c.data());
    for (std::size_t at = 0; at < name.size(); ++at)
        bytes[record * 24 + at] = static_cast<unsigned char>(name[at]);
    event.field2c[(record * 24 + 0xc) / 4] = points;
    event.field2c[(record * 24 + 0x10) / 4] = experience;
    event.field2c[(record * 24 + 0x14) / 4] = zen;
}

void NameTheHero(WorldState& world, std::string_view name)
{
    Client::Kernel::ActorState& hero = world.Hero();
    hero.name = {};
    for (std::size_t at = 0; at < name.size(); ++at)
        hero.name[at] = static_cast<std::byte>(name[at]);
}

// Devil Square (0x40bdd0): 647 and 648 with the hero's name, titles 680, 682 (ten further right), 683 and 684 (without
// 681), and the rows: record (row + 1) % count, and in the last the hero's own (record 0) under 685, with the rank of
// +0x28. The names go up to 10 bytes.
TEST_F(EventDrawingTest, TheDevilSquareRankingDrawsTheTitlesAndTheRows)
{
    NameTheHero(*world, "Heroe");
    EventMapObject event;
    event.kind = EventMapKind::DevilSquare;
    event.field24 = 3;
    event.field28 = 17;
    PutRecord(event, 0, "Heroe", 500, 60, 7);
    PutRecord(event, 1, "Primerito", 900, 80, 9);
    PutRecord(event, 2, "Segundon01", 700, 70, 8);
    GameHudContext context = Context();
    const auto drawn = Client::Events::DrawTheEventBoxText(context, event);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    struct Expected
    {
        int x;
        int y;
        std::string_view text;
        std::uint32_t color;
    };
    // The title text colors are the ones there were (white); the rows use their own.
    const std::vector<Expected> expected{
        {0xe5, 0x46, "bien hecho", WhiteText},
        {0xe5, 0x56, "Heroe aguanto la prueba", WhiteText},
        {0xdb, 0x6e, "puesto", WhiteText},
        {0x11f + 10, 0x6e, "puntos", WhiteText},
        {0x159, 0x6e, "exp", WhiteText},
        {0x17f, 0x6e, "premio", WhiteText},
        {0xdb, 0x7e, " 1", WhiteText},
        {0xeb, 0x7e, "Primerito", WhiteText},
        {0x11f, 0x7e, "       900", 0xffffd2d2u},
        {0x159, 0x7e, "    80", 0xffd2ffd2u},
        {0x17f, 0x7e, "     9", 0xffd2d2ffu},
        {0xdb, 0x8e, " 2", WhiteText},
        {0xeb, 0x8e, "Segundon01", WhiteText},
        {0x11f, 0x8e, "       700", 0xffffd2d2u},
        {0x159, 0x8e, "    70", 0xffd2ffd2u},
        {0x17f, 0x8e, "     8", 0xffd2d2ffu},
        {0xdb, 0x11e, "lo mio", 0xffd2d2ffu},
        {0xdb, 0x12e, "17", WhiteText},
        {0xeb, 0x12e, "Heroe", WhiteText},
        {0x11f, 0x12e, "       500", 0xffffd2d2u},
        {0x159, 0x12e, "    60", 0xffd2ffd2u},
        {0x17f, 0x12e, "     7", 0xffd2d2ffu},
    };
    ASSERT_EQ(text.texts.size(), expected.size());
    for (std::size_t at = 0; at < expected.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(text.texts[at].x, expected[at].x);
        EXPECT_EQ(text.texts[at].y, expected[at].y);
        EXPECT_EQ(text.texts[at].text, expected[at].text);
        EXPECT_EQ(text.texts[at].style.colors.text, expected[at].color);
    }
    // glColor3f: (0.5, 1, 0.5) the titles, (0.5, 0.5, 1) the columns, white the rows and (0.9, 0.9, 0) the hero's own.
    const std::vector<Color> colors = MarkerColors(sprites);
    ASSERT_EQ(colors.size(), expected.size());
    const Color title{0.5f, 1.0f, 0.5f, 1.0f};
    const Color header{0.5f, 0.5f, 1.0f, 1.0f};
    const Color own{0.9f, 0.9f, 0.0f, 1.0f};
    for (std::size_t at = 0; at < colors.size(); ++at)
    {
        SCOPED_TRACE(at);
        ExpectColor(colors[at], at < 2 ? title : at < 6 ? header : at < 16 ? White : own);
    }
}

// With more than 11 records, (row + 1) % count reads outside the 0x108 bytes of +0x2c: it stops.
TEST_F(EventDrawingTest, ADevilSquareRankingPastItsRecordsCuts)
{
    EventMapObject event;
    event.kind = EventMapKind::DevilSquare;
    event.field24 = 12;
    GameHudContext context = Context();
    const auto drawn = Client::Events::DrawTheEventBoxText(context, event);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x40bf1b"), std::string::npos) << drawn.Error();
}

// Blood Castle (0x40c440): 857 and 858 if it won (+0x24), 859 and 860 if not, centered at 0x140; then, in font
// 0x5763558, the experience (+0x3c), the zen (+0x40) only if it won and the points (+0x38).
TEST_F(EventDrawingTest, TheBloodCastleResultDrawsWonAndLost)
{
    for (const bool won : {true, false})
    {
        SCOPED_TRACE(won);
        Restart();
        EventMapObject event;
        event.kind = EventMapKind::BloodCastle;
        event.field24 = won ? 1 : 0;
        event.field2c[(0x38 - 0x2c) / 4] = 321;
        event.field2c[(0x3c - 0x2c) / 4] = 4500;
        event.field2c[(0x40 - 0x2c) / 4] = 20000;
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheEventBoxText(context, event);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        struct Expected
        {
            int y;
            std::string_view text;
            FontKind font;
            std::uint32_t color;
        };
        std::vector<Expected> expected{
            {0x46, won ? "ganaste" : "perdiste", FontKind::Normal, WhiteText},
            {0x56, won ? "mision cumplida" : "mision sin cumplir", FontKind::Normal, WhiteText},
            {0x6a, "exp 4500", FontKind::Bold, 0xffd2ffd2u}};
        if (won)
            expected.push_back({0x82, "zen 20000", FontKind::Bold, 0xffd2d2ffu});
        expected.push_back({won ? 0x9a : 0x82, "puntos 321", FontKind::Bold, 0xffffd2d2u});
        ASSERT_EQ(text.texts.size(), expected.size());
        const std::vector<Color> colors = MarkerColors(sprites);
        ASSERT_EQ(colors.size(), expected.size());
        for (std::size_t at = 0; at < expected.size(); ++at)
        {
            SCOPED_TRACE(at);
            EXPECT_EQ(text.texts[at].x, Centered(0x140, expected[at].text));
            EXPECT_EQ(text.texts[at].y, expected[at].y);
            EXPECT_EQ(text.texts[at].text, expected[at].text);
            EXPECT_EQ(text.texts[at].style.font, expected[at].font);
            EXPECT_EQ(text.texts[at].style.colors.text, expected[at].color);
            ExpectColor(colors[at], at < 2 ? Color{0.5f, 1.0f, 0.5f, 1.0f} : White);
        }
    }
}

// Chaos Castle (0x405220): 1151 and "%s %s" with 1152 and 858 if it won; "%s %s" with 1152 and 860 and 1153 if not;
// then the experience (+0x3c), the monsters (+0x38) and the players (+0x40).
TEST_F(EventDrawingTest, TheChaosCastleResultDrawsWonAndLost)
{
    for (const bool won : {true, false})
    {
        SCOPED_TRACE(won);
        Restart();
        EventMapObject event;
        event.kind = EventMapKind::ChaosCastle;
        event.field24 = won ? 1 : 0;
        event.field2c[(0x38 - 0x2c) / 4] = 33;
        event.field2c[(0x3c - 0x2c) / 4] = 9000;
        event.field2c[(0x40 - 0x2c) / 4] = 4;
        GameHudContext context = Context();
        const auto drawn = Client::Events::DrawTheEventBoxText(context, event);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        const std::vector<std::string_view> lines =
            won ? std::vector<std::string_view>{"el guardia descansa", "la mision mision cumplida", "exp 9000",
                                                "monstruos 33", "jugadores 4"}
                : std::vector<std::string_view>{"la mision mision sin cumplir", "otra vez sera", "exp 9000",
                                                "monstruos 33", "jugadores 4"};
        const std::array<int, 5> rows{0x50, 0x60, 0x7e, 0x92, 0xa6};
        ASSERT_EQ(text.texts.size(), lines.size());
        for (std::size_t at = 0; at < lines.size(); ++at)
        {
            SCOPED_TRACE(at);
            EXPECT_EQ(text.texts[at].text, lines[at]);
            EXPECT_EQ(text.texts[at].x, Centered(0x140, lines[at]));
            EXPECT_EQ(text.texts[at].y, rows[at]);
            EXPECT_EQ(text.texts[at].style.font, at < 2 ? FontKind::Normal : FontKind::Bold);
            EXPECT_EQ(text.texts[at].style.colors.text, at < 2 ? WhiteText : 0xffd2ffd2u);
        }
    }
}
} // namespace
