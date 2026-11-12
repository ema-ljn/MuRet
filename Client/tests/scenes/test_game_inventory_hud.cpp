#include "Scenes/GameInventoryHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameData.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El inventario en el HUD (CL8 hito 4): la ventana de 0x5f2340, los rand() de los ítems de la UI (0x5e1480 sin el
// modelo, P-C70), las cifras de 0x5f7840 y el ítem del cursor de 0x5f7b10. Los ítems y el zen son inventados.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Models::SmallLogicalCloseRandoms;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int Own = OwnRecordCloseRandoms;
// La ventana: los cierres de los lugares (0x5e2460), del zen y del nivel, la consulta del comercio del botón de
// cerrar y las del comercio y la tienda de NPC de reparar y de la tienda personal.
constexpr int WindowDraws = 3 * Own + 5;
// El HUD sin ventanas, como en GameHudTest.
constexpr int HudBaseDraws = 19 * Own + 2 * SmallLogicalCloseRandoms + 9;
constexpr std::uint16_t DigitSlot = 1;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
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

// El vértice 0 (arriba a la izquierda, con la y de GL hacia arriba) y el 2 del quad `index` con la textura `slot`.
struct Corners
{
    Client::Render::SpriteVertex first;
    Client::Render::SpriteVertex third;
    Color color;
};

std::optional<Corners> NthQuadOf(const SpriteBatch& sprites, std::uint16_t slot, std::size_t index = 0)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture->value != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            if (seen++ == index)
                return Corners{vertices[first], vertices[first + 2], vertices[first].color};
        }
    }
    return std::nullopt;
}

class GameInventoryHudTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto loaded = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // Como lo deja la entrada al mundo y un F3 10 sin ítems.
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        own.ClearEquipmentForEntry();
        own.life = 100;
        own.maxLife = 100;
        own.mana = 100;
        own.maxMana = 100;
        world.Inventory().ClearForInventoryReply();
        world.Runtime().panels.inventory = 1;
        sprites.Begin(Screen640);
    }

    GameHudContext Context()
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
                              .sprites = sprites,
                              .text = text,
                              .texts = &*data.text,
                              .items = &data.items->table,
                              .skills = &*data.skills,
                              .worldClock = worldClock,
                              .mouse = mouse};
    }

    Client::Core::Expected<void, std::string> Window()
    {
        Client::Scenes::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheInventory(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridItems()
    {
        Client::Scenes::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridItems(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridNumbers()
    {
        Client::Scenes::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridNumbers(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> UiItem(std::int32_t type, std::uint32_t level, std::uint8_t excellent,
                                                     std::uint8_t set)
    {
        GameHudContext context = Context();
        return Client::Scenes::DrawUiItem(context, type, level, excellent, set);
    }

    // Un ítem de `width` × `height` desde la casilla `origin`, como lo deja 0x5bc740: el tipo en todas y +0x3c en 1
    // solo en la de arriba a la izquierda.
    void PutItem(std::size_t origin, std::uint16_t type, std::size_t width = 1, std::size_t height = 1,
                 std::uint8_t durability = 0, std::uint32_t level = 0)
    {
        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                ItemState& item = world.Inventory().cells[origin + y * Client::Scenes::InventoryColumns + x];
                item.id = type;
                item.durability = durability;
                item.scalar1Low = level;
                item.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    std::string TextAt(std::size_t index) const
    {
        return std::string(Client::UI::TextAt(&*data.text, index).value_or("?"));
    }

    WorldState world;
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::UI::Point mouse{};
    Client::Scenes::GameData data;
};

// 0x5f2340 sin personaje ni party: la ventana en (0x1c2, 0), los doce fondos del equipo (el casco con la clase 0),
// las 64 casillas vacías, el fondo del zen, cerrar y el cartel del set; reparar y la tienda personal no, con el
// nivel en 0. Lo mismo en Lorencia y en Devias: la ventana no mira el mapa.
TEST_F(GameInventoryHudTest, TheWindowDrawsItsQuadsAndSpendsItsRandomsInLorenciaAndDevias)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        sprites.Begin(Screen640);
        text.texts.clear();
        random = CrtRandom{Seed};
        const CrtRandom expected = Advanced(random, WindowDraws);

        const auto drawn = Window();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        std::vector<std::uint16_t> slots{0x104, 0x105, 0x113, 0x112, 0x107, 0x108, 0x109,
                                         0x10a, 0x114, 0x10b, 0x10c, 0x10d, 0x10e, 0x10e};
        slots.insert(slots.end(), 64, std::uint16_t{0x115});
        slots.insert(slots.end(), {0x10f, 0x118, 0x652});
        EXPECT_EQ(SlotsOf(sprites), slots);
        EXPECT_EQ(world.Runtime().hud.inventoryX, 0x1c2);
        EXPECT_EQ(world.Runtime().hud.inventoryY, 0);
    }
    // El fondo de arriba: 190 × 256 desde (450, 0); la primera casilla en (465, 200); el zen en (500; 367,7).
    const auto back = NthQuadOf(sprites, 0x104);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->first.x, 450.0f);
    EXPECT_FLOAT_EQ(back->first.y, 480.0f);
    EXPECT_FLOAT_EQ(back->third.x, 640.0f);
    EXPECT_FLOAT_EQ(back->third.y, 224.0f);
    const auto box = NthQuadOf(sprites, 0x115);
    ASSERT_TRUE(box.has_value());
    EXPECT_FLOAT_EQ(box->first.x, 465.0f);
    EXPECT_FLOAT_EQ(box->first.y, 280.0f);
    const auto zen = NthQuadOf(sprites, 0x10f);
    ASSERT_TRUE(zen.has_value());
    EXPECT_FLOAT_EQ(zen->first.x, 500.0f);
    EXPECT_NEAR(zen->first.y, 480.0f - 367.7f, 1e-4f);
    // El título (Text 223) centrado en 120 desde 485, en negrita y en blanco sobre gris; "Zen" (224) en (470, 370)
    // y la cifra en (510, 370), con su color; el cartel del set (989) dos veces, centrado en 546 y 545.
    ASSERT_EQ(text.texts.size(), 5u);
    const std::string title = TextAt(223);
    EXPECT_EQ(text.texts[0].text, title);
    EXPECT_EQ(text.texts[0].x, 485 + (120 - static_cast<int>(title.size()) * 6) / 2);
    EXPECT_EQ(text.texts[0].y, 12);
    EXPECT_EQ(text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(text.texts[0].style.colors.background, 0xff141414u);
    EXPECT_EQ(text.texts[1].text, TextAt(224));
    EXPECT_EQ(text.texts[1].x, 470);
    EXPECT_EQ(text.texts[1].y, 370);
    EXPECT_EQ(text.texts[1].style.colors.text, 0xff96dcffu);
    EXPECT_EQ(text.texts[1].style.colors.background, 0xff282828u);
    EXPECT_EQ(text.texts[2].text, "0");
    EXPECT_EQ(text.texts[2].x, 510);
    const std::string set = "[" + TextAt(989) + "]";
    EXPECT_EQ(text.texts[3].text, set);
    EXPECT_EQ(text.texts[3].x, 0x1c2 + 0x60 - static_cast<int>(set.size()) * 6 / 2);
    EXPECT_EQ(text.texts[3].y, 25);
    EXPECT_EQ(text.texts[3].style.colors.text, 0xff000000u);
    EXPECT_EQ(text.texts[4].x, 0x1c2 + 0x5f - static_cast<int>(set.size()) * 6 / 2);
    EXPECT_EQ(text.texts[4].y, 24);
    EXPECT_EQ(text.texts[4].style.colors.text, 0xffffffffu);
}

// Con el personaje o la party abiertos, la ventana va en x = 0x104.
TEST_F(GameInventoryHudTest, TheWindowMovesLeftWithThePartyOpen)
{
    world.Runtime().panels.party = 1;

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(world.Runtime().hud.inventoryX, 0x104);
    const auto back = NthQuadOf(sprites, 0x104);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->first.x, 260.0f);
}

// 0x5f26a7..0x5f27ef: el zen con comas y su color por cuánto es.
TEST_F(GameInventoryHudTest, TheZenGetsItsCommasAndItsColor)
{
    struct Case
    {
        std::uint32_t zen;
        std::string_view shown;
        std::uint32_t color;
    };
    const std::array<Case, 6> cases{{
        {999, "999", 0xff96dcffu},
        {1000, "1,000", 0xff96dcffu},
        {123456, "123,456", 0xff18c900u},
        {1234567, "1,234,567", 0xff0096ffu},
        {12345678, "12,345,678", 0xff0000ffu},
        {1234567890, "1,234,567,890", 0xff0000ffu},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.zen);
        world.OwnCharacter().zen = probe.zen;
        text.texts.clear();
        ASSERT_TRUE(Window().HasValue());
        ASSERT_GE(text.texts.size(), 3u);
        EXPECT_EQ(text.texts[2].text, probe.shown);
        EXPECT_EQ(text.texts[2].style.colors.text, probe.color);
    }
}

// Reparar desde el nivel 50 (con 0x78abb0c, la otra textura) y la tienda personal desde el 6; el casco no tiene fondo
// con la clase 3.
TEST_F(GameInventoryHudTest, TheFootButtonsFollowTheLevel)
{
    struct Case
    {
        std::uint16_t level;
        std::int32_t repairMode;
        std::vector<std::uint16_t> buttons;
    };
    const std::array<Case, 4> cases{{
        {5, 0, {0x118, 0x652}},
        {6, 0, {0x118, 0x657, 0x652}},
        {50, 0, {0x118, 0x11e, 0x657, 0x652}},
        {50, 1, {0x118, 0x11f, 0x657, 0x652}},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.level);
        world.OwnCharacter().level = probe.level;
        world.Runtime().panels.repairMode = probe.repairMode;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Window().HasValue());
        const std::vector<std::uint16_t> slots = SlotsOf(sprites);
        const std::vector<std::uint16_t> tail(slots.begin() + 14 + 64 + 1, slots.end());
        EXPECT_EQ(tail, probe.buttons);
    }
    world.OwnCharacter().characterClass = 3;
    sprites.Begin(Screen640);
    ASSERT_TRUE(Window().HasValue());
    EXPECT_FALSE(NthQuadOf(sprites, 0x107).has_value());
}

// Con el mouse sobre cerrar, su ayuda (Text 225) en (_ftol(x + 20), _ftol(395) − 13).
TEST_F(GameInventoryHudTest, TheCloseButtonShowsItsHelp)
{
    mouse = {0x1c2 + 20 + 5, 395 + 5};

    ASSERT_TRUE(Window().HasValue());

    ASSERT_EQ(text.texts.size(), 6u);
    EXPECT_EQ(text.texts[3].text, TextAt(225));
    EXPECT_EQ(text.texts[3].x, 0x1c2 + 20);
    EXPECT_EQ(text.texts[3].y, 395 - 13);
    EXPECT_EQ(text.texts[3].style.font, Client::Render::Text::FontKind::Normal);
}

// Cada lugar del equipo y cada casilla toma el color del aviso de su ítem (0x5e22d0): el 8, roto, en rojo.
TEST_F(GameInventoryHudTest, TheSlotsTakeTheColorOfTheirWarning)
{
    world.OwnCharacter().equipment[8].warning = 8;
    world.Inventory().cells[0].warning = 2;

    ASSERT_TRUE(Window().HasValue());

    const auto helper = NthQuadOf(sprites, 0x113);
    ASSERT_TRUE(helper.has_value());
    EXPECT_EQ(helper->color, (Color{1.0f, 0.0f, 0.0f, 1.0f}));
    const auto cell = NthQuadOf(sprites, 0x115);
    ASSERT_TRUE(cell.has_value());
    EXPECT_EQ(cell->color, (Color{0.6f, 0.7f, 1.0f, 1.0f}));
}

// 0x5f7610 con el inventario: el registro abierto alrededor del equipo, los rand() de 0x607b60 por el modelo (el 0xe
// es el 0x220, uno; el 0xa05 y el 0xc0e, el 0xc17 y el 0xe20, dos; el 0xa07, el 0xc19, uno), los dos de 0x609640 del
// 0xc0f (0xe21) y las consultas del comercio y de la tienda. Las casillas que no son la de arriba a la izquierda no
// cuentan, y el 0x1a13 con el nivel en 3 no dibuja nada.
TEST_F(GameInventoryHudTest, TheGridItemsSpendTheRandomsOfTheirModels)
{
    PutItem(0, 0x000e, 1, 3);
    PutItem(1, 0x0c0e, 2, 2);
    PutItem(3, 0x0c0f, 2, 2);
    PutItem(5, 0x0a05, 1, 3);
    PutItem(6, 0x1a13, 1, 1, 0, 3u << 3u);
    ItemState& weapon = world.OwnCharacter().equipment[0];
    weapon.id = 0x0a07;
    weapon.occupied = 1;
    const CrtRandom expected = Advanced(random, Own + 1 + 2 + 2 + 2 + 1 + 2);

    const auto drawn = GridItems();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// El pase del set (0x60cc29): con el set en 1 o 2, sin excelente y con sin(0,001 · T) · 0,5 + 0,4 en 0,01 o menos, un
// rand() % 100 en el +0xc0 del modelo.
TEST_F(GameInventoryHudTest, TheSetPassDrawsItsRandomWhenThePulseIsOff)
{
    worldClock = 4712.0f;
    CrtRandom copy = random;
    const int selected = copy.Next() % 100;

    ASSERT_TRUE(UiItem(0x0001, 0, 0, 1).HasValue());

    EXPECT_EQ(random.State(), copy.State());
    EXPECT_EQ(world.Runtime().itemRender.selections.at(0x213), selected);
    const CrtRandom before = random;
    ASSERT_TRUE(UiItem(0x0001, 0, 1, 1).HasValue());
    ASSERT_TRUE(UiItem(0x0001, 0, 0, 3).HasValue());
    worldClock = 0.0f;
    ASSERT_TRUE(UiItem(0x0001, 0, 0, 2).HasValue());
    EXPECT_EQ(random.State(), before.State());
}

// Las alas 0x1806 (el modelo 0x1a18) cortan antes de sus uniones; las otras grillas, con su dirección.
TEST_F(GameInventoryHudTest, WhatBelongsToOtherWavesStopsWithItsAddress)
{
    PutItem(0, 0x1806, 2, 2);
    auto wings = GridItems();
    ASSERT_FALSE(wings.HasValue());
    EXPECT_NE(wings.Error().find("0x60c3b6"), std::string::npos) << wings.Error();

    world.Inventory().ClearForInventoryReply();
    world.Runtime().ownRecordDepth = 0;
    world.Runtime().panels.npcShop = 1;
    auto shop = GridItems();
    ASSERT_FALSE(shop.HasValue());
    EXPECT_NE(shop.Error().find("0x5f769b"), std::string::npos) << shop.Error();
    auto shopNumbers = GridNumbers();
    ASSERT_FALSE(shopNumbers.HasValue());
    EXPECT_NE(shopNumbers.Error().find("0x5f7973"), std::string::npos) << shopNumbers.Error();

    world.Runtime().panels.npcShop = 0;
    world.Runtime().panels.delegated.trainer = 3;
    auto trainer = GridItems();
    ASSERT_FALSE(trainer.HasValue());
    EXPECT_NE(trainer.Error().find("0x5f782b"), std::string::npos) << trainer.Error();
}

// 0x5f7840: las pociones con 2 o más (9 × 10, 13 a la derecha) y los paquetes de joyas con ((nivel + 1) · 5) · 2
// (7 × 8, 14 a la derecha), en (1; 0,9; 0,7) y con la mezcla de alfa.
TEST_F(GameInventoryHudTest, TheNumbersShowThePotionsAndTheJewelBundles)
{
    world.Runtime().hud.inventoryX = 0x1c2;
    PutItem(0, 0x1c00, 1, 1, 3);
    PutItem(1, 0x181e, 1, 1, 0, 2u << 3u);
    PutItem(2, 0x1c00, 1, 1, 1);
    PutItem(3, 0x000e, 1, 3, 5);

    ASSERT_TRUE(GridNumbers().HasValue());

    EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{DigitSlot, DigitSlot, DigitSlot}));
    const auto three = NthQuadOf(sprites, DigitSlot, 0);
    ASSERT_TRUE(three.has_value());
    EXPECT_FLOAT_EQ(three->first.x, 465.0f + 13.0f - 4.5f);
    EXPECT_FLOAT_EQ(three->third.y, 480.0f - 210.0f);
    EXPECT_EQ(three->color, (Color{1.0f, 0.9f, 0.7f, 1.0f}));
    const auto thirty = NthQuadOf(sprites, DigitSlot, 1);
    ASSERT_TRUE(thirty.has_value());
    EXPECT_FLOAT_EQ(thirty->first.x, 485.0f + 14.0f - 7.0f);
    EXPECT_FLOAT_EQ(thirty->third.y, 480.0f - 208.0f);
}

// 0x5f7b10: el ítem del cursor saca los rand() de su modelo; sin ítem, nada.
TEST_F(GameInventoryHudTest, TheCursorItemSpendsTheRandomsOfItsModel)
{
    world.Runtime().cursorItem.id = 0x0c0e;
    const CrtRandom unchanged = random;
    {
        GameHudContext context = Context();
        ASSERT_TRUE(Client::Scenes::DrawTheCursorItem(context).HasValue());
    }
    EXPECT_EQ(random.State(), unchanged.State());

    world.Runtime().cursorItemCount = 1;
    const CrtRandom expected = Advanced(random, 2);
    GameHudContext context = Context();
    ASSERT_TRUE(Client::Scenes::DrawTheCursorItem(context).HasValue());
    EXPECT_EQ(random.State(), expected.State());
}

// El HUD entero con el inventario abierto, en Lorencia y en Devias: el estado base más la ventana, el cierre del
// registro alrededor del equipo de 0x5e2fc0 y las dos consultas que el ancho del mundo (0x5bb8d0) hace con el
// inventario abierto (la tienda y el comercio) en los avisos de durabilidad.
TEST_F(GameInventoryHudTest, TheHudWithTheInventoryOpenSpendsItsRandomsInLorenciaAndDevias)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        random = CrtRandom{Seed};
        sprites.Begin(Screen640);
        const CrtRandom expected = Advanced(random, HudBaseDraws + WindowDraws + Own + 2);

        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    }
}
} // namespace
