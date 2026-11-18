#include "Scenes/GameTooltips.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Los tooltips (0x5f7bc0), su caja (0x5af090), la ventana del set (0x40e850) y el cartel de un conjunto (0x40eca0),
// CL8 hito 5, con textos y conjuntos inventados y el texto falso de RecordingTextDrawer (6 columnas por letra, 13 de
// alto) en 640 × 480, donde rx y ry son 1.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::TooltipLines;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint8_t None = 0xff;

// Los textos de formato de las opciones de un conjunto (0x40d530) y del título (1089 y 159), inventados.
Client::Data::Text::TextTable Texts()
{
    return Client::Tests::MakeTextTable({{159, "fin"},
                                         {632, "Magia +% d %%"},
                                         {950, "Fuerza +%d"},
                                         {951, "Agilidad +%d"},
                                         {970, "Ignora %d%%"},
                                         {957, "Opcion ocho %d"},
                                         {1089, "Set"}});
}

// ItemSetOption.bmd en claro, los 64 sin usar (0xff), como el archivo real.
class SetOptions
{
public:
    SetOptions() : m_plain(Client::Data::Tables::ItemSetOptionTableSize, std::byte{None}) {}

    SetOptions& Named(std::size_t set, std::string_view name, std::uint8_t pieces)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            At(set, i) = static_cast<std::byte>(name[i]);
        At(set, name.size()) = std::byte{0};
        At(set, 0x3c) = std::byte{pieces};
        return *this;
    }

    SetOptions& Byte(std::size_t set, std::size_t offset, std::uint8_t value)
    {
        At(set, offset) = std::byte{value};
        return *this;
    }

    [[nodiscard]] Client::Data::Tables::ItemSetTables Tables() const
    {
        return Client::Tests::ItemSetTablesFrom(
            std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0}), m_plain);
    }

private:
    std::byte& At(std::size_t set, std::size_t offset)
    {
        return m_plain[set * Client::Data::Tables::ItemSetOptionRecordSize + offset];
    }

    std::vector<std::byte> m_plain;
};

class Tooltips
{
public:
    explicit Tooltips(const Client::Data::Tables::ItemSetOptionTable* setOptions = nullptr) : m_setOptions(setOptions)
    {
        sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        Client::Scenes::ActorState& hero = world.Actors()[runtime.hero];
        return GameHudContext{.world = world,
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
                              .setOptions = m_setOptions,
                              .mouse = mouse};
    }

    TooltipLines& Lines()
    {
        return world.Runtime().hud.tooltip;
    }

    WorldState world;
    CrtRandom random;
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Data::Text::TextTable texts = Texts();
    Client::UI::Point mouse{};

private:
    const Client::Data::Tables::ItemSetOptionTable* m_setOptions = nullptr;
};

struct Quad
{
    Color color{};
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Los quads sin textura (los bordes y el fondo de 0x621aa0), con su color.
std::vector<Quad> ColorQuadsOf(const SpriteBatch& sprites)
{
    std::vector<Quad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            // En píxeles de GL, con la y hacia arriba: la esquina 0 es la de arriba a la izquierda.
            quads.push_back(Quad{.color = vertices[first].color,
                                 .left = vertices[first].x,
                                 .top = 480.0f - vertices[first].y,
                                 .right = vertices[first + 2].x,
                                 .bottom = 480.0f - vertices[first + 2].y});
        }
    }
    return quads;
}

// El color de la marca de cada texto: el glColor3f que había al dibujarlo.
std::vector<Color> TextColorsOf(const SpriteBatch& sprites)
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

void ExpectColor(const Color& color, float red, float green, float blue, float alpha = 1.0f)
{
    EXPECT_FLOAT_EQ(color.red, red);
    EXPECT_FLOAT_EQ(color.green, green);
    EXPECT_FLOAT_EQ(color.blue, blue);
    EXPECT_FLOAT_EQ(color.alpha, alpha);
}

void ExpectQuad(const Quad& quad, float left, float top, float width, float height)
{
    EXPECT_FLOAT_EQ(quad.left, left);
    EXPECT_FLOAT_EQ(quad.top, top);
    EXPECT_FLOAT_EQ(quad.right, left + width);
    EXPECT_FLOAT_EQ(quad.bottom, top + height);
}

// 0x5af090: tres renglones ("\n", uno dorado en negrita y uno azul) en (320, 100) con el marco.
TEST(GameTooltipsTest, TheBoxMeasuresFramesAndCentersEachLine)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "\n";
    lines.text[1] = "Uno";
    lines.colors[1] = Client::Scenes::TooltipGold;
    lines.bold[1] = 1;
    lines.text[2] = "Dos";
    lines.colors[2] = Client::Scenes::TooltipBlue;
    GameHudContext context = tested.Context();
    const auto drawn = Client::Scenes::DrawTheTooltipBox(context, 320, 100, 3, 0, 2, 1);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    // El ancho, 18 / rx + 4 = 22, centrado en 320: desde 309. El alto, (13 · 0,5 + 2 · 13) / 0,90909088.
    const float height = static_cast<float>(32.5 / static_cast<double>(0.90909088f));
    const std::vector<Quad> quads = ColorQuadsOf(tested.sprites);
    ASSERT_EQ(quads.size(), 5u);
    ExpectQuad(quads[0], 308.0f, 99.0f, 23.0f, 1.0f);
    ExpectQuad(quads[1], 308.0f, 99.0f, 1.0f, height + 1.0f);
    ExpectQuad(quads[2], 331.0f, 99.0f, 1.0f, height + 1.0f);
    ExpectQuad(quads[3], 308.0f, 100.0f + height, 24.0f, 1.0f);
    ExpectQuad(quads[4], 309.0f, 100.0f, 22.0f, height);
    for (std::size_t i = 0; i < 4; ++i)
        ExpectColor(quads[i].color, 0.0f, 0.0f, 0.0f);
    ExpectColor(quads[4].color, 0.0f, 0.0f, 0.0f, 0.8f);

    // El "\n" baja 6,5 · 1,1; cada texto va en la caja de 20 columnas desde 310 (0x467060 con el modo 2), corrido
    // (20 − 18) / 2, y baja 13 · 1,1.
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].text, "Uno");
    EXPECT_EQ(texts[0].x, 310);
    EXPECT_EQ(texts[0].y, 107);
    EXPECT_TRUE(texts[0].windowed);
    EXPECT_EQ(texts[0].firstColumn, -1);
    EXPECT_EQ(texts[0].columns, 20);
    EXPECT_EQ(texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[1].text, "Dos");
    EXPECT_EQ(texts[1].x, 310);
    EXPECT_EQ(texts[1].y, 121);
    EXPECT_EQ(texts[1].firstColumn, -1);
    EXPECT_EQ(texts[1].columns, 20);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[1].style.colors.text, 0xffffffffu);
    EXPECT_EQ(texts[1].style.colors.background, 0u);
    const std::vector<Color> colors = TextColorsOf(tested.sprites);
    ASSERT_EQ(colors.size(), 2u);
    ExpectColor(colors[0], 1.0f, 0.8f, 0.1f);
    ExpectColor(colors[1], 0.5f, 0.7f, 1.0f);
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Opaque);
}

// 0x5af0c5: el primer renglón vacío corta la caja; 0x5af3d7: el 9 es verde sobre rojo, y el 5 lleva fondo azul.
TEST(GameTooltipsTest, AnEmptyLineEndsTheBoxAndSomeColorsCarryABackground)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "Set";
    lines.colors[0] = Client::Scenes::TooltipGreenOnRed;
    lines.text[1] = "Azul";
    lines.colors[1] = Client::Scenes::TooltipWhiteOnBlue;
    lines.text[3] = "No sale";
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Scenes::DrawTheTooltipBox(context, 0, 0, 5, 0, 2, 0).HasValue());
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].style.colors.text, 0xff00ff00u);
    EXPECT_EQ(texts[0].style.colors.background, 0xffc83c3cu);
    EXPECT_EQ(texts[1].style.colors.background, 0xff0000a0u);
    // Sin marco, ningún quad sin textura; contra el borde izquierdo, la caja empieza en 0 y el texto en 1. El fondo
    // llena las 26 columnas de la caja, también las de los costados del texto (P-C145).
    EXPECT_TRUE(ColorQuadsOf(tested.sprites).empty());
    EXPECT_EQ(texts[0].x, 1);
    EXPECT_EQ(texts[0].firstColumn, -(26 - 18) / 2);
    EXPECT_EQ(texts[0].columns, 26);
    EXPECT_EQ(texts[1].x, 1);
    EXPECT_EQ(texts[1].firstColumn, -(26 - 24) / 2);
    EXPECT_EQ(texts[1].columns, 26);
}

// 0x5af090 con el modo 3, el de las columnas de la ayuda del ítem (0x5af9b0): cada texto contra el borde derecho de la
// caja (0x467134..0x467144), en la columna caja − cx de una caja de 20 columnas justas, con el fondo en toda ella.
TEST(GameTooltipsTest, TheModeThreePutsEachLineAgainstTheRightEdge)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "1";
    lines.colors[0] = Client::Scenes::TooltipRed;
    lines.text[1] = "100";
    lines.colors[1] = Client::Scenes::TooltipWhiteOnRed;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Scenes::DrawTheTooltipBox(context, 100, 50, 2, 0, 3, 0).HasValue());
    // El ancho, 18 + 4, centrado en 100: desde 89, y los textos desde 90 en 20 columnas.
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].x, 90);
    EXPECT_EQ(texts[0].y, 50);
    EXPECT_EQ(texts[0].firstColumn, 6 - 20);
    EXPECT_EQ(texts[0].columns, 20);
    EXPECT_EQ(texts[0].style.colors.background, 0u);
    EXPECT_EQ(texts[1].x, 90);
    EXPECT_EQ(texts[1].y, 64);
    EXPECT_EQ(texts[1].firstColumn, 18 - 20);
    EXPECT_EQ(texts[1].columns, 20);
    EXPECT_EQ(texts[1].style.colors.background, 0xffa00000u);
    EXPECT_TRUE(ColorQuadsOf(tested.sprites).empty());
    const std::vector<Color> colors = TextColorsOf(tested.sprites);
    ASSERT_EQ(colors.size(), 2u);
    ExpectColor(colors[0], 1.0f, 0.2f, 0.1f);
    ExpectColor(colors[1], 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Opaque);
}

// 0x5af090 con el modo 1, el de la ayuda del retrato de las cartas (0x497f3f): cada texto contra el borde izquierdo de
// la caja (0x467102..0x467119), en la columna 0 de una caja de 20 columnas justas, con el marco.
TEST(GameTooltipsTest, TheModeOnePutsEachLineAgainstTheLeftEdge)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "1";
    lines.text[1] = "100";
    lines.colors[1] = Client::Scenes::TooltipWhiteOnRed;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Scenes::DrawTheTooltipBox(context, 100, 50, 2, 0, 1, 1).HasValue());
    // El ancho, 18 + 4, centrado en 100: desde 89, y los textos desde 90 en 20 columnas, en la primera.
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].x, 90);
    EXPECT_EQ(texts[0].y, 50);
    EXPECT_EQ(texts[0].firstColumn, 0);
    EXPECT_EQ(texts[0].columns, 20);
    EXPECT_EQ(texts[1].x, 90);
    EXPECT_EQ(texts[1].y, 64);
    EXPECT_EQ(texts[1].firstColumn, 0);
    EXPECT_EQ(texts[1].columns, 20);
    EXPECT_EQ(texts[1].style.colors.background, 0xffa00000u);
    const std::vector<Quad> quads = ColorQuadsOf(tested.sprites);
    ASSERT_EQ(quads.size(), 5u);
    ExpectQuad(quads[4], 89.0f, 50.0f, 22.0f, quads[4].bottom - 50.0f);
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Opaque);
    // Otro modo corta antes de dibujar.
    EXPECT_FALSE(Client::Scenes::DrawTheTooltipBox(context, 100, 50, 2, 0, 4, 1).HasValue());
}

// En 800 × 600 (rx = 1,25) la caja del texto es de píxeles y la x, la de 640 × 480 sin pasar por la pantalla: con
// "Uno" (18 píxeles) en x 320, la caja mide 18 / 1,25 + 4 = 18,4 desde 310 y el texto va desde 311, en una caja de
// _ftol(16,4 · 1,25) = 20 columnas. Con el modo 3, el texto en la columna 20 − 18.
TEST(GameTooltipsTest, TheTextBoxIsInPixelsAtAnotherResolution)
{
    for (const std::int32_t mode : {2, 3})
    {
        Tooltips tested;
        tested.sprites.Begin(Client::App::PixelSize{800, 600});
        TooltipLines& lines = tested.Lines();
        lines.text[0] = "Uno";
        lines.colors[0] = Client::Scenes::TooltipGreenOnRed;
        GameHudContext context = tested.Context();
        ASSERT_TRUE(Client::Scenes::DrawTheTooltipBox(context, 320, 100, 1, 0, mode, 0).HasValue()) << mode;
        const auto& texts = tested.text.texts;
        ASSERT_EQ(texts.size(), 1u) << mode;
        EXPECT_EQ(texts[0].x, 311) << mode;
        EXPECT_EQ(texts[0].y, 100) << mode;
        EXPECT_EQ(texts[0].firstColumn, mode == 2 ? -1 : -2) << mode;
        EXPECT_EQ(texts[0].columns, 20) << mode;
        EXPECT_EQ(texts[0].style.colors.background, 0xffc83c3cu) << mode;
    }
}

// 0x40d530: la opción elige el texto de formato; con una de más de 0x23 el renglón queda.
TEST(GameTooltipsTest, ASetOptionPicksItsFormatText)
{
    const Client::Data::Text::TextTable texts = Texts();
    std::string line = "viejo";
    ASSERT_TRUE(Client::Scenes::WriteTheSetOption(&texts, line, 0, 12).HasValue());
    EXPECT_EQ(line, "Fuerza +12");
    ASSERT_TRUE(Client::Scenes::WriteTheSetOption(&texts, line, 7, 3).HasValue());
    EXPECT_EQ(line, "Magia + 3 %");
    ASSERT_TRUE(Client::Scenes::WriteTheSetOption(&texts, line, 8, 4).HasValue());
    EXPECT_EQ(line, "Opcion ocho 4");
    ASSERT_TRUE(Client::Scenes::WriteTheSetOption(&texts, line, 0x15, 9).HasValue());
    EXPECT_EQ(line, "Ignora 9%");
    ASSERT_TRUE(Client::Scenes::WriteTheSetOption(&texts, line, 0x24, 9).HasValue());
    EXPECT_EQ(line, "Ignora 9%");
    EXPECT_FALSE(Client::Scenes::WriteTheSetOption(nullptr, line, 2, 9).HasValue());
}

// 0x40e850 desde 0x5f7bc0: con el inventario abierto y +0x9304, el conjunto puesto y sus opciones; una de 0xa5 o más
// sin la marca va en rojo y no escribe su texto. Después, +0x9304 en 0.
TEST(GameTooltipsTest, TheSetWindowListsTheWornSetAndTurnsItselfOff)
{
    Tooltips tested;
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.panels.inventory = 1;
    runtime.hud.inventoryX = 0x1c2;
    Client::Scenes::ItemSetState& sets = tested.world.ItemSets();
    sets.byte9304 = 1;
    sets.names[0] = {'A', 'l', 'f', 'a'};
    sets.list.count9348 = 2;
    sets.list.entries[0] = {0, 1, 0};
    sets.list.values[0] = 0x105;
    sets.list.entries[1] = {0xa5, 0, 0};
    sets.list.values[1] = 3;
    tested.Lines().text[4] = "viejo";
    GameHudContext context = tested.Context();
    const auto drawn = Client::Scenes::DrawTheTooltips(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TooltipLines& lines = tested.Lines();
    EXPECT_EQ(lines.text[0], "\n");
    EXPECT_EQ(lines.text[3], "Alfa Set");
    EXPECT_EQ(lines.colors[3], Client::Scenes::TooltipGold);
    EXPECT_EQ(lines.bold[3], 1);
    EXPECT_EQ(lines.text[4], "Fuerza +5");
    EXPECT_EQ(lines.colors[4], Client::Scenes::TooltipBlue);
    EXPECT_EQ(lines.text[5], "");
    EXPECT_EQ(lines.colors[5], Client::Scenes::TooltipRed);
    EXPECT_EQ(sets.byte9304, 0);
    // El renglón vacío corta la caja: salen el título y la opción.
    ASSERT_EQ(tested.text.texts.size(), 2u);
    EXPECT_EQ(tested.text.texts[0].text, "Alfa Set");
}

// Un dibujo intermedio de MuRet dibuja la ventana pero no apaga +0x9304.
TEST(GameTooltipsTest, AProjectedFrameKeepsTheSetWindowOn)
{
    Tooltips tested;
    tested.world.Runtime().panels.inventory = 1;
    Client::Scenes::ItemSetState& sets = tested.world.ItemSets();
    sets.byte9304 = 1;
    sets.list.count9349 = 1;
    sets.names[1] = {'B'};
    GameHudContext context = tested.Context(true);
    ASSERT_TRUE(Client::Scenes::DrawTheTooltips(context).HasValue());
    EXPECT_EQ(sets.byte9304, 1);
    EXPECT_EQ(tested.Lines().text[3], "\n");
    EXPECT_EQ(tested.Lines().text[4], "B Set");
}

// 0x40eca0: el nombre con el 1089 y el 159, los pares, los extras y el conjunto completo, en ese orden.
TEST(GameTooltipsTest, TheSetLabelListsTheOptionsOfTheSet)
{
    SetOptions options;
    options.Named(1, "Beta", 3)
        .Byte(1, 0x20, 0)
        .Byte(1, 0x2c, 10)
        .Byte(1, 0x21, 1)
        .Byte(1, 0x2d, 20)
        .Byte(1, 0x38, 7)
        .Byte(1, 0x3a, 2)
        .Byte(1, 0x3d, 0x15)
        .Byte(1, 0x42, 6);
    const Client::Data::Tables::ItemSetTables tables = options.Tables();
    Tooltips tested(&tables.options);
    tested.world.ItemSets().byte9305 = 2;
    tested.Lines().colors[25] = 4;
    // Q-C596: la negrita que dejó un cuerpo anterior en el título queda.
    tested.Lines().bold[1] = 1;
    GameHudContext context = tested.Context();
    const auto drawn = Client::Scenes::DrawTheSetLabel(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TooltipLines& lines = tested.Lines();
    const std::vector<std::string> expected{"\n",           "Beta Set fin", "\n",        "\n", "Fuerza +10",
                                            "Agilidad +20", "Magia + 2 %",  "Ignora 6%", "\n", "\n"};
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(lines.text[i], expected[i]) << i;
    EXPECT_EQ(lines.text[expected.size()], "");
    EXPECT_EQ(lines.colors[1], Client::Scenes::TooltipGold);
    EXPECT_EQ(lines.colors[4], Client::Scenes::TooltipBlue);
    EXPECT_EQ(lines.colors[7], Client::Scenes::TooltipBlue);
    // Limpia 20 colores de los 30: el 25 queda.
    EXPECT_EQ(lines.colors[25], 4);
    EXPECT_EQ(lines.bold[1], 1);
    EXPECT_EQ(tested.world.ItemSets().byte9305, 2);
    // La caja va arriba a la izquierda: el ancho mayor ("Agilidad +20", 72) + 4, contra el 0.
    ASSERT_FALSE(tested.text.texts.empty());
    EXPECT_EQ(tested.text.texts[0].text, "Beta Set fin");
    EXPECT_EQ(tested.text.texts[0].x, 1);
    EXPECT_EQ(tested.text.texts[0].firstColumn, -(74 - 72) / 2);
    EXPECT_EQ(tested.text.texts[0].columns, 74);
}

// 0x40ed11..0x40ee15 (Q-C592): después del sexto par la segunda opción queda la de ese par, y sale otra vez en cada
// uno de los siete lugares que siguen.
TEST(GameTooltipsTest, TheSecondOptionOfTheSixthPairRepeatsInTheSetLabel)
{
    SetOptions options;
    options.Named(0, "Gamma", 2).Byte(0, 0x2b, 1).Byte(0, 0x37, 9);
    const Client::Data::Tables::ItemSetTables tables = options.Tables();
    Tooltips tested(&tables.options);
    tested.world.ItemSets().byte9305 = 1;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Scenes::DrawTheSetLabel(context).HasValue());
    const TooltipLines& lines = tested.Lines();
    for (std::size_t i = 4; i < 12; ++i)
        EXPECT_EQ(lines.text[i], "Agilidad +9") << i;
    EXPECT_EQ(lines.text[12], "\n");
    EXPECT_EQ(lines.text[13], "\n");
}

// 0x40ecf3: un conjunto sin usar apaga el cartel sin dibujar nada.
TEST(GameTooltipsTest, AnUnusedSetTurnsTheLabelOff)
{
    const Client::Data::Tables::ItemSetTables tables = SetOptions().Tables();
    Tooltips tested(&tables.options);
    tested.world.ItemSets().byte9305 = 3;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Scenes::DrawTheSetLabel(context).HasValue());
    EXPECT_EQ(tested.world.ItemSets().byte9305, 0);
    EXPECT_TRUE(tested.text.texts.empty());
}

// 0x5f7bc0: con el modo de reparar en 2, vuelve a 1 (fuera de un dibujo intermedio) y apaga 0x77f3b66.
TEST(GameTooltipsTest, TheTooltipsResetTheRepairModeAndTheDurabilityWarning)
{
    Tooltips tested;
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.panels.repairMode = 2;
    runtime.hud.durabilityWarning = 1;
    GameHudContext projected = tested.Context(true);
    ASSERT_TRUE(Client::Scenes::DrawTheTooltips(projected).HasValue());
    EXPECT_EQ(runtime.panels.repairMode, 2);
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Scenes::DrawTheTooltips(context).HasValue());
    EXPECT_EQ(runtime.panels.repairMode, 1);
    EXPECT_EQ(runtime.hud.durabilityWarning, 0);
}
} // namespace
