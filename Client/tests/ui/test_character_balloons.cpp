#include "FrontEnd/CharacterBalloons.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Ui/Widget.h"

#include "support/ExpectedError.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Client::FrontEnd::CharacterBalloons: the selection's balloons (+0x8dc8 of the new UI: 0x63ce70, 0x63cfb0 → 0x63cc60
// and 0x63cf80 → 0x63ca20). The texts are made up: only their indices and lengths matter.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::BalloonCount;
using Client::FrontEnd::BalloonHead;
using Client::FrontEnd::BalloonRecord;
using Client::FrontEnd::CharacterBalloons;
using Client::Render::Text::FontKind;
using Client::Ui::Point;

constexpr PixelSize Screen640{640, 480};
constexpr std::uint16_t BalloonSlot = 0x13;
const std::uint16_t Marker = Client::Tests::TextMarkerSlot.value;

using Records = std::array<BalloonRecord, BalloonCount>;
using Heads = std::array<std::optional<BalloonHead>, BalloonCount>;

// character_ex.tga, 118 × 54, in a 128 × 64 buffer.
Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{BalloonSlot, {128, 64}}});
}

// The guild ranks (1300, 1301, 1302, 1330 and 488, and 0 with another), the classes from 20 on and the three lines of
// 0x80 (1241 to 1243).
Client::Tables::TextTable LabTexts()
{
    return Client::Tests::MakeTextTable({{0, "Otro"},
                                         {20, "Mago"},
                                         {21, "Gran mago"},
                                         {25, "Caballero"},
                                         {488, "Sin gremio"},
                                         {1300, "Jefe"},
                                         {1301, "Ayudante"},
                                         {1302, "Batalla"},
                                         {1330, "Miembro"},
                                         {1241, "Primero"},
                                         {1242, "Segundo"},
                                         {1243, "Tercero"}});
}

BalloonRecord Alive(std::string name, std::uint8_t controlCode = 0)
{
    return BalloonRecord{.alive = true,
                         .characterClass = 0,
                         .level = 1,
                         .controlCode = controlCode,
                         .name = std::move(name),
                         .guildStatus = 0xff};
}

std::vector<std::uint16_t> QuadSlots(const Client::Render::SpriteBatch& batch)
{
    std::vector<std::uint16_t> slots;
    for (const auto& command : batch.Commands())
    {
        const std::uint16_t slot = command.texture ? command.texture->value : std::uint16_t{0xffff};
        slots.insert(slots.end(), command.vertexCount / 6, slot);
    }
    return slots;
}

// Top left, with y from the top.
Point FirstQuadCorner(const Client::Render::SpriteBatch& batch, PixelSize screen)
{
    const auto vertices = batch.Vertices();
    const auto quad = vertices.first(6);
    const auto left = std::ranges::min(quad, {}, &Client::Render::SpriteVertex::x).x;
    const auto top = std::ranges::max(quad, {}, &Client::Render::SpriteVertex::y).y;
    return Point{static_cast<int>(left), screen.height - static_cast<int>(top)};
}

struct Frame
{
    Client::Render::SpriteBatch batch;
    std::vector<Client::Tests::RecordedText> texts;
    std::optional<std::string> error;
};

Frame DrawOnce(CharacterBalloons& balloons, PixelSize screen, const Heads& heads)
{
    Frame frame;
    frame.batch.Begin(screen);
    Client::Tests::RecordingTextDrawer text(frame.batch);
    frame.error = Client::Tests::ErrorOf(balloons.Draw(frame.batch, text, screen, heads));
    frame.texts = text.texts;
    return frame;
}

// 0x63ce70 → 0x63c9a0: five 118 × 54 boxes of character_ex.tga at (0, 0), hidden, without color and without texts.
// Unbuilt (0x63ce50), neither the update nor the draw does anything.
TEST(CharacterBalloonsTest, BuildingLeavesFiveHiddenBoxesAtTheOrigin)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    EXPECT_FALSE(balloons.IsBuilt());
    Records records{};
    records.fill(Alive("nadie"));
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    EXPECT_FALSE(balloons.IsShown(0));

    balloons.Build();
    ASSERT_TRUE(balloons.IsBuilt());
    for (std::size_t index = 0; index < BalloonCount; ++index)
    {
        const auto& balloon = balloons.Balloons()[index];
        EXPECT_FALSE(balloons.IsShown(index));
        ASSERT_TRUE(balloon.image.slot.has_value());
        EXPECT_EQ(balloon.image.slot->value, BalloonSlot);
        EXPECT_EQ(balloon.image.x, 0);
        EXPECT_EQ(balloon.image.y, 0);
        EXPECT_EQ(balloon.image.width, 118);
        EXPECT_EQ(balloon.image.height, 54);
        EXPECT_FLOAT_EQ(balloon.image.textureWidth, 128.0f);
        EXPECT_FLOAT_EQ(balloon.image.textureHeight, 64.0f);
        EXPECT_EQ(balloon.nameColor, 0u);
        EXPECT_TRUE(balloon.name.empty());
    }
    const Frame hidden = DrawOnce(balloons, Screen640, Heads{});
    EXPECT_FALSE(hidden.error);
    EXPECT_TRUE(hidden.texts.empty());
    EXPECT_TRUE(hidden.batch.Commands().empty());

    balloons.Release();
    EXPECT_FALSE(balloons.IsBuilt());
    const Frame released = DrawOnce(balloons, Screen640, Heads{});
    EXPECT_FALSE(released.error);
    EXPECT_TRUE(released.batch.Commands().empty());
}

// 0x63cc60: the name color by the CtlCode, in the order of its tests: 1 yellow, 0x12 orange, 4 white before looking
// at 8 and 0x20, which go red, and white with nothing.
TEST(CharacterBalloonsTest, TheNameColorFollowsTheControlCode)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    const auto colorsOf = [&balloons](std::array<std::uint8_t, BalloonCount> codes)
    {
        Records records{};
        for (std::size_t index = 0; index < BalloonCount; ++index)
            records[index] = Alive("uno", codes[index]);
        EXPECT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
        std::array<std::uint32_t, BalloonCount> colors{};
        for (std::size_t index = 0; index < BalloonCount; ++index)
            colors[index] = balloons.Balloons()[index].nameColor;
        return colors;
    };
    EXPECT_EQ(
        colorsOf({0x01, 0x02, 0x10, 0x04, 0x00}),
        (std::array<std::uint32_t, BalloonCount>{0xffffff00u, 0xff27d9ffu, 0xff27d9ffu, 0xffffffffu, 0xffffffffu}));
    EXPECT_EQ(
        colorsOf({0x08, 0x20, 0x0c, 0x13, 0x80}),
        (std::array<std::uint32_t, BalloonCount>{0xff0000ffu, 0xff0000ffu, 0xffffffffu, 0xffffff00u, 0xffffffffu}));
}

// 0x63cd0d..0x63cdba: "(%s)" with the guild rank and "%s %d" with the class (20 + (c >> 3) · 5 + (c & 7)) and the
// level, unsigned.
TEST(CharacterBalloonsTest, TheGuildAndClassLinesComeFromTheRecord)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    const std::array<std::uint8_t, BalloonCount> ranks{0x80, 0x40, 0x20, 0x00, 0x33};
    for (std::size_t index = 0; index < BalloonCount; ++index)
    {
        records[index] = Alive("uno");
        records[index].guildStatus = ranks[index];
    }
    records[1].characterClass = 0x01;
    records[2].characterClass = 0x08;
    records[3].level = 400;
    records[4].level = 0xffff;
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));

    const auto& shown = balloons.Balloons();
    EXPECT_EQ(shown[0].name, "uno");
    EXPECT_EQ(shown[0].guild, "(Jefe)");
    EXPECT_EQ(shown[1].guild, "(Ayudante)");
    EXPECT_EQ(shown[2].guild, "(Batalla)");
    EXPECT_EQ(shown[3].guild, "(Miembro)");
    EXPECT_EQ(shown[4].guild, "(Otro)");
    EXPECT_EQ(shown[0].classAndLevel, "Mago 1");
    EXPECT_EQ(shown[1].classAndLevel, "Gran mago 1");
    EXPECT_EQ(shown[2].classAndLevel, "Caballero 1");
    EXPECT_EQ(shown[3].classAndLevel, "Mago 400");
    EXPECT_EQ(shown[4].classAndLevel, "Mago 65535");

    records[0].guildStatus = 0xff;
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    EXPECT_EQ(shown[0].guild, "(Sin gremio)");
}

// Q-C1503: "(%s)" has 16 bytes at +0xc0. With a 13-letter rank, its 15 and the 0 fit; with 14, the 0 falls on +0xd0,
// which the class sprintf overwrites, and 0x5542f0 reads the rank followed by the class and the level.
TEST(CharacterBalloonsTest, ALongGuildRankRunsIntoTheClassLine)
{
    auto texts = Client::Tests::MakeTextTable({{20, "Mago"}, {488, "abcdefghijklm"}, {1300, "abcdefghijklmn"}});
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[0] = Alive("uno");
    records[1] = Alive("dos");
    records[1].guildStatus = 0x80;
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    EXPECT_EQ(balloons.Balloons()[0].guild, "(abcdefghijklm)");
    EXPECT_EQ(balloons.Balloons()[1].guild, "(abcdefghijklmn)Mago 1");
    EXPECT_EQ(balloons.Balloons()[1].classAndLevel, "Mago 1");
}

// 0x63cd70 and 0x63cdba: a sprintf past the balloon's end (+0xf0) overwrites the next one: MuRet stops with the
// address.
TEST(CharacterBalloonsTest, ASprintfPastTheBalloonCuts)
{
    // "(" + 45 + ")" and the 0 are the 48 bytes from +0xc0 to +0xef; with 46, the 0 falls on +0xf0.
    auto texts = Client::Tests::MakeTextTable({{20, "Mago"},
                                               {21, std::string(29, 'c')},
                                               {22, std::string(30, 'c')},
                                               {488, std::string(45, 'g')},
                                               {1300, std::string(46, 'g')}});
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[2] = Alive("tres");
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    records[2].guildStatus = 0x80;
    auto guild = Client::Tests::ErrorOf(balloons.Update(records));
    ASSERT_TRUE(guild);
    EXPECT_NE(guild->find("0x63cd70"), std::string::npos) << *guild;
    EXPECT_NE(guild->find("lugar 2"), std::string::npos) << *guild;

    // The 29-letter class, " 1" and the 0 are 32 bytes: they fit exactly at +0xd0; with 30, not.
    records[2].guildStatus = 0xff;
    records[2].characterClass = 0x01;
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    records[2].characterClass = 0x02;
    auto level = Client::Tests::ErrorOf(balloons.Update(records));
    ASSERT_TRUE(level);
    EXPECT_NE(level->find("0x63cdba"), std::string::npos) << *level;
}

// 0x63cc71: with the record's +0 at 0, the balloon only hides: the texts and the color stay.
TEST(CharacterBalloonsTest, ADeadRecordOnlyHidesItsBalloon)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[3] = Alive("cuatro", 0x01);
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    EXPECT_TRUE(balloons.IsShown(3));
    EXPECT_FALSE(balloons.IsShown(2));

    records[3] = BalloonRecord{.alive = false, .name = "otro"};
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    EXPECT_FALSE(balloons.IsShown(3));
    EXPECT_EQ(balloons.Balloons()[3].name, "cuatro");
    EXPECT_EQ(balloons.Balloons()[3].nameColor, 0xffffff00u);
    EXPECT_EQ(balloons.Balloons()[3].classAndLevel, "Mago 1");
}

// 0x63ca20 at 640 × 480: the box goes where the previous draw left it (at (0, 0) the first one) and then is placed
// with the point (59, 54) on the head: (320, 200) leaves the corner at (261, 146). The three lines go centered in the
// 118-column box, at y + 7, + 23 and + 39, with the font of DAT_05763560 and the background at 0.
TEST(CharacterBalloonsTest, TheBoxLagsOneFrameAndTheLinesFollowTheHead)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[1] = Alive("dos", 0x08);
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    Heads heads{};
    heads[1] = BalloonHead{.point = Point{320, 200}};

    const Frame first = DrawOnce(balloons, Screen640, heads);
    ASSERT_FALSE(first.error) << *first.error;
    EXPECT_EQ(QuadSlots(first.batch), (std::vector<std::uint16_t>{BalloonSlot, Marker, Marker, Marker}));
    EXPECT_EQ(FirstQuadCorner(first.batch, Screen640), (Point{0, 0}));
    EXPECT_EQ(balloons.Balloons()[1].image.x, 261);
    EXPECT_EQ(balloons.Balloons()[1].image.y, 146);
    ASSERT_EQ(first.texts.size(), 3u);
    const std::array<std::string, 3> lines{"dos", "(Sin gremio)", "Mago 1"};
    const std::array<int, 3> rows{153, 169, 185};
    const std::array<std::uint32_t, 3> colors{0xff0000ffu, 0xffffffffu, 0xff27d9ffu};
    for (std::size_t line = 0; line < 3; ++line)
    {
        const auto& drawn = first.texts[line];
        EXPECT_EQ(drawn.text, lines[line]);
        EXPECT_EQ(drawn.x, 261);
        EXPECT_EQ(drawn.y, rows[line]);
        EXPECT_EQ(drawn.style.font, FontKind::OtherFace12);
        EXPECT_EQ(drawn.style.colors.text, colors[line]);
        EXPECT_EQ(drawn.style.colors.background, 0u);
        EXPECT_TRUE(drawn.windowed);
        // (118 − 6 · letters) / 2 with C division.
        const int column = (118 - 6 * static_cast<int>(lines[line].size())) / 2;
        EXPECT_EQ(drawn.firstColumn, -column);
        EXPECT_EQ(drawn.columns, 6 * static_cast<int>(lines[line].size()) + 2 * column);
    }

    const Frame second = DrawOnce(balloons, Screen640, heads);
    ASSERT_FALSE(second.error) << *second.error;
    EXPECT_EQ(FirstQuadCorner(second.batch, Screen640), (Point{261, 146}));

    heads[1]->point = Point{100, 50};
    const Frame moved = DrawOnce(balloons, Screen640, heads);
    ASSERT_FALSE(moved.error) << *moved.error;
    EXPECT_EQ(FirstQuadCorner(moved.batch, Screen640), (Point{261, 146}));
    EXPECT_EQ(balloons.Balloons()[1].image.x, 41);
    EXPECT_EQ(balloons.Balloons()[1].image.y, -4);
    EXPECT_EQ(moved.texts[0].y, 3);
}

// At 800 × 600 and 1024 × 768, the 640 × 480 point goes to pixels (fild, fmul by rx or ry and _ftol), the box is
// placed in pixels and the lines go back to 640 × 480 (fild, fdiv and _ftol).
TEST(CharacterBalloonsTest, OtherResolutionsScaleThePointAndTheLines)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[0] = Alive("uno");
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    Heads heads{};
    heads[0] = BalloonHead{.point = Point{320, 200}};

    struct Case
    {
        PixelSize screen;
        Point corner;
        int x;
        std::array<int, 3> rows;
    };
    for (const Case& at : {Case{PixelSize{800, 600}, Point{341, 196}, 272, {162, 175, 187}},
                           Case{PixelSize{1024, 768}, Point{453, 266}, 283, {170, 180, 190}}})
    {
        const Frame frame = DrawOnce(balloons, at.screen, heads);
        ASSERT_FALSE(frame.error) << *frame.error;
        EXPECT_EQ(balloons.Balloons()[0].image.x, at.corner.x) << at.screen.width;
        EXPECT_EQ(balloons.Balloons()[0].image.y, at.corner.y) << at.screen.width;
        ASSERT_EQ(frame.texts.size(), 3u);
        for (std::size_t line = 0; line < 3; ++line)
        {
            EXPECT_EQ(frame.texts[line].x, at.x) << at.screen.width;
            EXPECT_EQ(frame.texts[line].y, at.rows[line]) << at.screen.width;
        }
    }
}

// 0x63cbbc..0x63cc50: with the current CtlCode's 0x80, texts 1241 to 1243 above the box (y − 46 + 16 · i) in
// 0xffb4c8ff: the first in bold and the others in normal. The update's 0x80 does not count.
TEST(CharacterBalloonsTest, TheLiveControlCodeAddsThreeLinesAbove)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[4] = Alive("cinco", 0x80);
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    Heads heads{};
    heads[4] = BalloonHead{.point = Point{320, 200}};
    EXPECT_EQ(DrawOnce(balloons, Screen640, heads).texts.size(), 3u);

    heads[4]->controlCode = 0x80;
    const Frame frame = DrawOnce(balloons, Screen640, heads);
    ASSERT_FALSE(frame.error) << *frame.error;
    ASSERT_EQ(frame.texts.size(), 6u);
    const std::array<std::string, 3> lines{"Primero", "Segundo", "Tercero"};
    const std::array<int, 3> rows{100, 116, 132};
    const std::array<FontKind, 3> fonts{FontKind::Bold, FontKind::Normal, FontKind::Normal};
    for (std::size_t line = 0; line < 3; ++line)
    {
        const auto& drawn = frame.texts[3 + line];
        EXPECT_EQ(drawn.text, lines[line]);
        EXPECT_EQ(drawn.x, 261);
        EXPECT_EQ(drawn.y, rows[line]);
        EXPECT_EQ(drawn.style.font, fonts[line]);
        EXPECT_EQ(drawn.style.colors.text, 0xffb4c8ffu);
        EXPECT_EQ(drawn.style.colors.background, 0u);
    }
}

// 0x63ca76: a balloon shown without its character projected stops, after its box; a hidden one does not ask for it.
TEST(CharacterBalloonsTest, AShownBalloonWithoutItsHeadCuts)
{
    const auto texts = LabTexts();
    CharacterBalloons balloons(LabSizes(), &texts);
    balloons.Build();
    Records records{};
    records[2] = Alive("tres");
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    const Frame frame = DrawOnce(balloons, Screen640, Heads{});
    ASSERT_TRUE(frame.error);
    EXPECT_NE(frame.error->find("0x63ca76"), std::string::npos) << *frame.error;
    EXPECT_EQ(QuadSlots(frame.batch), (std::vector<std::uint16_t>{BalloonSlot}));
    EXPECT_TRUE(frame.texts.empty());
}
} // namespace
