#include "Events/Crywolf/CrywolfInterfaceDrawing.h"

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
#include "Hud/GameHud.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// CL9 milestone 11, phase 3 (06 § La interfaz de Crywolf): 0x4365c0 and 0x430180 against the ASM of c32bbd5f, with no
// GL trace (*static*; what the VM has to confirm is in 08, P-C298). GS 0.97 has no Crywolf. Text.bmd is made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::CrywolfInterface;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Text::FontKind;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::int32_t CrywolfMap = 34;
constexpr std::uint16_t DigitsSlot = 1;

std::map<std::size_t, std::string> CrywolfTexts()
{
    std::map<std::size_t, std::string> texts{
        {20, "mago"},       {21, "guerrero"},    {22, "arquera"},   {23, "gladiador"}, {24, "senor"},
        {25, "maestro"},    {26, "espadachin"},  {27, "musa"},      {680, "puesto"},   {681, "quien"},
        {1948, "lobos %d"}, {1949, "balgass"},   {1973, "clase"},   {1977, "puntos"},  {2000, "se defendio"},
        {2001, "el altar"}, {2009, "se perdio"}, {2010, "la aldea"}};
    for (std::size_t tip = 1953; tip <= 1972; ++tip)
        texts.emplace(tip, "consejo " + std::to_string(tip));
    return texts;
}

struct DrawnQuad
{
    std::optional<std::uint16_t> slot;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// The batch's quads in order, without the text marks.
std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(
                DrawnQuad{.slot = command.texture ? std::optional<std::uint16_t>{command.texture->value} : std::nullopt,
                          .blend = command.blend,
                          .color = vertices[first].color,
                          .first = vertices[first],
                          .third = vertices[first + 2]});
        }
    }
    return quads;
}

std::vector<std::uint16_t> SlotsOf(const std::vector<DrawnQuad>& quads)
{
    std::vector<std::uint16_t> slots;
    for (const DrawnQuad& quad : quads)
        slots.push_back(quad.slot.value_or(0));
    return slots;
}

// The top left corner and the size of a quad in 640 × 480, with y going down.
void ExpectQuad(const DrawnQuad& quad, std::uint16_t slot, float x, float y, float width, float height)
{
    EXPECT_EQ(quad.slot, slot);
    EXPECT_FLOAT_EQ(quad.first.x, x);
    EXPECT_FLOAT_EQ(quad.first.y, 480.0f - y);
    // The corners are exact; the subtraction of two floats near 300 loses the last bits.
    EXPECT_NEAR(quad.third.x - quad.first.x, width, 1e-4f);
    EXPECT_NEAR(quad.first.y - quad.third.y, height, 1e-4f);
}

class CrywolfInterfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sprites.Begin(Screen640);
    }

    GameHudContext Context(CrywolfInterface* copy = nullptr)
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
                              .crywolfInterface = copy,
                              .sound = sound,
                              .sprites = sprites,
                              .text = text,
                              .texts = &texts,
                              .tickCount = tickCount,
                              .mouse = mouse};
    }

    // A frame: the batch empty, the texts cleared and 0x4365c0.
    void Frame(CrywolfInterface* copy = nullptr)
    {
        text.texts.clear();
        sprites.Begin(Screen640);
        GameHudContext context = Context(copy);
        const auto drawn = Client::Events::Crywolf::DrawTheCrywolfInterface(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }

    // The blend left over: that of an untextured rectangle drawn now.
    BlendMode BlendNow()
    {
        sprites.ColorQuad(0.0f, 0.0f, 1.0f, 1.0f);
        return sprites.Commands().back().blend;
    }

    static int Centered(int x, std::string_view line)
    {
        return Client::Render::Text::CenteredAt(
            x, {static_cast<int>(line.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height},
            Screen640);
    }

    CrywolfInterface& Ui()
    {
        return world->Runtime().crywolf.ui;
    }

    void Enter(std::int32_t map, std::uint8_t stage)
    {
        world->Runtime() = Client::Kernel::WorldRuntime{};
        world->Runtime().map = map;
        world->Runtime().crywolf.stage = stage;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x2468ace0u};
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    RecordingTextDrawer text{sprites};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(CrywolfTexts());
    std::uint32_t tickCount = 50000;
    Client::Ui::Point mouse{};
};

// Without results, without a notice and outside the defense in 34: only 0x620a00(1), which stays (it does not reach
// 0x620980). Stage 3 or 4 in Lorencia or Devias does not draw the state either (0x4303c0).
TEST_F(CrywolfInterfaceTest, OutsideItsStatesItOnlyLeavesTheNormalBlend)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        for (const std::uint8_t stage : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{5}})
        {
            if (map == CrywolfMap && (stage == 3 || stage == 4))
                continue;
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(stage));
            Enter(map, stage);
            sprites.SetBlend(BlendMode::Opaque);
            Frame();
            EXPECT_TRUE(QuadsOf(sprites).empty());
            EXPECT_TRUE(text.texts.empty());
            EXPECT_EQ(BlendNow(), BlendMode::Alpha);
            EXPECT_EQ(Ui().byte723bf8, 0);
        }
    }
}

// Stage 4 in 34: the frame, the decoration at 98, the five .data altars (2: 0x6be), 0x6bc without 0x723bd8, the bottom
// bar, 1948 at (57, 85) in 0xff1594ff with glColor (1, 0.6, 0.3), the clock in white and the light; it ends with no
// blend. No tips (they belong to stage 3).
TEST_F(CrywolfInterfaceTest, TheWarningStageDrawsTheStateInCrywolf)
{
    Enter(CrywolfMap, 4);
    Ui().light = 100;
    Frame();
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 11u);
    ExpectQuad(quads[0], 0x6b9, 0.0f, 1.0f, 122.0f, 98.0f);
    ExpectQuad(quads[1], 0x6b8, 4.0f, 98.0f, 20.0f, 9.0f);
    const std::array<std::array<float, 2>, 5> altars{{{4, 29}, {16, 17}, {30, 9}, {46, 5}, {62, 4}}};
    for (std::size_t altar = 0; altar < altars.size(); ++altar)
        ExpectQuad(quads[2 + altar], 0x6be, altars[altar][0], altars[altar][1], 13.0f, 13.0f);
    ExpectQuad(quads[7], 0x6bc, 4.0f, 81.0f, 15.0f, 15.0f);
    ExpectQuad(quads[8], 0x6c2, 240.0f, 396.0f, 156.0f, 36.0f);
    // The minutes and the seconds at 0: one digit each, in white.
    EXPECT_EQ(quads[9].slot, DigitsSlot);
    EXPECT_EQ(quads[10].slot, DigitsSlot);
    EXPECT_FLOAT_EQ(quads[9].color.green, 1.0f);
    ASSERT_EQ(quads.size(), 12u);
    ExpectQuad(quads[11], 0x6bf, 4.0f, 46.0f, static_cast<float>(100.0 * static_cast<double>(0.89f)), 30.0f);
    EXPECT_FLOAT_EQ(quads[11].third.u - quads[11].first.u,
                    static_cast<float>(100.0 * static_cast<double>(0.88f) * 0.0078125));
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "lobos 0");
    EXPECT_EQ(text.texts[0].x, Centered(0x39, "lobos 0"));
    EXPECT_EQ(text.texts[0].y, 0x55);
    EXPECT_EQ(text.texts[0].style.font, FontKind::Normal);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xff1594ffu);
    EXPECT_EQ(BlendNow(), BlendMode::Opaque);
    EXPECT_EQ(Ui().byte723bf8, 1);
}

// The altars by their byte: 0x1? in the high nibble gives 0x6cd, 0x6ce or 0x6cf by the low one; otherwise 0x6bd with
// 1, 0x6be with 2 and nothing with another. With 0x723bd8, icon 0x6bb.
TEST_F(CrywolfInterfaceTest, TheAltarsPickTheirSlotByTheirByte)
{
    Enter(CrywolfMap, 3);
    Ui().altars = {0x11, 0x12, 0x15, 0x01, 0x23};
    Ui().dword723bd8 = 2;
    Frame();
    const std::vector<std::uint16_t> slots = SlotsOf(QuadsOf(sprites));
    ASSERT_GE(slots.size(), 8u);
    EXPECT_EQ(std::vector<std::uint16_t>(slots.begin() + 2, slots.begin() + 7),
              (std::vector<std::uint16_t>{0x6cd, 0x6ce, 0x6cf, 0x6bd, 0x6bb}));
}

// The Balgass bar with 0x723a3d at 1: the decoration drops by 1 (98 + 0x723a40) down to 21; once down, background
// 0x6b6, icon 0x6ba, 1949 at (38, 101) and bar 0x6b7 of 0x6c73d4 · 0.68f. With 0x723a3d at 0 it comes back by 1. The
// clock goes in (1, 0.3, 0.3) with the bar.
TEST_F(CrywolfInterfaceTest, TheBossBarSlidesDownAndBack)
{
    Enter(CrywolfMap, 4);
    Ui().byte723a3d = 1;
    Ui().dword6c73d4 = 80;
    for (int frame = 0; frame < 21; ++frame)
    {
        Frame();
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        EXPECT_FLOAT_EQ(quads[1].first.y, 480.0f - (98.0f + static_cast<float>(frame)));
        EXPECT_FLOAT_EQ(Ui().float723a40, static_cast<float>(frame + 1));
    }
    Frame();
    std::vector<DrawnQuad> quads = QuadsOf(sprites);
    EXPECT_FLOAT_EQ(Ui().float723a40, 21.0f);
    ASSERT_GE(quads.size(), 12u);
    ExpectQuad(quads[9], 0x6b6, 0.0f, 98.0f, 97.0f, 21.0f);
    ExpectQuad(quads[10], 0x6ba, 4.0f, 101.0f, 15.0f, 15.0f);
    ExpectQuad(quads[11], 0x6b7, 25.0f, 109.0f, static_cast<float>(80.0 * static_cast<double>(0.68f)), 8.0f);
    EXPECT_FLOAT_EQ(quads[11].third.u - quads[11].first.u,
                    static_cast<float>(80.0 * static_cast<double>(0.67f) * 0.0078125));
    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[1].text, "balgass");
    EXPECT_EQ(text.texts[1].x, Centered(0x26, "balgass"));
    EXPECT_EQ(text.texts[1].y, 0x65);
    EXPECT_EQ(quads[12].slot, DigitsSlot);
    EXPECT_FLOAT_EQ(quads[12].color.green, 0.3f);
    Ui().byte723a3d = 0;
    Frame();
    EXPECT_FLOAT_EQ(Ui().float723a40, 20.0f);
}

// The clock from BD 04's GetTickCount: the minutes with the unsigned division by 60000 and the seconds gone, one per
// new second of _ftol(ms · 1.6666667e-5f · 60f), from 0 to 59; a new minute resets them to 0. With the minutes past,
// + 60.
TEST_F(CrywolfInterfaceTest, TheClockCountsTheMinutesLeftAndTheSecondsGone)
{
    Enter(CrywolfMap, 4);
    Ui().dwords723bdc[3] = 10;
    Ui().dword7239f4 = 1000;
    tickCount = 1000 + 61500;
    Frame();
    EXPECT_EQ(Ui().dword723c00, 61);
    EXPECT_EQ(Ui().dword723bf4, 9);
    EXPECT_EQ(Ui().dword723bfc, 0);
    tickCount += 1000;
    Frame();
    EXPECT_EQ(Ui().dword723c00, 62);
    EXPECT_EQ(Ui().dword723bfc, 1);
    // 60 − 1 = 59: two digits after the 9's.
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    EXPECT_EQ(quads[9].slot, DigitsSlot);
    EXPECT_EQ(quads[10].slot, DigitsSlot);
    EXPECT_EQ(quads[11].slot, DigitsSlot);
    EXPECT_EQ(quads[12].slot, 0x6bf);
    tickCount += 100;
    Frame();
    EXPECT_EQ(Ui().dword723bfc, 1);
    // 11 minutos pasados de 10: −1 + 60.
    tickCount = 1000 + 11 * 60000;
    Frame();
    EXPECT_EQ(Ui().dword723bf4, 59);
    EXPECT_EQ(Ui().dword723bfc, 0);
    Ui().dwords723bdc[3] = 0;
    Frame();
    EXPECT_EQ(Ui().dword723bf4, 0);
}

// 0x430180 with stage 3: font 0x5763558, the four lines of 1957 + 4 · tip at (0xbe, 0x3f + 13 · i), the first in
// 0xffffc864 and the others in 0xffff9664 over 0xaa000000. 0x63b0e3(10000) fires on the first call (the clock at 0)
// and fires again 10000 ms after the following call; the tip goes from −1 to 0 and wraps around at 4.
TEST_F(CrywolfInterfaceTest, TheDefenseStageRotatesTheTips)
{
    Enter(CrywolfMap, 3);
    tickCount = 20000;
    Frame();
    EXPECT_EQ(Ui().dwords6c73e0[0], 0);
    ASSERT_EQ(text.texts.size(), 5u);
    for (std::size_t row = 0; row < 4; ++row)
    {
        const RecordedText& line = text.texts[1 + row];
        EXPECT_EQ(line.text, "consejo " + std::to_string(1957 + row));
        EXPECT_EQ(line.x, 0xbe);
        EXPECT_EQ(line.y, 0x3f + 13 * static_cast<int>(row));
        EXPECT_EQ(line.style.font, FontKind::Bold);
        EXPECT_EQ(line.style.colors.text, row == 0 ? 0xffffc864u : 0xffff9664u);
        EXPECT_EQ(line.style.colors.background, 0xaa000000u);
    }
    tickCount = 25000;
    Frame();
    EXPECT_EQ(Ui().tipTimerStart, 25000u);
    EXPECT_EQ(Ui().dwords6c73e0[0], 0);
    tickCount = 35000;
    Frame();
    EXPECT_EQ(Ui().dwords6c73e0[0], 1);
    EXPECT_EQ(text.texts[1].text, "consejo 1961");
    for (const std::uint32_t at : {35001u, 45001u, 45002u, 55002u, 55003u, 65003u})
    {
        tickCount = at;
        Frame();
    }
    EXPECT_EQ(Ui().dwords6c73e0[0], 0);
}

// The notice at 1: the count rises every frame and banner 0x6c0 enters from 15 · count − 329 with the wave's alpha
// ((2 · count) % 140); the rank (0x6d1) arrives in steps of 15 from 0x186 + 250 and, at 0, letter 0x6d2 + 0x723bed,
// the label and the nine digits of the experience (0x6d7 + digit, from 409 in steps of 15 to the left).
TEST_F(CrywolfInterfaceTest, TheNoticeBannerEntersAndTheRankArrives)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        Enter(map, 5);
        Ui().message = 1;
        Ui().byte723bed = 2;
        Ui().dword723bf0 = 12345;
        Frame();
        std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 3u);
        ExpectQuad(quads[0], 0x6c0, 30.0f - 329.0f, 50.0f, 329.0f, 94.0f);
        EXPECT_FLOAT_EQ(quads[0].color.alpha,
                        static_cast<float>(4.0 * static_cast<double>(0.01f) + static_cast<double>(0.3f)));
        ExpectQuad(quads[1], 0x6d0, 230.0f, 150.0f, 196.0f, 141.0f);
        ExpectQuad(quads[2], 0x6d1, static_cast<float>(0x186 - 15 + 250), 188.0f, 110.0f, 28.0f);
        EXPECT_EQ(Ui().dword6c73d8, 2);
        // Up to count 40: 2 · 40 % 140 = 80, the alpha drops.
        for (int frame = 0; frame < 38; ++frame)
            Frame();
        quads = QuadsOf(sprites);
        EXPECT_EQ(Ui().dword6c73d8, 40);
        ExpectQuad(quads[0], 0x6c0, 150.0f, 50.0f, 329.0f, 94.0f);
        EXPECT_FLOAT_EQ(quads[0].color.alpha, static_cast<float>(1.0 - 10.0 * static_cast<double>(0.01f)));
        ASSERT_EQ(quads.size(), 3u + 1u + 1u + 9u);
        EXPECT_EQ(Ui().dword6c73f8, 0);
        ExpectQuad(quads[3], 0x6d4, 370.0f, 188.0f, 29.0f, 28.0f);
        ExpectQuad(quads[4], 0x6e1, 229.0f, 235.0f, 60.0f, 19.0f);
        const std::array<std::uint16_t, 9> digits{5, 4, 3, 2, 1, 0, 0, 0, 0};
        for (std::size_t digit = 0; digit < digits.size(); ++digit)
            ExpectQuad(quads[5 + digit], static_cast<std::uint16_t>(0x6d7 + digits[digit]),
                       409.0f - 15.0f * static_cast<float>(digit), 235.0f, 15.0f, 19.0f);
    }
}

// At 400 the notice goes to 0 and the count to 0: the banner leaves from 15 · count + 150 and, past 479, the notice
// stays at −1, the count at 0 and the rank goes back to 0x186.
TEST_F(CrywolfInterfaceTest, TheNoticeBannerLeavesAfterFourHundredFrames)
{
    Enter(CrywolfMap, 5);
    Ui().message = 1;
    Ui().dword6c73d8 = 399;
    Frame();
    EXPECT_EQ(Ui().message, 0);
    EXPECT_EQ(Ui().dword6c73d8, 1);
    ExpectQuad(QuadsOf(sprites)[0], 0x6c0, 150.0f, 50.0f, 329.0f, 94.0f);
    Frame();
    ExpectQuad(QuadsOf(sprites)[0], 0x6c0, 165.0f, 50.0f, 329.0f, 94.0f);
    Ui().dword6c73d8 = 31;
    Frame();
    EXPECT_EQ(Ui().message, -1);
    EXPECT_EQ(Ui().dword6c73d8, 0);
    EXPECT_EQ(Ui().dword6c73f8, 0x186);
    Frame();
    EXPECT_TRUE(QuadsOf(sprites).empty());
}

// The results with 0x723bf9: frame 0x6e2, the titles in 0xff1594ff centered at y 0xa0, the rows in white from 0xaf in
// steps of 15 (the −1 one is not drawn but counts), the class by its bits (with 5 or more, the name again) and the
// notice text in 0xffff00ff; button 0x6c9 without the mouse.
TEST_F(CrywolfInterfaceTest, TheResultsListTheFivePlaces)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        Enter(map, 0);
        CrywolfInterface& ui = Ui();
        ui.byte723bf9 = 1;
        ui.dwords6c73e0 = {-1, 500, -1, 300, 200, 100};
        const std::array<std::string, 5> names{"LoboGris01", "", "Aullido", "Luna", "Colmillo"};
        for (std::size_t place = 0; place < names.size(); ++place)
            std::copy(names[place].begin(), names[place].end(), ui.rankingNames[place].begin());
        ui.rankingClasses = {0x08, 0, 3, 0x0b, 5};
        Frame();
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 2u);
        ExpectQuad(quads[0], 0x6e2, 200.0f, 110.0f, 252.0f, 240.0f);
        ExpectQuad(quads[1], 0x6c9, 300.0f, 300.0f, 54.0f, 30.0f);
        struct Line
        {
            int x;
            int y;
            std::string text;
            std::uint32_t color;
        };
        const std::vector<Line> expected{
            {0xf0, 0xa0, "puesto", 0xff1594ffu},     {0x11d, 0xa0, "quien", 0xff1594ffu},
            {0x14d, 0xa0, "clase", 0xff1594ffu},     {0x183, 0xa0, "puntos", 0xff1594ffu},
            {0xf0, 0xaf, "1", 0xffffffffu},          {0x11d, 0xaf, "LoboGris01", 0xffffffffu},
            {0x14f, 0xaf, "maestro", 0xffffffffu},   {0x181, 0xaf, "500", 0xffffffffu},
            {0xf0, 0xcd, "3", 0xffffffffu},          {0x11d, 0xcd, "Aullido", 0xffffffffu},
            {0x14f, 0xcd, "gladiador", 0xffffffffu}, {0x181, 0xcd, "300", 0xffffffffu},
            {0xf0, 0xdc, "4", 0xffffffffu},          {0x11d, 0xdc, "Luna", 0xffffffffu},
            {0x14f, 0xdc, "gladiador", 0xffffffffu}, {0x181, 0xdc, "200", 0xffffffffu},
            {0xf0, 0xeb, "5", 0xffffffffu},          {0x11d, 0xeb, "Colmillo", 0xffffffffu},
            {0x14f, 0xeb, "Colmillo", 0xffffffffu},  {0x181, 0xeb, "100", 0xffffffffu},
            {0x14a, 260, "se perdio", 0xffff00ffu},  {0x148, 277, "la aldea", 0xffff00ffu}};
        ASSERT_EQ(text.texts.size(), expected.size());
        for (std::size_t line = 0; line < expected.size(); ++line)
        {
            SCOPED_TRACE(line);
            EXPECT_EQ(text.texts[line].text, expected[line].text);
            EXPECT_EQ(text.texts[line].x, Centered(expected[line].x, expected[line].text));
            EXPECT_EQ(text.texts[line].y, expected[line].y);
            EXPECT_EQ(text.texts[line].style.font, FontKind::Normal);
            EXPECT_EQ(text.texts[line].style.colors.text, expected[line].color);
        }
        EXPECT_EQ(ui.byte723bf9, 1);
    }
}

// The button: with the mouse inside (300, 354) × (300, 330), 0x6ca; and with 0x723bdc at 5, 0x6cb and the window
// closes (0x723a44, 0x723bdc and 0x723bf9 at 0). On the edge it is not inside. With the notice at 1, texts 2000 and
// 2001.
TEST_F(CrywolfInterfaceTest, TheResultsButtonClosesWithTheUiMark)
{
    Enter(CrywolfMap, 0);
    CrywolfInterface& ui = Ui();
    ui.byte723bf9 = 1;
    ui.message = 1;
    mouse = {300, 315};
    Frame();
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6c9);
    EXPECT_EQ(text.texts.back().text, "el altar");
    mouse = {301, 329};
    Frame();
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6ca);
    EXPECT_EQ(ui.byte723bf9, 1);
    ui.dwords723bdc[0] = 5;
    ui.byte723a44 = 1;
    Frame();
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6cb);
    EXPECT_EQ(ui.byte723bf9, 0);
    EXPECT_EQ(ui.byte723a44, 0);
    EXPECT_EQ(ui.dwords723bdc[0], 0);
}

// An intermediate MuRet drawing writes into its copy: WorldRuntime's stays.
TEST_F(CrywolfInterfaceTest, AProjectionWritesItsCopy)
{
    Enter(CrywolfMap, 3);
    Ui().message = 1;
    Ui().byte723a3d = 1;
    CrywolfInterface copy = Ui();
    Frame(&copy);
    EXPECT_EQ(copy.dword6c73d8, 2);
    EXPECT_FLOAT_EQ(copy.float723a40, 1.0f);
    EXPECT_EQ(copy.byte723bf8, 1);
    EXPECT_EQ(Ui().dword6c73d8, 1);
    EXPECT_FLOAT_EQ(Ui().float723a40, 0.0f);
    EXPECT_EQ(Ui().byte723bf8, 0);
}

// 0x436240 (HUD child 26): with stage 3 or 4 in 34 it leaves 0xff1594ff over 0 for what follows, without drawing; in
// Lorencia, in Devias or with another stage it does not touch the color or draw the contract window.
TEST_F(CrywolfInterfaceTest, TheContractStepOnlyLeavesTheTitleColorInTheDefense)
{
    const Client::Render::Text::TextColors before{.text = 0xff102030u, .background = 0xff405060u};
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        for (const std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(stage));
            Enter(map, stage);
            world->Runtime().hud.textColors = before;
            sprites.Begin(Screen640);
            GameHudContext context = Context();
            ASSERT_TRUE(Client::Events::Crywolf::DrawTheCrywolfContract(context).HasValue());
            EXPECT_TRUE(QuadsOf(sprites).empty());
            const bool defense = map == CrywolfMap && stage != 2;
            EXPECT_EQ(world->Runtime().hud.textColors.text, defense ? 0xff1594ffu : before.text);
            EXPECT_EQ(world->Runtime().hud.textColors.background, defense ? 0u : before.background);
        }
    }
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Enter(map, 3);
        Ui().byte723a44 = 1;
        sprites.Begin(Screen640);
        GameHudContext context = Context();
        ASSERT_TRUE(Client::Events::Crywolf::DrawTheCrywolfContract(context).HasValue());
        EXPECT_TRUE(QuadsOf(sprites).empty());
    }
}

// 0x436274..0x43643b: window 1, background 0x6cc, the No (0x6c3..0x6c5) and the Yes (0x6c9..0x6cb) with the mouse
// inside their box without the edges, and the lines centered at 0x13d: the first at 0xe3 with a second (and at 0xef
// without) and the second at 0xee. With the mouse over the pressed button (0x723bdc at 1 or 2) the window closes and
// the rest is drawn.
TEST_F(CrywolfInterfaceTest, TheQuestionDrawsBothButtonsAndCloses)
{
    const auto draw = [this]
    {
        text.texts.clear();
        sprites.Begin(Screen640);
        GameHudContext context = Context();
        const auto drawn = Client::Events::Crywolf::DrawTheCrywolfContract(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    };
    const auto put = [](std::array<char, 200>& line, std::string_view value)
    {
        std::copy(value.begin(), value.end(), line.begin());
        line[value.size()] = '\0';
    };
    Enter(CrywolfMap, 4);
    Ui().byte723a44 = 1;
    put(Ui().contractLines[0], "quedan 3");
    put(Ui().contractLines[1], "firmas?");
    mouse = {100, 100};
    draw();
    auto quads = QuadsOf(sprites);
    ASSERT_EQ(quads.size(), 3u);
    ExpectQuad(quads[0], 0x6cc, 212.0f, 206.0f, 209.0f, 80.0f);
    ExpectQuad(quads[1], 0x6c3, 330.0f, 250.0f, 54.0f, 30.0f);
    ExpectQuad(quads[2], 0x6c9, 250.0f, 250.0f, 54.0f, 30.0f);
    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, "quedan 3");
    EXPECT_EQ(text.texts[0].x, Centered(0x13d, "quedan 3"));
    EXPECT_EQ(text.texts[0].y, 0xe3);
    EXPECT_EQ(text.texts[1].text, "firmas?");
    EXPECT_EQ(text.texts[1].y, 0xee);
    EXPECT_EQ(world->Runtime().hud.textColors.text, 0xff1594ffu);

    mouse = {0x14b, 0xfb};
    draw();
    EXPECT_EQ(SlotsOf(QuadsOf(sprites)), (std::vector<std::uint16_t>{0x6cc, 0x6c4, 0x6c9}));
    mouse = {0x12f, 0x117};
    draw();
    EXPECT_EQ(SlotsOf(QuadsOf(sprites)), (std::vector<std::uint16_t>{0x6cc, 0x6c3, 0x6ca}));
    EXPECT_EQ(Ui().byte723a44, 1);

    Ui().dwords723bdc[0] = 2;
    draw();
    EXPECT_EQ(SlotsOf(QuadsOf(sprites)), (std::vector<std::uint16_t>{0x6cc, 0x6c3, 0x6cb}));
    EXPECT_EQ(Ui().byte723a44, 0);
    EXPECT_EQ(Ui().dwords723bdc[0], 0);
    EXPECT_EQ(text.texts.size(), 2u);

    Ui().byte723a44 = 1;
    Ui().dwords723bdc[0] = 1;
    mouse = {0x17f, 0x117};
    Ui().contractLines[1][0] = '\0';
    draw();
    EXPECT_EQ(SlotsOf(QuadsOf(sprites)), (std::vector<std::uint16_t>{0x6cc, 0x6c5, 0x6c9}));
    EXPECT_EQ(Ui().byte723a44, 0);
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].y, 0xef);
}

// 0x43643e..0x43656c: window 2, the background and the OK (0x6c6..0x6c8) at 290, which closes with 0x723bdc at 3. An
// intermediate drawing writes into its copy.
TEST_F(CrywolfInterfaceTest, TheNoticeDrawsTheOk)
{
    Enter(CrywolfMap, 3);
    Ui().byte723a44 = 2;
    Ui().contractLines[0][0] = 'a';
    mouse = {0x121, 0x100};
    text.texts.clear();
    sprites.Begin(Screen640);
    GameHudContext first = Context();
    ASSERT_TRUE(Client::Events::Crywolf::DrawTheCrywolfContract(first).HasValue());
    auto quads = QuadsOf(sprites);
    ASSERT_EQ(quads.size(), 2u);
    ExpectQuad(quads[1], 0x6c6, 290.0f, 250.0f, 54.0f, 30.0f);
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "a");
    EXPECT_EQ(text.texts[0].y, 0xef);

    mouse = {0x123, 0x100};
    Ui().dwords723bdc[0] = 3;
    CrywolfInterface copy = Ui();
    sprites.Begin(Screen640);
    GameHudContext second = Context(&copy);
    ASSERT_TRUE(Client::Events::Crywolf::DrawTheCrywolfContract(second).HasValue());
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6c8);
    EXPECT_EQ(copy.byte723a44, 0);
    EXPECT_EQ(Ui().byte723a44, 2);
    EXPECT_EQ(Ui().dwords723bdc[0], 3);

    sprites.Begin(Screen640);
    GameHudContext third = Context();
    ASSERT_TRUE(Client::Events::Crywolf::DrawTheCrywolfContract(third).HasValue());
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6c8);
    EXPECT_EQ(Ui().byte723a44, 0);
    EXPECT_EQ(Ui().dwords723bdc[0], 0);
}
} // namespace
