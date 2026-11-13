#include "Scenes/NpcWindowDrawing.h"

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
#include "Scenes/GamePanels.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// El dibujo de las ventanas de los NPC (CL9 hito 6, fase 3; 06 § Las ventanas de los NPC): la tienda (0x5f4720), en
// 640 × 480. Los textos y los precios son inventados y los textos miden 6 columnas por letra. Cada test corre en
// Lorencia, Devias y Noria.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x6a3c19e5u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Noria = 3;
// 0x5f474c: la esquina de la tienda.
constexpr float Left = 260.0f;
// Los botones del pie: y + 365 y x + 25, 55, 85 y 115.
constexpr float FootY = 365.0f;
constexpr float BuyX = Left + 25.0f;
constexpr float SellX = Left + 55.0f;
constexpr float RepairX = Left + 85.0f;
constexpr float RepairAllX = Left + 115.0f;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color RepairAllColor{0.5f, 0.7f, 1.0f, 1.0f};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr std::size_t GridCells = 8 * 15;

std::map<std::size_t, std::string> ShopTexts()
{
    return {{230, "Tienda"},  {1623, "Impuesto %d%%"}, {231, "Comprar"}, {232, "Vender"},
            {233, "Reparar"}, {237, "Reparar todo"},   {239, "Precio"}};
}

// Un quad del lote: el slot (NoTexture sin textura, Marker si es la marca de un texto), la mezcla, el color y las
// esquinas 0 y 2 del fan.
struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot,
                                      .blend = command.blend,
                                      .color = vertices[first].color,
                                      .first = vertices[first],
                                      .third = vertices[first + 2]});
        }
    }
    return quads;
}

// Los quads con textura que no son marcas de texto, en su orden.
std::vector<DrawnQuad> TexturedQuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> textured;
    for (const DrawnQuad& quad : QuadsOf(sprites))
    {
        if (quad.slot >= 0)
            textured.push_back(quad);
    }
    return textured;
}

// Las esquinas en píxeles de GL (H − (y + h)).
void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class NpcWindowDrawingTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    NpcWindowDrawingTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->Runtime().map = GetParam();
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Core::Expected<void, std::string> DrawTheShop(std::int32_t x, std::int32_t y, bool projecting = false)
    {
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
                               .projecting = projecting,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = m_drawnTexts,
                               .mouse = {.x = x, .y = y}};
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::DrawTheNpcShop(context, panels.Registry());
    }

    // La tienda abierta, como la dejan el 30 con 0 (0x65ba67, el byte protegido sin accesos abiertos) y el 31 con la
    // grilla vacía (el tipo en 0xffff).
    void Open()
    {
        Runtime().panels.npcShop = 1;
        for (Client::World::ItemState& cell : Runtime().npcWindows.grid)
            cell.id = Client::Scenes::EmptyOwnItemId;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(ShopTexts());
    const Client::Data::Text::TextTable* m_drawnTexts = &m_texts;
};

// 0x5f4736..0x5f473d: cerrada, solo la consulta del byte protegido (1 rand()); la esquina queda como estaba.
TEST_P(NpcWindowDrawingTest, TheClosedShopOnlySpendsItsQuery)
{
    Runtime().hud.npcWindowX = 7;
    const CrtRandom expected = Advanced(m_random, 1);

    ASSERT_TRUE(DrawTheShop(0, 0).HasValue());

    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().hud.npcWindowX, 7);
}

// 0x5f4743..0x5f4a40: la esquina (0x104, 0), el fondo, las 120 casillas, el título y el impuesto (sprintf con
// 0x78aba30) en negrita y en cajas de 120, y comprar (suelto con 0x78abb27 en 0) y vender (apretado). Sin 0x78abb09,
// nada más. Solo el rand() de la consulta.
TEST_P(NpcWindowDrawingTest, TheShopDrawsItsBackGridTitleTaxAndTwoButtons)
{
    Open();
    Runtime().npcWindows.taxRate = 3;
    Runtime().npcWindows.grid[8 + 3].id = 0x0c0e;
    const CrtRandom expected = Advanced(m_random, 1);

    const auto drawn = DrawTheShop(0, 0);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().hud.npcWindowX, 0x104);
    EXPECT_EQ(Runtime().hud.npcWindowY, 0);
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 2);
    ExpectQuadAt(quads[0], 0x104, Left, 0.0f, 190.0f, 256.0f);
    ExpectQuadAt(quads[1], 0x105, Left, 256.0f, 190.0f, 177.0f);
    ExpectQuadAt(quads[2], 0x115, Left + 15.0f, 50.0f, 20.0f, 20.0f);
    ExpectQuadAt(quads[1 + GridCells], 0x115, Left + 15.0f + 140.0f, 50.0f + 280.0f, 20.0f, 20.0f);
    ExpectQuadAt(quads[2 + 8 + 3], 0x116, Left + 15.0f + 60.0f, 50.0f + 20.0f, 20.0f, 20.0f);
    ExpectQuadAt(quads[2 + GridCells], 0x11b, BuyX, FootY, 24.0f, 24.0f);
    ExpectQuadAt(quads[3 + GridCells], 0x11c, SellX, FootY, 24.0f, 24.0f);
    EXPECT_EQ(quads[2 + GridCells].color, White);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 2U);
    EXPECT_EQ(texts[0].text, "Tienda");
    EXPECT_EQ(std::tuple(texts[0].x, texts[0].y), std::tuple(0x104 + 0x23, 8));
    EXPECT_TRUE(texts[0].windowed);
    EXPECT_EQ(texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(texts[0].style.colors.background, 0xff141414u);
    EXPECT_EQ(texts[1].text, "Impuesto 3%");
    EXPECT_EQ(std::tuple(texts[1].x, texts[1].y), std::tuple(0x104 + 0x23, 0x14));
    EXPECT_TRUE(texts[1].windowed);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Bold);
}

// 0x5f4882..0x5f49af: con el modo de la tienda en 1 (vender), comprar se dibuja apretado y vender suelto.
TEST_P(NpcWindowDrawingTest, TheShopModePressesBuyOrSell)
{
    Open();
    Runtime().panels.repairCursor = 1;

    ASSERT_TRUE(DrawTheShop(0, 0).HasValue());

    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 2);
    ExpectQuadAt(quads[2 + GridCells], 0x11a, BuyX, FootY, 24.0f, 24.0f);
    ExpectQuadAt(quads[3 + GridCells], 0x11d, SellX, FootY, 24.0f, 24.0f);
}

// 0x5f4a46..0x5f4e13: con 0x78abb09, reparar (0x11e suelto con 0x78abb0c en 0), reparar todo en celeste y el precio
// de reparar todo (0x78ababc) sobre su fondo, en blanco: el texto 239 en x − 0x23 y el monto con comas en x + 10, en
// el color de su monto.
TEST_P(NpcWindowDrawingTest, TheRepairingShopDrawsRepairAndItsPrice)
{
    Open();
    Runtime().panels.field78abb09 = 1;
    Runtime().npcWindows.repairAllPrice = 1234567;
    const CrtRandom expected = Advanced(m_random, 1);

    const auto drawn = DrawTheShop(0, 0);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_random.State(), expected.State());
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 5);
    ExpectQuadAt(quads[4 + GridCells], 0x11e, RepairX, FootY, 24.0f, 24.0f);
    EXPECT_EQ(quads[4 + GridCells].color, White);
    ExpectQuadAt(quads[5 + GridCells], 0x11e, RepairAllX, FootY, 24.0f, 24.0f);
    EXPECT_EQ(quads[5 + GridCells].color, RepairAllColor);
    ExpectQuadAt(quads[6 + GridCells], 0x10f, RepairAllX - 60.0f, FootY + 30.0f, 113.0f, 18.0f);
    EXPECT_EQ(quads[6 + GridCells].color, White);
    EXPECT_NEAR(quads[6 + GridCells].third.u, 0.8828125f, 1e-6f);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4U);
    EXPECT_EQ(texts[2].text, "Precio");
    EXPECT_EQ(std::tuple(texts[2].x, texts[2].y), std::tuple(315 - 0x23, 398));
    EXPECT_FALSE(texts[2].windowed);
    EXPECT_EQ(texts[2].style.font, FontKind::Bold);
    EXPECT_EQ(texts[2].style.colors.text, 0xff96dcffu);
    EXPECT_EQ(texts[2].style.colors.background, 0xff282828u);
    EXPECT_EQ(texts[3].text, "1,234,567");
    EXPECT_EQ(std::tuple(texts[3].x, texts[3].y), std::tuple(325, 398));
    EXPECT_EQ(texts[3].style.colors.text, 0xff0096ffu);
    EXPECT_EQ(texts[3].style.colors.background, 0xff282828u);
}

// 0x5f4882 y 0x5f4a4c: reparar con 0x78abb0c prendido, apretado; el precio de más de 10 millones, en rojo, y sin
// signo (el DIV de 0x5f4cc1 y los JC).
TEST_P(NpcWindowDrawingTest, TheRepairModePressesRepairAndBigPricesAreRed)
{
    Open();
    Runtime().panels.field78abb09 = 1;
    Runtime().panels.repairMode = 1;
    Runtime().npcWindows.repairAllPrice = -1;

    ASSERT_TRUE(DrawTheShop(0, 0).HasValue());

    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 5);
    ExpectQuadAt(quads[4 + GridCells], 0x11f, RepairX, FootY, 24.0f, 24.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4U);
    EXPECT_EQ(texts[3].text, "4,294,967,295");
    EXPECT_EQ(texts[3].style.colors.text, 0xff0000ffu);
}

// 0x5f48d3..0x5f494f: el mouse en [x, x + 24) × [y, y + 24) de un botón dibuja su ayuda con la fuente normal en
// (x, y − 12); afuera, nada.
TEST_P(NpcWindowDrawingTest, AButtonUnderTheMouseShowsItsHelp)
{
    Open();
    Runtime().panels.field78abb09 = 1;
    for (const auto& [x, y, help] :
         {std::tuple{285, 365, "Comprar"}, std::tuple{308, 388, "Comprar"}, std::tuple{315, 370, "Vender"},
          std::tuple{345, 365, "Reparar"}, std::tuple{284, 365, ""}, std::tuple{309, 365, ""}, std::tuple{285, 389, ""},
          std::tuple{285, 364, ""}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        m_text.texts.clear();
        m_sprites.Begin(Screen640);

        ASSERT_TRUE(DrawTheShop(x, y).HasValue());

        std::vector<std::string> helps;
        for (const auto& text : m_text.texts)
        {
            if (text.style.font == FontKind::Normal)
                helps.push_back(text.text);
        }
        if (std::string(help).empty())
        {
            EXPECT_TRUE(helps.empty());
            continue;
        }
        ASSERT_EQ(helps.size(), 1U);
        EXPECT_EQ(helps[0], help);
        const auto& drawn = m_text.texts[2];
        EXPECT_EQ(drawn.text, help);
        EXPECT_EQ(drawn.y, 365 - 12);
        EXPECT_EQ(drawn.style.colors.text, 0xffffffffu);
        EXPECT_EQ(drawn.style.colors.background, 0u);
    }
}

// 0x5f4be3..0x5f4c55: reparar todo con el mouse encima y el clic pendiente (0x7d366d4) lo gasta y se dibuja apretado
// (0x11f) en celeste, con su ayuda; en un dibujo intermedio de MuRet el clic queda. Sin clic, solo la ayuda.
TEST_P(NpcWindowDrawingTest, RepairAllSpendsThePendingClick)
{
    Open();
    Runtime().panels.field78abb09 = 1;
    Runtime().movement.pendingClick = true;

    ASSERT_TRUE(DrawTheShop(375, 380, true).HasValue());
    EXPECT_TRUE(Runtime().movement.pendingClick);

    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheShop(375, 380).HasValue());

    EXPECT_FALSE(Runtime().movement.pendingClick);
    std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 6);
    ExpectQuadAt(quads[6 + GridCells], 0x11f, RepairAllX, FootY, 24.0f, 24.0f);
    EXPECT_EQ(quads[6 + GridCells].color, RepairAllColor);
    ExpectQuadAt(quads[7 + GridCells], 0x10f, RepairAllX - 60.0f, FootY + 30.0f, 113.0f, 18.0f);
    EXPECT_EQ(quads[7 + GridCells].color, White);
    ASSERT_EQ(m_text.texts.size(), 5U);
    EXPECT_EQ(m_text.texts[2].text, "Reparar todo");
    EXPECT_EQ(std::tuple(m_text.texts[2].x, m_text.texts[2].y), std::tuple(375, 353));

    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheShop(375, 380).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    EXPECT_EQ(quads.size(), 2 + GridCells + 5);
    EXPECT_EQ(m_text.texts[2].text, "Reparar todo");
}

// Sin el texto del título, corta con la dirección.
TEST_P(NpcWindowDrawingTest, AMissingTextStopsWithItsAddress)
{
    Open();
    m_drawnTexts = nullptr;

    const auto drawn = DrawTheShop(0, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5f4807"), std::string::npos) << drawn.Error();
}

INSTANTIATE_TEST_SUITE_P(Maps, NpcWindowDrawingTest, ::testing::Values(Lorencia, Devias, Noria));
} // namespace
