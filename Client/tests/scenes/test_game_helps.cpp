#include "Scenes/GameHelps.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/TipBox.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Las ayudas de las superposiciones (CL8 hito 6; 06 § HUD y ventanas, "Las ayudas de F1"): los modos 1, 2 y 3 de
// 0x5b0550 y la caja de ayuda, 0x5af090. El texto mide como el rasterizador falso: 6 columnas por letra y 13 filas
// (también el '\n'). Los textos son inventados.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color LightBlue{0.5f, 0.7f, 1.0f, 1.0f};

// Los textos de las dos ayudas: el 125 es el más ancho de la primera (24 letras, 144 columnas).
std::map<std::size_t, std::string> HelpTexts()
{
    std::map<std::size_t, std::string> texts;
    texts[120] = "Ayuda";
    for (std::size_t index = 121; index <= 139; ++index)
        texts[index] = "linea " + std::to_string(index);
    texts[125] = "una linea bastante larga";
    texts[140] = "B";
    for (std::size_t index = 141; index <= 156; ++index)
        texts[index] = "otra " + std::to_string(index);
    return texts;
}

struct DrawnQuad
{
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// Los quads sin textura del lote (los de 0x621aa0), en orden.
std::vector<DrawnQuad> ColorQuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture.has_value())
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(
                DrawnQuad{.color = vertices[first].color, .first = vertices[first], .third = vertices[first + 2]});
        }
    }
    return quads;
}

// El color de la marca de cada texto, en orden.
std::vector<Color> TextColorsOf(const SpriteBatch& sprites)
{
    std::vector<Color> colors;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            colors.push_back(vertices[command.firstVertex + quad * 6].color);
    }
    return colors;
}

class GameHelpsTest : public ::testing::Test
{
protected:
    GameHelpsTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
        return GameHudContext{.world = *m_world,
                              .random = m_random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = m_sound,
                              .projecting = projecting,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = &m_texts};
    }

    Client::Core::Expected<void, std::string> Overlays(bool projecting = false)
    {
        GameHudContext context = Context(projecting);
        return Client::Scenes::DrawTheOverlays(context);
    }

    [[nodiscard]] std::int32_t& Mode()
    {
        return m_world->Runtime().panels.overlayMode;
    }

    [[nodiscard]] Client::Scenes::HudState& Hud()
    {
        return m_world->Runtime().hud;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{0x5eed1234u};
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(HelpTexts());
};

// 0x5b0559..0x5b069d: la lista de la primera ayuda ("\n", el título, "\n", los 19 textos y "\n") y 0x77f3b66.
TEST_F(GameHelpsTest, TheFirstHelpFillsTheTipList)
{
    Mode() = Client::UI::FirstHelpMode;
    ASSERT_TRUE(Overlays().HasValue());
    const Client::Scenes::TipList& tips = Hud().tips;
    ASSERT_EQ(tips.count, 23);
    EXPECT_EQ(tips.lines[0].text, "\n");
    EXPECT_EQ(tips.lines[1].text, "Ayuda");
    EXPECT_EQ(tips.lines[1].color, 1);
    EXPECT_EQ(tips.lines[1].bold, 1);
    EXPECT_EQ(tips.lines[2].text, "\n");
    EXPECT_EQ(tips.lines[3].text, "linea 121");
    EXPECT_EQ(tips.lines[3].color, 0);
    EXPECT_EQ(tips.lines[3].bold, 0);
    EXPECT_EQ(tips.lines[21].text, "linea 139");
    EXPECT_EQ(tips.lines[22].text, "\n");
    EXPECT_EQ(Hud().durabilityWarning, 1);
    EXPECT_EQ(Mode(), Client::UI::FirstHelpMode);
}

// 0x5af090(1, 1, 23, 0, 2, 1): la caja de 144 + 4 contra el borde izquierdo, de (3 · 13 · 0,5 + 20 · 13) / 0,9090909
// de alto, con el marco negro y el fondo al 80 %; cada texto centrado en sus 146 columnas y bajando 1,1 veces su alto
// (el '\n', la mitad). El título en negrita y celeste; el resto en blanco. Termina en blanco y sin mezcla.
TEST_F(GameHelpsTest, TheFirstHelpDrawsItsBox)
{
    Mode() = Client::UI::FirstHelpMode;
    ASSERT_TRUE(Overlays().HasValue());

    const std::vector<DrawnQuad> quads = ColorQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 5U);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_EQ(quads[i].color, (Color{0.0f, 0.0f, 0.0f, 1.0f})) << i;
    EXPECT_EQ(quads[4].color, (Color{0.0f, 0.0f, 0.0f, 0.8f}));
    // El fondo: x de 0 a 148 y la y de GL de 480 − 1 a 480 − (1 + 307,45).
    EXPECT_FLOAT_EQ(quads[4].first.x, 0.0f);
    EXPECT_FLOAT_EQ(quads[4].third.x, 148.0f);
    EXPECT_FLOAT_EQ(quads[4].first.y, 479.0f);
    EXPECT_NEAR(quads[4].third.y, 480.0f - 308.45f, 1e-3f);
    // El marco de la derecha, en x = −1 + 148 + 1.
    EXPECT_FLOAT_EQ(quads[2].first.x, 148.0f);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 20U);
    EXPECT_EQ(texts[0].text, "Ayuda");
    EXPECT_EQ(texts[0].x, 1 + (146 - 30) / 2);
    EXPECT_EQ(texts[0].y, 8);
    EXPECT_EQ(texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(texts[0].style.colors.background, 0u);
    EXPECT_EQ(texts[1].text, "linea 121");
    EXPECT_EQ(texts[1].x, 1 + (146 - 54) / 2);
    EXPECT_EQ(texts[1].y, 29);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[19].text, "linea 139");
    EXPECT_EQ(texts[19].y, 287);

    const std::vector<Color> colors = TextColorsOf(m_sprites);
    ASSERT_EQ(colors.size(), 20U);
    EXPECT_EQ(colors[0], LightBlue);
    EXPECT_EQ(colors[1], White);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Hud().dcFont, Client::Render::Text::FontKind::Normal);
}

// 0x5b06ac..0x5b07ef: la segunda, del 140 al 156.
TEST_F(GameHelpsTest, TheSecondHelpListsItsTexts)
{
    Mode() = Client::UI::SecondHelpMode;
    ASSERT_TRUE(Overlays().HasValue());
    const Client::Scenes::TipList& tips = Hud().tips;
    ASSERT_EQ(tips.count, 20);
    EXPECT_EQ(tips.lines[1].text, "B");
    EXPECT_EQ(tips.lines[3].text, "otra 141");
    EXPECT_EQ(tips.lines[18].text, "otra 156");
    EXPECT_EQ(m_text.texts.size(), 17U);
}

// Q-C613: un texto de 100 bytes o más no entra en su renglón.
TEST_F(GameHelpsTest, ALongTextCuts)
{
    std::map<std::size_t, std::string> texts = HelpTexts();
    texts[130] = std::string(100, 'x');
    m_texts = Client::Tests::MakeTextTable(texts);
    Mode() = Client::UI::FirstHelpMode;
    const auto drawn = Overlays();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("Q-C613"), std::string::npos);
}

// 0x5b089c..0x5b0944: las flechas, los virotes y los que no tienen franja cierran la ayuda del ítem.
TEST_F(GameHelpsTest, TheItemHelpClosesForArrowsAndUnknownItems)
{
    for (const std::int32_t item : {0x807, 0x80f, 0x1800, 0x1dff, 0x2000, -1})
    {
        Mode() = Client::UI::ItemHelpMode;
        m_world->Runtime().panels.helpItem = item;
        ASSERT_TRUE(Overlays().HasValue()) << item;
        EXPECT_EQ(Mode(), 0) << item;
    }
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_TRUE(m_text.texts.empty());
    // Un dibujo intermedio no la cierra.
    Mode() = Client::UI::ItemHelpMode;
    ASSERT_TRUE(Overlays(true).HasValue());
    EXPECT_EQ(Mode(), Client::UI::ItemHelpMode);
}

// 0x5b097b y 0x5b09a8: un ítem con franja corta en el dibujo; en 1.600 de ancho, las franjas 1 a 4 dividen por 0.
TEST_F(GameHelpsTest, TheItemHelpCutsWhereItDraws)
{
    Mode() = Client::UI::ItemHelpMode;
    m_world->Runtime().panels.helpItem = 0x10;
    const auto drawn = Overlays();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5b09a8"), std::string::npos);

    m_sprites.Begin(Client::App::PixelSize{1600, 1200});
    const auto divided = Overlays();
    ASSERT_FALSE(divided.HasValue());
    EXPECT_NE(divided.Error().find("0x5b097b"), std::string::npos);

    m_world->Runtime().panels.helpItem = 0x1e05;
    const auto fifth = Overlays();
    ASSERT_FALSE(fifth.HasValue());
    EXPECT_NE(fifth.Error().find("0x5b09a8"), std::string::npos);
}

// 0x5af471 y 0x5af3e3: la caja corta con otro modo de 0x465950 y con un renglón con fondo.
TEST_F(GameHelpsTest, TheTipBoxCutsWhatItCannotDraw)
{
    Client::Scenes::TipList& tips = Hud().tips;
    tips.lines[0] = Client::Scenes::TipLine{.text = "algo", .color = 7, .bold = 0};
    Client::Scenes::TipBoxContext box{.sprites = m_sprites, .text = m_text, .hud = Hud()};
    const auto other = Client::Scenes::DrawTheTipBox(box, {.x = 1, .y = 1, .count = 1, .width = 0, .mode = 1});
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x5af471"), std::string::npos);
    const auto shaded = Client::Scenes::DrawTheTipBox(box, {.x = 1, .y = 1, .count = 1, .width = 0, .mode = 2});
    ASSERT_FALSE(shaded.HasValue());
    EXPECT_NE(shaded.Error().find("0x5af3e3"), std::string::npos);
}

// 0x5af0c5..0x5af13d: la cuenta se corta en el primer renglón vacío; un espacio solo baja un renglón sin dibujarse.
// Con ancho, la caja mide su doble más 4 y se centra en x.
TEST_F(GameHelpsTest, TheTipBoxStopsAtTheFirstEmptyLine)
{
    Client::Scenes::TipList& tips = Hud().tips;
    tips.lines[0] = Client::Scenes::TipLine{.text = " ", .color = 0, .bold = 0};
    tips.lines[1] = Client::Scenes::TipLine{.text = "dos", .color = 4, .bold = 1};
    tips.lines[2] = Client::Scenes::TipLine{};
    tips.lines[3] = Client::Scenes::TipLine{.text = "nunca", .color = 0, .bold = 0};
    Client::Scenes::TipBoxContext box{.sprites = m_sprites, .text = m_text, .hud = Hud()};
    ASSERT_TRUE(
        Client::Scenes::DrawTheTipBox(box, {.x = 320, .y = 50, .count = 4, .width = 50, .mode = 2, .frame = false})
            .HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, "dos");
    // La caja: 2 · 50 + 4 = 104 de ancho desde 320 − 52; el texto en sus 102 columnas desde 269.
    EXPECT_EQ(m_text.texts[0].x, 269 + (102 - 18) / 2);
    // 50 + 13 · 1,1.
    EXPECT_EQ(m_text.texts[0].y, 64);
    EXPECT_TRUE(ColorQuadsOf(m_sprites).empty());
    EXPECT_EQ(TextColorsOf(m_sprites)[0], (Color{0.1f, 1.0f, 0.5f, 1.0f}));
    EXPECT_EQ(Hud().dcFont, Client::Render::Text::FontKind::Bold);
}
} // namespace
