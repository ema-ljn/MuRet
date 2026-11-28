#include "FrontEnd/SelectionInventory.h"

#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"

#include <gtest/gtest.h>

#include <memory>
#include <span>

TEST(SelectionInventoryTest, TheNpcGridsClearOnlyTheirTypeAndOccupation)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    auto& npc = world->Runtime().npcWindows;
    const auto prepare = [](std::span<Client::Kernel::ItemState> cells)
    {
        for (auto& cell : cells)
        {
            cell.id = 7;
            cell.occupied = 1;
            cell.scalar1Low = 0x1234;
        }
    };
    prepare(npc.grid);
    prepare(npc.chaosBox);
    prepare(npc.trainerBox);
    npc.jewels.mode = 1;
    npc.jewels.kind = 0;
    npc.jewels.count = 10;
    npc.field6e5798 = 1;
    npc.jewels.bundles.rows.push_back({1, 9, 2});
    Client::Crt::CrtRandom random(42);
    auto expected = random;
    expected.Discard(Client::Models::OwnRecordCloseRandoms);
    Client::FrontEnd::ClearSelectionInventory(*world, random);
    const auto check = [](std::span<const Client::Kernel::ItemState> cells)
    {
        for (const auto& cell : cells)
        {
            EXPECT_EQ(cell.id, Client::Kernel::EmptyOwnItemId);
            EXPECT_EQ(cell.occupied, 0);
            EXPECT_EQ(cell.scalar1Low, 0x1234u);
        }
    };
    check(npc.grid);
    check(npc.chaosBox);
    check(npc.trainerBox);
    EXPECT_EQ(npc.jewels.mode, 0);
    EXPECT_EQ(npc.jewels.kind, 0xff);
    EXPECT_EQ(npc.jewels.count, 0xff);
    EXPECT_EQ(npc.field6e5798, 0);
    ASSERT_EQ(npc.jewels.bundles.rows.size(), 1u);
    EXPECT_EQ(npc.jewels.bundles.rows[0].cell, 9);
    EXPECT_EQ(random.State(), expected.State());
}
