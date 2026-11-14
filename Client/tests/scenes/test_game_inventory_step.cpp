#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include "support/ItemFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// El paso del inventario abierto (0x5ee1ac..0x5ee1ca, CL8 hito 4): 0x5e4ce0 con el equipo (0x5c3d20 → 0x5bec50) o
// la grilla (0x5c4110), los botones del pie y las seis hermanas, y el del ítem del cursor (0x5d8d50). Los ítems son
// inventados.
namespace
{
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
// El paso con todo cerrado (test_game_ui.cpp): la barra de skills, la cadena de Q, W y E y la cola IsOpen(0).
constexpr int BaseStepRandoms = Own + 2 + 2;
// 0x5e4ce0 sin nada bajo el mouse: las consultas de la tienda y del comercio de las marcas, de los botones y de las
// otras grillas (6), los doce lugares del equipo y el nivel (13 cierres del registro), y las hermanas del comercio y
// de la tienda (2).
constexpr int InventoryStepRandoms = 8 + 13 * Own;
// 0x5c3d20 con el mouse arriba de la grilla: once 0x5bec50 con su registro y el acceso del casco.
constexpr int EquipmentStepRandoms = 12 * Own;
constexpr std::int32_t WindowX = 0x1c2;

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

class InventoryStepTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().panels.inventory = 1;
        world.Runtime().hud.inventoryX = WindowX;
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y,
                                                   const Client::Scenes::InventoryServices* inventory = nullptr)
    {
        return Client::Scenes::StepTheGameUi(GameUiContext{
            .world = world, .random = random, .input = input, .mouseX = x, .mouseY = y, .inventory = inventory});
    }

    void Click()
    {
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true, false);
    }

    static void ExpectCut(const Client::Core::Expected<void, std::string>& stepped, std::string_view address)
    {
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find(address), std::string::npos) << stepped.Error();
    }

    WorldState world;
    CrtRandom random{0x2468aceu};
    InputState input;
};

// Sin nada bajo el mouse, en Lorencia y en Devias: abajo, la grilla no saca nada; arriba, los doce lugares del
// equipo cierran su registro. Las marcas +0x44 quedan en 1 con un tipo y en 0 sin él.
TEST_F(InventoryStepTest, TheQuietStepSpendsTheRandomsOfItsAccessesInLorenciaAndDevias)
{
    world.OwnCharacter().equipment[3].id = 0x0c0e;
    world.OwnCharacter().equipment[5].warning = 7;
    world.Inventory().cells[9].id = 0x1c00;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        CrtRandom expected = Advanced(random, BaseStepRandoms + InventoryStepRandoms);
        ASSERT_TRUE(Step(100, 300).HasValue());
        EXPECT_EQ(random.State(), expected.State());
        expected = Advanced(random, BaseStepRandoms + InventoryStepRandoms + EquipmentStepRandoms);
        ASSERT_TRUE(Step(300, 100).HasValue());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(world.OwnCharacter().equipment[3].warning, 1);
        EXPECT_EQ(world.OwnCharacter().equipment[5].warning, 0);
        EXPECT_EQ(world.Inventory().cells[9].warning, 1);
        EXPECT_EQ(world.Inventory().cells[10].warning, 0);
        EXPECT_EQ(world.Runtime().panels.inventory, 1);
    }
}

// 0x5e4ce0..0x5e4d1d: el mouse sobre la ventana (190 × 433) es de la UI.
TEST_F(InventoryStepTest, TheMouseOverTheWindowBelongsToTheUi)
{
    ASSERT_TRUE(Step(WindowX + 0xbd, 300).HasValue());
    EXPECT_EQ(world.Runtime().ui.mouseOnUi, 1);
    ASSERT_TRUE(Step(WindowX - 1, 300).HasValue());
    EXPECT_EQ(world.Runtime().ui.mouseOnUi, 0);
}

// El botón de cerrar (x + 0x14 a x + 0x2c, y + 0x18b a y + 0x1a3) consume el clic y cierra el inventario.
TEST_F(InventoryStepTest, TheCloseButtonClosesTheInventory)
{
    world.Runtime().movement.mouseUpdateTime = 4;
    Click();
    ASSERT_TRUE(Step(WindowX + 0x14, 0x18b).HasValue());
    EXPECT_EQ(world.Runtime().panels.inventory, 0);
    EXPECT_FALSE(world.Runtime().movement.pendingClick);
    EXPECT_EQ(world.Runtime().movement.mouseUpdateTime, 0);
    EXPECT_EQ(world.Runtime().movement.mouseUpdateTimeMax, 6);
}

// Reparar (x + 0x30) solo desde el nivel 50, que también deja 0x78abb10 en 1; el clic invierte 0x78abb0c.
TEST_F(InventoryStepTest, TheRepairButtonWorksFromLevelFifty)
{
    world.OwnCharacter().level = 49;
    Click();
    ASSERT_TRUE(Step(WindowX + 0x30, 0x18b).HasValue());
    EXPECT_EQ(world.Runtime().panels.repairMode, 0);
    EXPECT_EQ(world.Runtime().field78abb10, 0);
    EXPECT_TRUE(world.Runtime().movement.pendingClick);

    world.OwnCharacter().level = 50;
    ASSERT_TRUE(Step(WindowX + 0x30, 0x18b).HasValue());
    EXPECT_EQ(world.Runtime().panels.repairMode, 1);
    EXPECT_EQ(world.Runtime().field78abb10, 1);
    EXPECT_FALSE(world.Runtime().movement.pendingClick);
}

// Lo que es de otras olas, o lo que pide Item.bmd sin tenerlo, corta con su dirección: un ítem de la grilla bajo el
// mouse (0x5c420a) y el ítem del cursor sobre un lugar del equipo (0x5bed9e) sin Item.bmd, la tienda personal que abre
// la S (Open de 0x800, CL9.4) y el comercio abierto.
TEST_F(InventoryStepTest, WhatIsStillCutStopsWithItsAddress)
{
    world.Inventory().cells[0].id = 0x1c00;
    ExpectCut(Step(WindowX + 15, 200), "0x5c420a");
    world.Runtime().cursorItemCount = 1;
    ExpectCut(Step(WindowX + 15 + 39, 89 + 59), "0x5bed9e");
    world.Runtime().cursorItemCount = 0;
    // El cartel del set ya no corta (hito 5): 0x40e6c0 prende la ventana del set.
    const auto label = Step(WindowX + 0xe, 0x16);
    ASSERT_TRUE(label.HasValue()) << label.Error();
    EXPECT_EQ(world.ItemSets().byte9304, 1);

    world.OwnCharacter().level = 6;
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::S});
    ExpectCut(Step(100, 300), "0x6599a0(0x800)");

    world.Runtime().panels.trade = 1;
    ExpectCut(Step(100, 300), "0x5e4e34");
}

// 0x5c2c56 → 0x5c35ad: el mouse sobre un lugar ocupado, sin el clic, prende 0x78abaac (y 0x5d8d50 no corre), marca el
// lugar con 2 y deja el ítem del cartel y la esquina de su ayuda. No saca más rand() que el paso quieto.
TEST_F(InventoryStepTest, AnOccupiedEquipmentPlaceUnderTheMouseIsMarked)
{
    world.OwnCharacter().equipment[0].id = 0x0001;
    const CrtRandom expected = Advanced(random, BaseStepRandoms + InventoryStepRandoms + EquipmentStepRandoms);
    ASSERT_TRUE(Step(WindowX + 15, 89).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[0].warning, 2);
    EXPECT_EQ(world.Runtime().ui.field78abaac, 1);
    EXPECT_EQ(world.Runtime().ui.hoveredItem.place, Client::UI::HoveredItem::Place::Equipment);
    EXPECT_EQ(world.Runtime().ui.skillTooltipX, WindowX + 15 + 20);
    EXPECT_EQ(world.Runtime().ui.skillTooltipY, 89);
    EXPECT_EQ(random.State(), expected.State());
}

class InventoryMovesStepTest : public InventoryStepTest
{
protected:
    void SetUp() override
    {
        InventoryStepTest::SetUp();
        services.items = &items;
        services.dressTheHero = [this]
        {
            ++dressed;
            return Client::Core::Expected<void, std::string>{};
        };
        services.systemMessage = [this](std::size_t index, std::int32_t)
        {
            messages.push_back(index);
            return Client::Core::Expected<void, std::string>{};
        };
        // Una poción en la casilla (1, 2), con su columna y su fila.
        Client::World::ItemState& potion = world.Inventory().cells[PotionCell];
        potion.id = Potion;
        potion.gridColumn = 1;
        potion.gridRow = 2;
        potion.occupied = 1;
    }

    static constexpr std::uint16_t Potion = 0x1c00;
    static constexpr std::size_t PotionCell = 2 * 8 + 1;
    static constexpr std::int32_t PotionX = WindowX + 15 + 20 + 5;
    static constexpr std::int32_t PotionY = 200 + 40 + 5;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemTableOf({{Potion, 1, 1, 0xff}, {0x1c0d, 1, 1, 0xff}});
    Client::Scenes::InventoryServices services;
    int dressed = 0;
    std::vector<std::size_t> messages;
};

// 0x5c41f4..0x5c4b3b sin el clic: la marca 2, el ítem del cartel (la casilla + 12) y la esquina de la ayuda. Ctrl con
// W pone el tipo y el nivel en la ranura rápida 1.
TEST_F(InventoryMovesStepTest, AGridItemUnderTheMouseIsMarkedAndCtrlPutsItInAQuickSlot)
{
    world.Inventory().cells[PotionCell].scalar1Low = 2u << 3;
    const CrtRandom expected = Advanced(random, BaseStepRandoms + InventoryStepRandoms);
    ASSERT_TRUE(Step(PotionX, PotionY, &services).HasValue());
    EXPECT_EQ(world.Inventory().cells[PotionCell].warning, 2);
    EXPECT_EQ(world.Runtime().ui.hoveredItem.index, PotionCell);
    EXPECT_EQ(world.Runtime().hoveredItemCopy.id, Potion);
    EXPECT_EQ(world.Runtime().ui.field78abb2c, static_cast<std::int32_t>(PotionCell) + 12);
    EXPECT_EQ(world.Runtime().ui.skillTooltipX, WindowX + 15 + 10 + 20);
    EXPECT_EQ(world.Runtime().ui.skillTooltipY, 200 + 40);
    EXPECT_EQ(random.State(), expected.State());

    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::CtrlLeft});
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::W});
    ASSERT_TRUE(Step(PotionX, PotionY, &services).HasValue());
    EXPECT_EQ(world.Runtime().hud.quickSlots.types[1], Potion);
    EXPECT_EQ(world.Runtime().hud.quickSlots.levels[1], 2);
}

// Con el clic, 0x5c4110 la levanta y sale, con el rand() de IsOpen(0x20) y el sonido 0x1d; en el mismo paso, 0x5d8d50
// la ve en el cursor sin clic (otro IsOpen(0x20)). Con el inventario cerrado, Esc abre el menú y la vuelve a su lugar
// (0x623f42).
TEST_F(InventoryMovesStepTest, AClickPicksTheItemUpAndEscapePutsItBack)
{
    const CrtRandom expected = Advanced(random, BaseStepRandoms + InventoryStepRandoms + 2);
    Click();
    ASSERT_TRUE(Step(PotionX, PotionY, &services).HasValue());
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_EQ(world.Runtime().cursorItem.id, Potion);
    EXPECT_EQ(world.Runtime().slot78a6758, static_cast<std::int32_t>(PotionCell) + 12);
    EXPECT_EQ(world.Inventory().cells[PotionCell].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(world.Runtime().lastSoundId, 0x1d);
    EXPECT_FALSE(world.Runtime().movement.pendingClick);
    EXPECT_EQ(random.State(), expected.State());

    world.Runtime().panels.inventory = 0;
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::Escape});
    ASSERT_TRUE(Client::Scenes::StepTheGameDialogs(GameUiContext{.world = world,
                                                                 .random = random,
                                                                 .input = input,
                                                                 .mouseX = PotionX,
                                                                 .mouseY = PotionY,
                                                                 .inventory = &services})
                    .HasValue());
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().cells[PotionCell].id, Potion);
    EXPECT_EQ(world.Inventory().cells[PotionCell].occupied, 1u);
    EXPECT_EQ(dressed, 1);
}

// 0x5d8d50 con el ítem en el cursor y el clic fuera de las grillas: consume el clic (MouseUpdateTime en 0 y su máximo
// en 6) y lo tira al piso. La poción se tira (CL9.3: corta en 0x5df58b); un Jewel of Bless vuelve con el aviso 269.
TEST_F(InventoryMovesStepTest, AClickOutsideTheGridsDropsTheCursorItem)
{
    Click();
    ASSERT_TRUE(Step(PotionX, PotionY, &services).HasValue());
    world.Runtime().panels.inventory = 0;
    Click();
    world.Runtime().movement.mouseUpdateTime = 4;
    ExpectCut(Step(100, 300, &services), "0x5df58b");
    EXPECT_FALSE(world.Runtime().movement.pendingClick);
    EXPECT_EQ(world.Runtime().movement.mouseUpdateTime, 0);
    EXPECT_EQ(world.Runtime().movement.mouseUpdateTimeMax, 6);

    world.Runtime().cursorItem.id = 0x1c0d;
    Click();
    ASSERT_TRUE(Step(100, 300, &services).HasValue());
    EXPECT_EQ(messages, std::vector<std::size_t>{269});
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().cells[PotionCell].id, 0x1c0d);
}
} // namespace
