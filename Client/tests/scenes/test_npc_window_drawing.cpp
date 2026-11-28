#include "Npc/NpcWindowDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// The NPC windows' drawing (CL9 milestone 6, phase 3; 06 § Los dibujos de las ventanas de los NPC): the shop
// (0x5f4720) and the warehouse (0x5f4e30), in 640 × 480. The texts and prices are made up and the texts measure 6
// columns per letter. Each test runs in Lorencia, Devias and Noria.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Text::FontKind;
using Client::Ui::GameHudContext;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x6a3c19e5u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Noria = 3;
// 0x5f474c: the shop's corner.
constexpr float Left = 260.0f;
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
    return {{230, "Tienda"},       {1623, "Impuesto %d%%"}, {231, "Comprar"}, {232, "Vender"},  {233, "Reparar"},
            {237, "Reparar todo"}, {239, "Precio"},         {234, "Baul"},    {240, "abierto"}, {241, "cerrado"},
            {224, "Zen"},          {266, "Costo"},          {235, "Meter"},   {236, "Sacar"},   {242, "Trabar"}};
}

// A batch quad: the slot (NoTexture without a texture, Marker for a text's mark), the blend, the color and corners 0
// and 2 of the fan.
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

// The textured quads that are not text marks, in order.
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

// The corners in GL pixels (H − (y + h)).
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

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    GameHudContext Context(std::int32_t x, std::int32_t y, bool projecting = false, bool leftDown = false)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = m_world->Hero();
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
                              .texts = m_drawnTexts,
                              .mouse = {.x = x, .y = y},
                              .leftDown = leftDown};
    }

    Client::Core::Expected<void, std::string> DrawTheShop(std::int32_t x, std::int32_t y, bool projecting = false)
    {
        GameHudContext context = Context(x, y, projecting);
        Client::Hud::GamePanels panels(*m_world, m_random);
        return Client::Npc::DrawTheNpcShop(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> DrawTheWarehouse(std::int32_t x, std::int32_t y, bool leftDown = false)
    {
        GameHudContext context = Context(x, y, false, leftDown);
        Client::Hud::GamePanels panels(*m_world, m_random);
        return Client::Npc::DrawTheWarehouse(context, panels.Registry());
    }

    // The shop open, as the 30 with 0 (0x65ba67, the protected byte with no open accesses) and the 31 with the empty
    // grid (the type at 0xffff) leave it.
    void Open()
    {
        Runtime().panels.npcShop = 1;
        for (Client::Kernel::ItemState& cell : Runtime().npcWindows.grid)
            cell.id = Client::Kernel::EmptyOwnItemId;
    }

    // The warehouse open as the 31 with the empty grid leaves it, with the level and the stored zen.
    void OpenTheWarehouse(std::uint16_t level, std::uint32_t zen)
    {
        Runtime().panels.warehouse = 1;
        for (Client::Kernel::ItemState& cell : Runtime().npcWindows.grid)
            cell.id = Client::Kernel::EmptyOwnItemId;
        m_world->OwnCharacter().level = level;
        m_world->OwnCharacter().warehouseZen = zen;
    }

    std::vector<std::string> TextsDrawn() const
    {
        std::vector<std::string> drawn;
        for (const auto& text : m_text.texts)
            drawn.push_back(text.text);
        return drawn;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(ShopTexts());
    const Client::Tables::TextTable* m_drawnTexts = &m_texts;
};

// 0x5f7180: position, instructions and buttons kept on every map of the NPC window.
TEST_P(NpcWindowDrawingTest, ThePanel2000DrawsTheAcceptanceAndBothButtons)
{
    Runtime().panels.panel2000 = 1;
    std::map<std::size_t, std::string> texts;
    for (std::size_t index = 462; index < 470; ++index)
        texts[index] = "Instruccion";
    texts[447] = "Acepto";
    texts[228] = "Continuar";
    texts[229] = "Volver";
    const auto table = Client::Tests::MakeTextTable(texts);
    m_drawnTexts = &table;
    for (const bool accepted : {false, true})
    {
        Runtime().panels.field78abb08 = accepted ? 1 : 0;
        m_sprites.Begin(Screen640);
        m_text.texts.clear();
        GameHudContext context = Context(0, 0, false);
        Client::Hud::GamePanels panels(*m_world, m_random);
        const auto drawn = Client::Npc::DrawThePanel2000(context, panels.Registry());
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(Runtime().hud.inventoryX, 450);
        EXPECT_EQ(Runtime().hud.inventoryY, 0);
        const auto quads = TexturedQuadsOf(m_sprites);
        ASSERT_EQ(quads.size(), 6u);
        ExpectQuadAt(quads[0], 0x104, 450, 0, 190, 256);
        ExpectQuadAt(quads[2], 0x117, 450, 225, 190, 10);
        ExpectQuadAt(quads[3], accepted ? 0x123 : 0x122, 475, 240, 16, 16);
        ExpectQuadAt(quads[4], 0xf0, 485, 350, 120, 24);
        ExpectQuadAt(quads[5], 0xf0, 485, 320, 120, 24);
        EXPECT_EQ(quads[5].color, accepted ? White : (Color{0.5f, 0.5f, 0.5f, 1.0f}));
        ASSERT_EQ(m_text.texts.size(), 11u);
        EXPECT_EQ(m_text.texts[0].y, 50);
        EXPECT_EQ(m_text.texts[7].y, 190);
        EXPECT_EQ(m_text.texts[8].text, "Acepto");
        EXPECT_EQ(m_text.texts[8].x, 494);
        EXPECT_EQ(m_text.texts[8].y, 245);
        EXPECT_EQ(m_text.texts[8].style.colors.text, accepted ? 0xff0096d4u : 0xff67bfdfu);
        EXPECT_EQ(m_text.texts[10].text, "Continuar");
        EXPECT_EQ(m_random.State(), Seed);
    }
}

// 0x5f4736..0x5f473d: closed, only the query of the protected byte (1 rand()); the corner stays as it was.
TEST_P(NpcWindowDrawingTest, TheClosedShopOnlySpendsItsQuery)
{
    Runtime().ui.npcWindowX = 7;
    const CrtRandom expected = Advanced(m_random, 1);

    ASSERT_TRUE(DrawTheShop(0, 0).HasValue());

    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().ui.npcWindowX, 7);
}

// 0x5f4743..0x5f4a40: the corner (0x104, 0), the background, the 120 cells, the title and the tax (sprintf with
// 0x78aba30) in bold and in boxes of 120, and buy (released with 0x78abb27 at 0) and sell (pressed). Without
// 0x78abb09, nothing more. Only the query's rand().
TEST_P(NpcWindowDrawingTest, TheShopDrawsItsBackGridTitleTaxAndTwoButtons)
{
    Open();
    Runtime().npcWindows.taxRate = 3;
    Runtime().npcWindows.grid[8 + 3].id = 0x0c0e;
    const CrtRandom expected = Advanced(m_random, 1);

    const auto drawn = DrawTheShop(0, 0);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ui.npcWindowX, 0x104);
    EXPECT_EQ(Runtime().ui.npcWindowY, 0);
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

// 0x5f4882..0x5f49af: with the shop mode at 1 (sell), buy is drawn pressed and sell released.
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

// 0x5f4a46..0x5f4e13: with 0x78abb09, repair (0x11e released with 0x78abb0c at 0), repair all in light blue and the
// repair-all price (0x78ababc) over its background, in white: text 239 at x − 0x23 and the amount with commas at
// x + 10, in its amount's color.
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

// 0x5f4882 and 0x5f4a4c: repair with 0x78abb0c on, pressed; the price over 10 million, in red, and unsigned (the DIV
// of 0x5f4cc1 and the JCs).
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

// 0x5f48d3..0x5f494f: the mouse in [x, x + 24) × [y, y + 24) of a button draws its help with the normal font at
// (x, y − 12); outside, nothing.
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

// 0x5f4be3..0x5f4c55: repair all with the mouse over it and the click pending (0x7d366d4) spends it and is drawn
// pressed (0x11f) in light blue, with its help; in an intermediate MuRet drawing the click stays. Without a click,
// only the help.
TEST_P(NpcWindowDrawingTest, RepairAllSpendsThePendingClick)
{
    Open();
    Runtime().panels.field78abb09 = 1;
    Runtime().mouse.pendingClick = true;

    ASSERT_TRUE(DrawTheShop(375, 380, true).HasValue());
    EXPECT_TRUE(Runtime().mouse.pendingClick);

    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheShop(375, 380).HasValue());

    EXPECT_FALSE(Runtime().mouse.pendingClick);
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

TEST_P(NpcWindowDrawingTest, AMissingTextStopsWithItsAddress)
{
    Open();
    m_drawnTexts = nullptr;

    const auto drawn = DrawTheShop(0, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5f4807"), std::string::npos) << drawn.Error();
}

// 0x5f4e3f..0x5f4e48: the closed warehouse draws nothing and draws no rand() (0x40 is an unprotected byte).
TEST_P(NpcWindowDrawingTest, TheClosedWarehouseDrawsNothing)
{
    Runtime().ui.npcWindowX = 7;

    ASSERT_TRUE(DrawTheWarehouse(0, 0).HasValue());

    EXPECT_EQ(m_random.State(), Seed);
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().ui.npcWindowX, 7);
}

// 0x5f4e4e..0x5f58e6: the corner (0x104, 0), the background, the cells, the title "%s (%s)" with text 240, the stored
// zen over its background at (x + 50, y + 352.7f), text 266 in red with the fee (60² · 0.04f = 144, rounded to 140)
// and the three buttons at y + 390. The own record twice: 2 · 3848 rand().
TEST_P(NpcWindowDrawingTest, TheWarehouseDrawsItsZenItsFeeAndItsButtons)
{
    OpenTheWarehouse(60, 1234567);
    const CrtRandom expected = Advanced(m_random, 2 * Client::Models::OwnRecordCloseRandoms);

    const auto drawn = DrawTheWarehouse(0, 0);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ownRecordDepth, 0);
    EXPECT_EQ(Runtime().ui.npcWindowX, 0x104);
    EXPECT_EQ(Runtime().ui.npcWindowY, 0);
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 4);
    ExpectQuadAt(quads[0], 0x104, Left, 0.0f, 190.0f, 256.0f);
    ExpectQuadAt(quads[2], 0x115, Left + 15.0f, 50.0f, 20.0f, 20.0f);
    ExpectQuadAt(quads[2 + GridCells], 0x10f, Left + 50.0f, 352.7f, 113.0f, 18.0f);
    ExpectQuadAt(quads[3 + GridCells], 0x110, Left + 0x1a, 390.0f, 24.0f, 24.0f);
    ExpectQuadAt(quads[4 + GridCells], 0x110, Left + 0x4c, 390.0f, 24.0f, 24.0f);
    ExpectQuadAt(quads[5 + GridCells], 0x126, Left + 0x7e, 390.0f, 24.0f, 24.0f);
    EXPECT_EQ(quads[5 + GridCells].color, White);

    EXPECT_EQ(TextsDrawn(), (std::vector<std::string>{"Baul (abierto)", "Zen", "1,234,567", "Costo", "140"}));
    const auto& texts = m_text.texts;
    EXPECT_EQ(std::tuple(texts[0].x, texts[0].y), std::tuple(0x104 + 0x23, 0xc));
    EXPECT_TRUE(texts[0].windowed);
    EXPECT_EQ(texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(std::tuple(texts[1].x, texts[1].y), std::tuple(310 - 0x1e, 355));
    EXPECT_EQ(texts[1].style.colors.text, 0xff96dcffu);
    EXPECT_EQ(texts[1].style.colors.background, 0xff282828u);
    EXPECT_EQ(std::tuple(texts[2].x, texts[2].y), std::tuple(320, 355));
    EXPECT_EQ(texts[2].style.colors.text, 0xff0096ffu);
    EXPECT_EQ(std::tuple(texts[3].x, texts[3].y), std::tuple(310, 374));
    EXPECT_EQ(texts[3].style.colors.text, 0xff0000ffu);
    EXPECT_EQ(texts[3].style.colors.background, 0xff282828u);
    EXPECT_EQ(texts[3].style.font, FontKind::Bold);
    EXPECT_EQ(std::tuple(texts[4].x, texts[4].y), std::tuple(310 + 0x46, 374));
    EXPECT_EQ(texts[4].style.colors.text, 0xff96dcffu);
}

// 0x5f5457..0x5f55fc: _ftol(level² · 0.04f) plus 2 · level when locked; below 1 is 1, from 1000 it drops to the
// hundred and from 100 to the ten.
TEST_P(NpcWindowDrawingTest, TheWarehouseFeeRoundsByItsSize)
{
    for (const auto& [level, locked, fee] :
         {std::tuple{std::uint16_t{3}, false, "1"}, std::tuple{std::uint16_t{3}, true, "6"},
          std::tuple{std::uint16_t{30}, false, "36"}, std::tuple{std::uint16_t{60}, true, "260"},
          std::tuple{std::uint16_t{170}, true, "1,400"}, std::tuple{std::uint16_t{200}, false, "1,600"},
          std::tuple{std::uint16_t{400}, true, "7,200"}, std::tuple{std::uint16_t{46341}, false, "1"},
          std::tuple{std::uint16_t{65535}, true, "125,800"}})
    {
        SCOPED_TRACE(testing::Message() << level << (locked ? " trabado" : ""));
        OpenTheWarehouse(level, 0);
        Runtime().npcWindows.warehouseLocked = locked ? 1 : 0;
        m_text.texts.clear();
        m_sprites.Begin(Screen640);

        ASSERT_TRUE(DrawTheWarehouse(0, 0).HasValue());

        ASSERT_EQ(m_text.texts.size(), 5U);
        EXPECT_EQ(m_text.texts[4].text, fee);
    }
}

// 0x5f4eec..0x5f4f6b and 0x5f574d..0x5f575a: with the lock (0x6c932f), the title says text 241 in yellow and the lock
// is lock_03 (0x128).
TEST_P(NpcWindowDrawingTest, TheLockedWarehouseTintsItsTitleAndShowsTheLock)
{
    OpenTheWarehouse(60, 5);
    Runtime().npcWindows.warehouseLocked = 1;

    ASSERT_TRUE(DrawTheWarehouse(0, 0).HasValue());

    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].text, "Baul (cerrado)");
    const std::vector<DrawnQuad> all = QuadsOf(m_sprites);
    const auto title = std::ranges::find_if(all, [](const DrawnQuad& quad) { return quad.slot == Marker; });
    ASSERT_NE(title, all.end());
    EXPECT_EQ(title->color, (Color{1.0f, 1.0f, 0.0f, 1.0f}));
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 4);
    ExpectQuadAt(quads[5 + GridCells], 0x128, Left + 0x7e, 390.0f, 24.0f, 24.0f);
    EXPECT_EQ(quads[2 + GridCells].color, White);
}

// 0x5f57ab..0x5f5887: the mouse over a button draws its help at (x, y − 13), and with the left button down (0x7d36874)
// the button pressed first; the lock is also pressed with the keypad open (0x78abb20).
TEST_P(NpcWindowDrawingTest, TheWarehouseButtonsPressAndShowTheirHelp)
{
    OpenTheWarehouse(60, 5);

    ASSERT_TRUE(DrawTheWarehouse(260 + 0x4c, 390, true).HasValue());
    std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 5);
    ExpectQuadAt(quads[5 + GridCells], 0x111, Left + 0x4c, 390.0f, 24.0f, 24.0f);
    ExpectQuadAt(quads[6 + GridCells], 0x126, Left + 0x7e, 390.0f, 24.0f, 24.0f);
    ASSERT_EQ(m_text.texts.size(), 6U);
    EXPECT_EQ(m_text.texts[5].text, "Sacar");
    EXPECT_EQ(std::tuple(m_text.texts[5].x, m_text.texts[5].y), std::tuple(260 + 0x4c, 390 - 13));
    EXPECT_EQ(m_text.texts[5].style.font, FontKind::Normal);

    // Without the left button, only the help; outside (x + 24), nothing.
    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheWarehouse(260 + 0x1a + 23, 413).HasValue());
    EXPECT_EQ(TexturedQuadsOf(m_sprites).size(), 2 + GridCells + 4);
    ASSERT_EQ(m_text.texts.size(), 6U);
    EXPECT_EQ(m_text.texts[5].text, "Meter");
    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheWarehouse(260 + 0x1a + 24, 413, true).HasValue());
    EXPECT_EQ(TexturedQuadsOf(m_sprites).size(), 2 + GridCells + 4);
    EXPECT_EQ(m_text.texts.size(), 5U);

    // With the keypad open and the lock, lock_04 (0x129) over lock_03.
    Runtime().npcWindows.warehouseLocked = 1;
    Runtime().ui.field78abb20 = 1;
    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheWarehouse(0, 0).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 2 + GridCells + 5);
    ExpectQuadAt(quads[5 + GridCells], 0x128, Left + 0x7e, 390.0f, 24.0f, 24.0f);
    ExpectQuadAt(quads[6 + GridCells], 0x129, Left + 0x7e, 390.0f, 24.0f, 24.0f);
}

INSTANTIATE_TEST_SUITE_P(Maps, NpcWindowDrawingTest, ::testing::Values(Lorencia, Devias, Noria));
} // namespace
