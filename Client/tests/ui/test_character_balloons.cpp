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

// Client::FrontEnd::CharacterBalloons: los globos de la selección (+0x8dc8 de la UI nueva: 0x63ce70, 0x63cfb0 →
// 0x63cc60 y 0x63cf80 → 0x63ca20). Los textos son inventados: solo importan sus índices y largos.
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

// character_ex.tga, de 118 × 54, en un buffer de 128 × 64.
Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{BalloonSlot, {128, 64}}});
}

// Los rangos del gremio (1300, 1301, 1302, 1330 y 488, y el 0 con otro), las clases de la 20 en adelante y los tres
// renglones del 0x80 (1241 a 1243).
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

// La textura de cada quad del lote, en orden.
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

// La esquina de arriba a la izquierda del primer quad del lote, con la y desde arriba.
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

// 0x63ce70 → 0x63c9a0: cinco cajas de 118 × 54 de character_ex.tga en (0, 0), sin mostrar, sin color y sin textos.
// Sin armar (0x63ce50), ni la puesta al día ni el dibujo hacen nada.
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

// 0x63cc60: el color del nombre por el CtlCode, en el orden de sus pruebas: 1 amarillo, 0x12 naranja, 4 blanco antes
// de mirar el 8 y el 0x20, que van en rojo, y blanco sin nada.
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

// 0x63cd0d..0x63cdba: "(%s)" con el rango del gremio y "%s %d" con la clase (20 + (c >> 3) · 5 + (c & 7)) y el
// nivel, sin signo.
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

// Q-C1503: "(%s)" tiene 16 bytes en +0xc0. Con un rango de 13 letras, sus 15 y el 0 entran; con 14, el 0 cae en +0xd0,
// que el sprintf de la clase pisa, y 0x5542f0 lee el rango seguido de la clase y el nivel.
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

// 0x63cd70 y 0x63cdba: un sprintf que pasa del final del globo (+0xf0) pisa el que sigue: MuRet corta con la dirección.
TEST(CharacterBalloonsTest, ASprintfPastTheBalloonCuts)
{
    // "(" + 45 + ")" y el 0 son los 48 bytes de +0xc0 a +0xef; con 46, el 0 cae en +0xf0.
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

    // La clase de 29 letras, " 1" y el 0 son 32 bytes: entran justos en +0xd0; con 30, no.
    records[2].guildStatus = 0xff;
    records[2].characterClass = 0x01;
    ASSERT_FALSE(Client::Tests::ErrorOf(balloons.Update(records)));
    records[2].characterClass = 0x02;
    auto level = Client::Tests::ErrorOf(balloons.Update(records));
    ASSERT_TRUE(level);
    EXPECT_NE(level->find("0x63cdba"), std::string::npos) << *level;
}

// 0x63cc71: con el +0 del registro en 0, el globo solo se esconde: los textos y el color quedan.
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

// 0x63ca20 en 640 × 480: la caja va donde la dejó el dibujo anterior (en (0, 0) el primero) y después se ubica con el
// punto (59, 54) en la cabeza: (320, 200) deja la esquina en (261, 146). Los tres renglones van centrados en la caja de
// 118 columnas, en y + 7, + 23 y + 39, con la fuente de DAT_05763560 y el fondo en 0.
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
        // (118 − 6 · letras) / 2 con la división de C.
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

// En 800 × 600 y 1024 × 768, el punto de 640 × 480 pasa a píxeles (fild, fmul por rx o ry y _ftol), la caja se ubica
// en píxeles y los renglones vuelven a 640 × 480 (fild, fdiv y _ftol).
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

// 0x63cbbc..0x63cc50: con el 0x80 del CtlCode de ahora, los textos 1241 a 1243 arriba de la caja (y − 46 + 16 · i) en
// 0xffb4c8ff: el primero con la negrita y los otros con la normal. El 0x80 de la puesta al día no cuenta.
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

// 0x63ca76: un globo que se muestra sin su personaje proyectado corta, después de su caja; uno escondido no lo pide.
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
