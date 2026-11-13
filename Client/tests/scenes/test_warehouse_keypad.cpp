#include "Scenes/WarehouseKeypad.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// La caja de la contraseña del almacén (CL9 hito 6, fase 3; 06 § Los dibujos de las ventanas de los NPC): lo que hay
// bajo el mouse (0x5e7b50) y el dibujo (0x5e9b10), en 640 × 480. Los textos y los números son inventados y los textos
// miden 6 columnas por letra. Cada test corre en Lorencia, Devias y Noria.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::KeypadKeyAt;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x2b7e1516u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Noria = 3;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color HoveredKey{1.0f, 1.0f, 0.8f, 1.0f};
constexpr Color Key{0.85f, 0.85f, 0.65f, 1.0f};
constexpr int NoTexture = -1;
// El marco (6), la barra (1), dos por tecla (22) y el OK y cancelar (2).
constexpr std::size_t KeypadQuads = 6 + 1 + 22 + 2;

std::map<std::size_t, std::string> KeypadTexts()
{
    return {{690, "Clave"},   {691, "Cambiar"}, {692, "Quitar"},   {693, "Nueva"},
            {694, "Repetir"}, {695, "Cuatro"},  {696, "Otra vez"}, {697, "Siete"}};
}

struct DrawnQuad
{
    int slot = NoTexture;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// Los quads con textura que no son marcas de texto, en su orden.
std::vector<DrawnQuad> TexturedQuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = command.texture->value,
                                      .color = vertices[first].color,
                                      .first = vertices[first],
                                      .third = vertices[first + 2]});
        }
    }
    return quads;
}

void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

// La esquina de la tecla `key` en el dibujo: de a 40 en x desde 0xdf y de a 38 en y desde 0x9a.
std::tuple<float, float> KeyCorner(int key)
{
    return {static_cast<float>((key % 5) * 40 + 0xdf), static_cast<float>((key / 5) * 38 + 0x9a)};
}

class WarehouseKeypadTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    WarehouseKeypadTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->Runtime().map = GetParam();
        // Un teclado barajado inventado.
        Runtime().npcWindows.keypad = {7, 3, 9, 0, 5, 1, 8, 2, 6, 4};
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x, std::int32_t y)
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = m_drawnTexts,
                               .mouse = {.x = x, .y = y}};
        return Client::Scenes::DrawTheKeypad(context);
    }

    void Type(std::string_view digits)
    {
        Runtime().npcWindows.typedNumber.fill(0);
        for (std::size_t i = 0; i < digits.size(); ++i)
            Runtime().npcWindows.typedNumber[i] = static_cast<std::uint8_t>(digits[i]);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(KeypadTexts());
    const Client::Data::Text::TextTable* m_drawnTexts = &m_texts;
};

// 0x5e7b50: las once teclas de 32 desde (0xdf, 0x96) de a 40, y el OK y cancelar de 0x46 × 0x15 desde (0x109, 0xeb)
// de a 0x4e, con los bordes de arriba y de la izquierda adentro.
TEST_P(WarehouseKeypadTest, TheKeyUnderTheMouse)
{
    for (const auto& [x, y, key] :
         {std::tuple{0xdf, 0x96, 0}, std::tuple{0xdf + 31, 0x96 + 31, 0}, std::tuple{0xdf + 32, 0x96, -1},
          std::tuple{0xdf + 40, 0x96, 1}, std::tuple{0xdf + 160, 0x96, 4}, std::tuple{0xdf, 0x96 + 40, 5},
          std::tuple{0xdf, 0x96 + 39, -1}, std::tuple{0xdf + 160, 0xbe, 9}, std::tuple{0xdf, 0xe6, 10},
          std::tuple{0xdf + 40, 0xe6, -1}, std::tuple{0x109, 0xeb, 11}, std::tuple{0x109 + 0x45, 0xff, 11},
          std::tuple{0x109 + 0x46, 0xff, -1}, std::tuple{0x157, 0xeb, 12}, std::tuple{0x157, 0x100, -1},
          std::tuple{0x157, 0xea, -1}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        EXPECT_EQ(KeypadKeyAt(x, y), key);
    }
}

// 0x5e9b16..0x5e9b1d: con el modo en 0, nada.
TEST_P(WarehouseKeypadTest, ClosedItDrawsNothing)
{
    ASSERT_TRUE(Draw(0, 0).HasValue());

    EXPECT_TRUE(TexturedQuadsOf(m_sprites).empty());
    EXPECT_TRUE(m_text.texts.empty());
}

// 0x5e9b23..0x5ea042 con el modo 1 (la contraseña de cuatro cifras): el marco, los dos renglones (690 y 695), la barra
// de 52 centrada en 320, las teclas con su número y el OK y cancelar. Sin rand().
TEST_P(WarehouseKeypadTest, ThePasswordModeDrawsTheFrameTheBarAndTheKeys)
{
    Runtime().ui.field78abb20 = 1;

    ASSERT_TRUE(Draw(0, 0).HasValue());

    EXPECT_EQ(m_random.State(), Seed);
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    ExpectQuadAt(quads[0], 0xfc, 213.0f, 100.0f, 213.0f, 5.0f);
    for (std::size_t row = 0; row < 4; ++row)
        ExpectQuadAt(quads[1 + row], 0xfb, 213.0f, 105.0f + 40.0f * static_cast<float>(row), 213.0f, 40.0f);
    ExpectQuadAt(quads[5], 0xfc, 213.0f, 265.0f, 213.0f, 5.0f);
    EXPECT_EQ(quads[5].color, White);
    ExpectQuadAt(quads[6], 0xfd, 294.0f, 134.0f, 52.0f, 16.0f);
    EXPECT_EQ(quads[6].color, (Color{0.3f, 0.3f, 0.3f, 1.0f}));
    for (int key = 0; key < 10; ++key)
    {
        SCOPED_TRACE(key);
        const auto [x, y] = KeyCorner(key);
        ExpectQuadAt(quads[7 + 2 * static_cast<std::size_t>(key)], 0x115, x, y, 32.0f, 32.0f);
        ExpectQuadAt(quads[8 + 2 * static_cast<std::size_t>(key)], 0x115, x, y, 32.0f, 32.0f);
        EXPECT_EQ(quads[7 + 2 * static_cast<std::size_t>(key)].color, Key);
    }
    ExpectQuadAt(quads[27], 0x115, 223.0f, 230.0f, 32.0f, 32.0f);
    ExpectQuadAt(quads[28], 0xfe, 223.0f, 230.0f, 32.0f, 32.0f);
    ExpectQuadAt(quads[29], 0xf1, 265.0f, 239.0f, 70.0f, 21.0f);
    ExpectQuadAt(quads[30], 0xf3, 343.0f, 239.0f, 70.0f, 21.0f);
    EXPECT_EQ(quads[29].color, White);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 2U + 10U);
    EXPECT_EQ(texts[0].text, "Clave");
    EXPECT_EQ(std::tuple(texts[0].x, texts[0].y), std::tuple(320 - 15, 0x6e));
    EXPECT_EQ(texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffffc4c4u);
    EXPECT_EQ(texts[0].style.colors.background, 0u);
    EXPECT_EQ(texts[1].text, "Cuatro");
    EXPECT_EQ(std::tuple(texts[1].x, texts[1].y), std::tuple(320 - 18, 0x7a));
    // El "*" de lo tecleado vacío no se dibuja; los números, con la fuente gruesa grande.
    EXPECT_EQ(texts[2].text, "7");
    EXPECT_EQ(std::tuple(texts[2].x, texts[2].y), std::tuple(0xdf + 0xc, 0x9a + 6));
    EXPECT_EQ(texts[2].style.font, FontKind::BigBold);
    EXPECT_EQ(texts[2].style.colors.text, 0xffc48080u);
    EXPECT_EQ(texts[11].text, "4");
    EXPECT_EQ(std::tuple(texts[11].x, texts[11].y), std::tuple(0xdf + 160 + 0xc, 0x9a + 38 + 6));
}

// 0x5e9d7b..0x5e9e12: lo tecleado, un "*" por byte, en (cifras − _ftol(mitad − 320), 0x84): con cuatro cifras, 4 +
// 294; con siete (modos 2, 3 y 6), la barra de 91 y 7 − _ftol(−274,5) = 281.
TEST_P(WarehouseKeypadTest, TheTypedDigitsAreStars)
{
    Runtime().ui.field78abb20 = 1;
    Type("1234");
    ASSERT_TRUE(Draw(0, 0).HasValue());
    ASSERT_GE(m_text.texts.size(), 3U);
    EXPECT_EQ(m_text.texts[2].text, "****");
    EXPECT_EQ(std::tuple(m_text.texts[2].x, m_text.texts[2].y), std::tuple(298, 0x84));
    EXPECT_EQ(m_text.texts[2].style.font, FontKind::BigBold);
    EXPECT_EQ(m_text.texts[2].style.colors.text, 0xffc48080u);

    Runtime().ui.field78abb20 = 2;
    Type("98765");
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    ExpectQuadAt(quads[6], 0xfd, 274.5f, 134.0f, 91.0f, 16.0f);
    ASSERT_GE(m_text.texts.size(), 3U);
    EXPECT_EQ(m_text.texts[0].text, "Cambiar");
    EXPECT_EQ(m_text.texts[1].text, "Siete");
    EXPECT_EQ(m_text.texts[2].text, "*****");
    EXPECT_EQ(m_text.texts[2].x, 281);
}

// 0x5e9c15..0x5e9cb3: el título 690 + (modo − 1) entre 0 y 4, el 6 con el 691; el pedido 697 con 2, 3 y 6, 696 con
// 5 y 695 con los demás.
TEST_P(WarehouseKeypadTest, TheModeChoosesTheTitleAndThePrompt)
{
    for (const auto& [mode, title, prompt] :
         {std::tuple{1, "Clave", "Cuatro"}, std::tuple{2, "Cambiar", "Siete"}, std::tuple{3, "Quitar", "Siete"},
          std::tuple{4, "Nueva", "Cuatro"}, std::tuple{5, "Repetir", "Otra vez"}, std::tuple{6, "Cambiar", "Siete"}})
    {
        SCOPED_TRACE(mode);
        Runtime().ui.field78abb20 = mode;
        ASSERT_TRUE(Draw(0, 0).HasValue());
        ASSERT_GE(m_text.texts.size(), 2U);
        EXPECT_EQ(m_text.texts[0].text, title);
        EXPECT_EQ(m_text.texts[1].text, prompt);
    }
}

// 0x5e9e1c..0x5e9fb4: la tecla del mouse (por 0x5e7b50, de a 40 desde 0x96) va en (1, 1, 0,8) con el texto en
// 0xff00c4c4; apretada (0x78abb3a), InventoryBox2 encima y el número un punto más arriba. La tecla 8 se dibuja en
// y = 0x9a + 38 = 192 y se toca desde y = 190.
TEST_P(WarehouseKeypadTest, TheKeyUnderTheMouseLightsUpAndPresses)
{
    Runtime().ui.field78abb20 = 1;
    const int key = 8;
    const int mouseX = 0xdf + 3 * 40;
    const int mouseY = 0x96 + 40;
    ASSERT_TRUE(Draw(mouseX, mouseY).HasValue());
    std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    const auto [x, y] = KeyCorner(key);
    ExpectQuadAt(quads[7 + 2 * key], 0x115, x, y, 32.0f, 32.0f);
    EXPECT_EQ(quads[7 + 2 * key].color, HoveredKey);
    ExpectQuadAt(quads[8 + 2 * key], 0x115, x, y, 32.0f, 32.0f);
    EXPECT_EQ(quads[9 + 2 * key].color, Key);
    ASSERT_EQ(m_text.texts.size(), 12U);
    EXPECT_EQ(m_text.texts[2 + key].text, "6");
    EXPECT_EQ(m_text.texts[2 + key].y, 192 + 6);
    EXPECT_EQ(m_text.texts[2 + key].style.colors.text, 0xff00c4c4u);

    Runtime().npcWindows.keypadPressed = 1;
    ASSERT_TRUE(Draw(mouseX, mouseY).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    ExpectQuadAt(quads[8 + 2 * key], 0x116, x, y, 32.0f, 32.0f);
    EXPECT_EQ(m_text.texts[2 + key].y, 192 + 5);
    EXPECT_EQ(m_text.texts[2 + key + 1].y, 192 + 6);
}

// 0x5e9ee3..0x5e9f27 y 0x5e9fee..0x5ea02a: borrar es back1 (0xfe), back2 con el mouse y back3 apretada; el OK y
// cancelar suben uno con el mouse.
TEST_P(WarehouseKeypadTest, TheEraseKeyAndTheButtonsLightUp)
{
    Runtime().ui.field78abb20 = 5;
    ASSERT_TRUE(Draw(0xdf, 0x96 + 80).HasValue());
    std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    ExpectQuadAt(quads[28], 0xff, 223.0f, 230.0f, 32.0f, 32.0f);
    EXPECT_EQ(quads[27].color, HoveredKey);

    Runtime().npcWindows.keypadPressed = 1;
    ASSERT_TRUE(Draw(0xdf, 0x96 + 80).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    ExpectQuadAt(quads[28], 0x100, 223.0f, 230.0f, 32.0f, 32.0f);

    ASSERT_TRUE(Draw(0x109, 0xeb).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    EXPECT_EQ(quads[28].slot, 0xfe);
    EXPECT_EQ(quads[29].slot, 0xf2);
    EXPECT_EQ(quads[30].slot, 0xf3);
    ASSERT_TRUE(Draw(0x157, 0xff).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    EXPECT_EQ(quads[29].slot, 0xf1);
    EXPECT_EQ(quads[30].slot, 0xf4);
}

// Sin el texto del título, corta con la dirección.
TEST_P(WarehouseKeypadTest, AMissingTextStopsWithItsAddress)
{
    Runtime().ui.field78abb20 = 3;
    m_drawnTexts = nullptr;

    const auto drawn = Draw(0, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5e9c63"), std::string::npos) << drawn.Error();
}

INSTANTIATE_TEST_SUITE_P(Maps, WarehouseKeypadTest, ::testing::Values(Lorencia, Devias, Noria));
} // namespace
