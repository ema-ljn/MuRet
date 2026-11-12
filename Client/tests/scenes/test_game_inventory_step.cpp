#include "Core/Expected.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

// El paso del inventario abierto (0x5ee1ac..0x5ee1ca, CL8 hito 4): 0x5e4ce0 con el equipo (0x5c3d20 → 0x5bec50) o
// la grilla (0x5c4110) sin nada bajo el mouse, los botones del pie y las seis hermanas. Los ítems son inventados.
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

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::StepTheGameUi(
            GameUiContext{.world = world, .random = random, .input = input, .mouseX = x, .mouseY = y});
    }

    void Click()
    {
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true);
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

// Lo que es de la segunda parte del hito o de otras olas corta con su dirección: un ítem de la grilla o un lugar del
// equipo bajo el mouse, el cartel del set, la tienda personal que abre la S (Open de 0x800, CL9.4) y el comercio
// abierto.
TEST_F(InventoryStepTest, WhatIsStillCutStopsWithItsAddress)
{
    world.Inventory().cells[0].id = 0x1c00;
    ExpectCut(Step(WindowX + 15, 200), "0x5c41f4");
    world.OwnCharacter().equipment[0].id = 0x0001;
    ExpectCut(Step(WindowX + 15, 89), "0x5c2c56");
    world.Runtime().cursorItemCount = 1;
    ExpectCut(Step(WindowX + 15 + 39, 89 + 59), "0x5bee04");
    world.Runtime().cursorItemCount = 0;
    ExpectCut(Step(WindowX + 0xe, 0x16), "0x40e6f5");

    world.OwnCharacter().level = 6;
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::S});
    ExpectCut(Step(100, 300), "0x6599a0(0x800)");

    world.Runtime().panels.trade = 1;
    ExpectCut(Step(100, 300), "0x5e4e34");
}
} // namespace
