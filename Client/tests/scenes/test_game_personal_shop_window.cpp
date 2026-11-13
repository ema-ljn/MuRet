#include "Scenes/GameInventoryHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"
#include "World/ItemState.h"

#include "support/ItemFiles.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// La ventana de las tiendas personales (0x5f3030) y su grilla en las de los ítems y las cifras (0x5f7821 y 0x5f7af9;
// CL9 hito 7, fase 6a) en Lorencia (0) y Devias (2), contra el ASM: la propia cerrada y abierta, la de otro y sin modo.
// Los títulos, los textos y los ítems son inventados.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x5f30;
constexpr Client::App::PixelSize Screen640{640, 480};
// 0x5f306e: la esquina, (0x104, 0).
constexpr int Left = 0x104;
constexpr float LeftF = 260.0f;
// Un bastón de 1 × 3 (0x0a05): su modelo (0xc17) saca dos rand() sin el pase 3D, como en el inventario.
constexpr std::uint16_t StaffType = 0x0a05;
constexpr int StaffRandoms = 2;
// La poción (0x1c00) con 2 o más muestra la durabilidad.
constexpr std::uint16_t PotionType = 0x1c00;
constexpr std::uint16_t DigitSlot = 1;
// El fondo de la ventana (0x5ee220), Item_Money, las casillas vacía y llena y text_button.
constexpr std::uint16_t BackTop = 0x104;
constexpr std::uint16_t BackBottom = 0x105;
constexpr std::uint16_t MoneyBox = 0x10f;
constexpr std::uint16_t EmptyBox = 0x115;
constexpr std::uint16_t FullBox = 0x116;
constexpr std::uint16_t TextButton = 0x659;

std::uint32_t Drawn(CrtRandom from, const CrtRandom& to)
{
    for (std::uint32_t count = 0; count <= 100'000; ++count)
    {
        if (from.State() == to.State())
            return count;
        static_cast<void>(from.Next());
    }
    return 0xffffffffu;
}

std::vector<std::uint16_t> SlotsOf(const SpriteBatch& sprites)
{
    std::vector<std::uint16_t> slots;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            slots.push_back(command.texture->value);
    }
    return slots;
}

struct Corners
{
    Client::Render::SpriteVertex first;
    Client::Render::SpriteVertex third;
};

// El quad `index` del slot, o el de los sin textura (0x621aa0) con `slot` vacío.
std::optional<Corners> NthQuadOf(const SpriteBatch& sprites, std::optional<std::uint16_t> slot, std::size_t index = 0)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        const bool match = slot ? command.texture && command.texture->value == *slot : !command.texture;
        if (!match)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            if (seen++ == index)
                return Corners{vertices[first], vertices[first + 2]};
        }
    }
    return std::nullopt;
}

void ExpectQuad(const SpriteBatch& sprites, std::optional<std::uint16_t> slot, std::size_t index, float x, float y,
                float width, float height)
{
    SCOPED_TRACE(slot.value_or(0));
    SCOPED_TRACE(index);
    const auto quad = NthQuadOf(sprites, slot, index);
    ASSERT_TRUE(quad.has_value());
    constexpr float Tolerance = 1e-3f;
    EXPECT_NEAR(quad->first.x, x, Tolerance);
    EXPECT_NEAR(quad->first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad->third.x, x + width, Tolerance);
    EXPECT_NEAR(quad->third.y, 480.0f - y - height, Tolerance);
}

// Las mezclas de las marcas de los textos, en su orden.
std::vector<Client::Render::BlendMode> TextBlendsOf(const SpriteBatch& sprites)
{
    std::vector<Client::Render::BlendMode> blends;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            blends.push_back(command.blend);
    }
    return blends;
}

struct Expected
{
    std::string text;
    int x;
    int y;
    std::uint32_t color;
    std::uint32_t background;
    // Las columnas de la caja de 0x467060, o 0 sin caja.
    int columns;
};

// sen(0) = 0: el alfa del aviso que late, 275 recortado a 255.
constexpr std::uint32_t Pulse = 0xff0000d2u;
constexpr std::uint32_t Lines = 0xff96dcffu;
constexpr std::uint32_t LinesBack = 0xff141414u;

// El mundo del juego, en el heap.
struct ShopWorld
{
    Client::Scenes::WorldState world;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemTableOf({{StaffType, 1, 3, 0}, {PotionType, 1, 1, 0}});
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{370, "ojo"},
                                                                        {560, "cerrada"},
                                                                        {1102, "Tienda"},
                                                                        {1103, "abierta"},
                                                                        {1105, "sin titulo"},
                                                                        {1106, "aplicar"},
                                                                        {1107, "abrir"},
                                                                        {1108, "cerrar"},
                                                                        {1109, "propia uno"},
                                                                        {1110, "ajena uno"},
                                                                        {1111, "dos"},
                                                                        {1112, "tres"},
                                                                        {1113, "propia cuatro"},
                                                                        {1114, "ajena cuatro"},
                                                                        {1115, "propia cinco"},
                                                                        {1116, "ajena cinco"},
                                                                        {1134, "late uno"},
                                                                        {1135, "late dos"}});
};

class GamePersonalShopWindowTest : public ::testing::TestWithParam<std::uint8_t>
{
protected:
    void SetUp() override
    {
        shop = std::make_unique<ShopWorld>();
        Runtime().map = GetParam();
        shop->world.Inventory().ClearForInventoryReply();
        sprites.Begin(Screen640);
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return shop->world.Runtime();
    }

    Client::Scenes::PersonalShopState& Shops()
    {
        return Runtime().social.personalShop;
    }

    // Lo que deja Open(0x800) u Open(0x20000): el byte de las dos (0x7d5370f) y 0x6c931c en el modo.
    void OpenTheShop(std::int32_t mode)
    {
        Runtime().panels.personalShop = 1;
        Shops().mode = mode;
        Shops().pricesBuilt = true;
    }

    GameHudContext Context()
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = shop->world.Actors()[runtime.hero];
        return GameHudContext{.world = shop->world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .sprites = sprites,
                              .text = text,
                              .texts = withTexts ? &shop->texts : nullptr,
                              .items = &shop->items,
                              .worldClock = worldClock,
                              .mouse = mouse};
    }

    Client::Core::Expected<void, std::string> Window()
    {
        Client::Scenes::GamePanels panels(shop->world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawThePersonalShopWindow(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridItems()
    {
        Client::Scenes::GamePanels panels(shop->world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridItems(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridNumbers()
    {
        Client::Scenes::GamePanels panels(shop->world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridNumbers(context, panels.Registry());
    }

    void ExpectTexts(const std::vector<Expected>& expected)
    {
        ASSERT_EQ(text.texts.size(), expected.size());
        for (std::size_t at = 0; at < expected.size(); ++at)
        {
            SCOPED_TRACE(at);
            EXPECT_EQ(text.texts[at].text, expected[at].text);
            EXPECT_EQ(text.texts[at].x, expected[at].x);
            EXPECT_EQ(text.texts[at].y, expected[at].y);
            EXPECT_EQ(text.texts[at].style.font, FontKind::Bold);
            EXPECT_EQ(text.texts[at].style.colors.text, expected[at].color);
            EXPECT_EQ(text.texts[at].style.colors.background, expected[at].background);
            EXPECT_EQ(text.texts[at].windowed, expected[at].columns != 0);
            if (expected[at].columns != 0)
            {
                EXPECT_EQ(text.texts[at].columns, expected[at].columns);
            }
        }
    }

    std::unique_ptr<ShopWorld> shop;
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::UI::Point mouse{};
    bool withTexts = true;
};

// Las 32 casillas de la grilla, todas vacías.
std::vector<std::uint16_t> EmptyGrid()
{
    return std::vector<std::uint16_t>(32, EmptyBox);
}

// Con las dos cerradas, 0x5f3030 solo consulta el byte (sin rand()) y no pone la esquina.
TEST_P(GamePersonalShopWindowTest, ClosedItDrawsNothing)
{
    ASSERT_TRUE(Window().HasValue());
    EXPECT_TRUE(SlotsOf(sprites).empty());
    EXPECT_TRUE(text.texts.empty());
    EXPECT_EQ(Shops().windowX, 0);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// La propia cerrada y sin título: la esquina, el fondo, la franja roja sin textura con el 560 en blanco, Item_Money
// con el 1105, la grilla, los dos botones (el de cerrar en gris), el aviso que late, los cinco renglones de la propia y
// los dos que laten. Todo en la negrita, sin rand().
TEST_P(GamePersonalShopWindowTest, TheClosedOwnShopDrawsItsStatusAndItsButtons)
{
    OpenTheShop(Client::Scenes::OwnShopView);
    // 0x5f7440 deja la mezcla apagada antes de las ventanas.
    sprites.SetBlend(Client::Render::BlendMode::Opaque);
    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(Shops().windowX, Left);
    EXPECT_EQ(Shops().windowY, 0);
    std::vector<std::uint16_t> expected{BackTop, BackBottom, MoneyBox};
    for (const std::uint16_t box : EmptyGrid())
        expected.push_back(box);
    expected.push_back(TextButton);
    expected.push_back(TextButton);
    EXPECT_EQ(SlotsOf(sprites), expected);
    // 0x5f3140..0x5f318a: 150 de ancho y el paso de la letra (12 en 640) más 2 de alto, en (0,6; 0; 0; 0,8).
    ExpectQuad(sprites, std::nullopt, 0, LeftF + 20.0f, 48.0f, 150.0f, 14.0f);
    EXPECT_EQ(NthQuadOf(sprites, std::nullopt)->first.color, (Color{0.6f, 0.0f, 0.0f, 0.8f}));
    ExpectQuad(sprites, MoneyBox, 0, LeftF + 20.0f, 65.0f, 150.0f, 24.0f);
    ExpectQuad(sprites, EmptyBox, 0, LeftF + 15.0f, 110.0f, 20.0f, 20.0f);
    ExpectQuad(sprites, EmptyBox, 31, LeftF + 15.0f + 140.0f, 170.0f, 20.0f, 20.0f);
    ExpectQuad(sprites, TextButton, 0, LeftF + 30.0f, 396.0f, 56.0f, 24.0f);
    ExpectQuad(sprites, TextButton, 1, LeftF + 105.0f, 396.0f, 56.0f, 24.0f);
    EXPECT_EQ(NthQuadOf(sprites, TextButton)->first.color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));

    ExpectTexts({{"Tienda", Left + 0x23, 0xc, 0xffdcdcdcu, 0xff141414u, 120},
                 {"cerrada", Left + 0x14, 0x32, 0xffffffffu, 0, 150},
                 {"sin titulo", Left + 0x14, 0x48, 0xff0de90du, 0, 150},
                 {"abrir", Left + 0x20, 0x193, 0xff19ccffu, 0, 52},
                 {"cerrar", Left + 0x6b, 0x193, 0xffaaaaaau, 0, 52},
                 {"ojo", Left + 0x1e, 0xe6, Pulse, LinesBack, 0},
                 {"propia uno", Left + 0x1e, 0xf5, Lines, LinesBack, 0},
                 {"dos", Left + 0x1e, 0x104, Lines, LinesBack, 0},
                 {"tres", Left + 0x1e, 0x113, Lines, LinesBack, 0},
                 {"propia cuatro", Left + 0x1e, 0x122, Lines, LinesBack, 0},
                 {"propia cinco", Left + 0x1e, 0x131, Lines, LinesBack, 0},
                 {"late uno", Left + 0x1e, 0x14a, Pulse, LinesBack, 0},
                 {"late dos", Left + 0x1e, 0x159, Pulse, LinesBack, 0}});
    // El título sin mezcla (la de 0x5f7440), los demás en la normal; termina sin mezcla.
    std::vector<Client::Render::BlendMode> blends(13, Client::Render::BlendMode::Alpha);
    blends[0] = Client::Render::BlendMode::Opaque;
    EXPECT_EQ(TextBlendsOf(sprites), blends);
    EXPECT_EQ(sprites.Blend(), Client::Render::BlendMode::Opaque);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// La propia abierta con título: la franja verde con el 1103 en 0xbe000000, el título en Item_Money, el botón de
// aplicar (1106) y el de cerrar en el color de los botones. Con el reloj, el aviso y los dos últimos laten.
TEST_P(GamePersonalShopWindowTest, TheOpenOwnShopShowsItsTitle)
{
    OpenTheShop(Client::Scenes::OwnShopView);
    Shops().ownOpen = 1;
    Shops().ownTitle = "Ofertas del dia";
    worldClock = 1000.0f;
    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(NthQuadOf(sprites, std::nullopt)->first.color, (Color{0.1f, 0.8f, 0.1f, 0.6f}));
    const double alpha = std::sin(1000.0 * static_cast<double>(0.005f)) * 200.0 + 275.0;
    const auto pulse = (static_cast<std::uint32_t>(static_cast<std::int32_t>(alpha)) << 24u) + 0xd2u;
    ExpectTexts({{"Tienda", Left + 0x23, 0xc, 0xffdcdcdcu, 0xff141414u, 120},
                 {"abierta", Left + 0x14, 0x32, 0xbe000000u, 0, 150},
                 {"Ofertas del dia", Left + 0x14, 0x48, 0xff0de90du, 0, 150},
                 {"aplicar", Left + 0x20, 0x193, 0xff19ccffu, 0, 52},
                 {"cerrar", Left + 0x6b, 0x193, 0xff19ccffu, 0, 52},
                 {"ojo", Left + 0x1e, 0xe6, pulse, LinesBack, 0},
                 {"propia uno", Left + 0x1e, 0xf5, Lines, LinesBack, 0},
                 {"dos", Left + 0x1e, 0x104, Lines, LinesBack, 0},
                 {"tres", Left + 0x1e, 0x113, Lines, LinesBack, 0},
                 {"propia cuatro", Left + 0x1e, 0x122, Lines, LinesBack, 0},
                 {"propia cinco", Left + 0x1e, 0x131, Lines, LinesBack, 0},
                 {"late uno", Left + 0x1e, 0x14a, pulse, LinesBack, 0},
                 {"late dos", Left + 0x1e, 0x159, pulse, LinesBack, 0}});
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// La de otro: sin la franja ni los botones; el título de la tienda que se mira aunque esté vacío (0x5f32e5) y los
// renglones de la de otro.
TEST_P(GamePersonalShopWindowTest, TheOtherShopShowsItsNameAndItsLines)
{
    OpenTheShop(Client::Scenes::OtherShopView);
    Shops().windowX = 7;
    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(Shops().windowX, Left);
    std::vector<std::uint16_t> expected{BackTop, BackBottom, MoneyBox};
    for (const std::uint16_t box : EmptyGrid())
        expected.push_back(box);
    EXPECT_EQ(SlotsOf(sprites), expected);
    EXPECT_FALSE(NthQuadOf(sprites, std::nullopt).has_value());
    ExpectTexts({{"Tienda", Left + 0x23, 0xc, 0xffdcdcdcu, 0xff141414u, 120},
                 {"", Left + 0x14, 0x48, 0xff0de90du, 0, 150},
                 {"ojo", Left + 0x1e, 0xe6, Pulse, LinesBack, 0},
                 {"ajena uno", Left + 0x1e, 0xf5, Lines, LinesBack, 0},
                 {"dos", Left + 0x1e, 0x104, Lines, LinesBack, 0},
                 {"tres", Left + 0x1e, 0x113, Lines, LinesBack, 0},
                 {"ajena cuatro", Left + 0x1e, 0x122, Lines, LinesBack, 0},
                 {"ajena cinco", Left + 0x1e, 0x131, Lines, LinesBack, 0},
                 {"late uno", Left + 0x1e, 0x14a, Pulse, LinesBack, 0},
                 {"late dos", Left + 0x1e, 0x159, Pulse, LinesBack, 0}});
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// Con el byte abierto y 0x6c931c en −1 (0x5f36bb): el título del cartel o el 1105 y solo el aviso.
TEST_P(GamePersonalShopWindowTest, WithoutAModeOnlyTheWarningIsWritten)
{
    OpenTheShop(Client::Scenes::NoShopView);
    ASSERT_TRUE(Window().HasValue());

    ExpectTexts({{"Tienda", Left + 0x23, 0xc, 0xffdcdcdcu, 0xff141414u, 120},
                 {"sin titulo", Left + 0x14, 0x48, 0xff0de90du, 0, 150},
                 {"ojo", Left + 0x1e, 0xe6, Pulse, LinesBack, 0}});
}

// Sin Text.bmd, el título corta con su dirección.
TEST_P(GamePersonalShopWindowTest, WithoutTheTextsItCuts)
{
    OpenTheShop(Client::Scenes::OwnShopView);
    withTexts = false;
    const auto drawn = Window();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5f30f9"), std::string::npos) << drawn.Error();
}

// 0x5f77f6..0x5f7821: el bastón de la grilla en (x + 15 + 20, y + 110) saca los rand() de su modelo, después de los
// del inventario abierto; 0x5f7ace..0x5f7af9: la poción con 5 muestra la cifra en su casilla.
TEST_P(GamePersonalShopWindowTest, TheGridDrawsItsItemsAndItsNumbers)
{
    OpenTheShop(Client::Scenes::OwnShopView);
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Window().HasValue());

    CrtRandom before = random;
    ASSERT_TRUE(GridItems().HasValue());
    const std::uint32_t emptyDraws = Drawn(before, random);
    ASSERT_NE(emptyDraws, 0xffffffffu);

    auto& cells = shop->world.Inventory().secondCells;
    for (std::size_t row = 0; row < 3; ++row)
    {
        ItemState& item = cells[1 + row * 8];
        item.id = StaffType;
        item.gridColumn = 1;
        item.gridRow = 0;
        item.occupied = row == 0 ? 1U : 0U;
    }
    before = random;
    ASSERT_TRUE(GridItems().HasValue());
    EXPECT_EQ(Drawn(before, random), emptyDraws + StaffRandoms);

    cells[3].id = PotionType;
    cells[3].durability = 5;
    cells[3].occupied = 1;
    cells[1].id = 0xffff;
    cells[1 + 8].id = 0xffff;
    cells[1 + 16].id = 0xffff;
    sprites.Begin(Screen640);
    ASSERT_TRUE(GridNumbers().HasValue());
    EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{DigitSlot}));
    const auto five = NthQuadOf(sprites, DigitSlot);
    ASSERT_TRUE(five.has_value());
    EXPECT_FLOAT_EQ(five->first.x, LeftF + 15.0f + 60.0f + 13.0f - 4.5f);
    EXPECT_FLOAT_EQ(five->first.y, 480.0f - 110.0f);
}

INSTANTIATE_TEST_SUITE_P(Maps, GamePersonalShopWindowTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
} // namespace
