#include "Network/Xor32Packet.h"
#include "Hud/GameUi.h"

#include "Crt/CrtRandom.h"
#include "Input/InputState.h"
#include "Protocol/Outgoing.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"

#include "support/Bytes.h"
#include "support/ItemFiles.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace
{
TEST(EventTicketsTest, TheRightClickRequestsEachEventAndKeepsOnlyTheChaosTicket)
{
    const auto items = Client::Tests::ItemTableOf({{0x1a1d, 1, 1, 0xff}, {0x1c13, 1, 1, 0xff}, {0x1a12, 1, 1, 0xff}});
    struct Ticket
    {
        std::uint16_t type;
        std::uint8_t kind;
    };
    constexpr std::array tickets{Ticket{0x1a1d, 4}, Ticket{0x1c13, 1}, Ticket{0x1a12, 2}};
    for (const auto& [type, kind] : tickets)
    {
        SCOPED_TRACE(type);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        world->Inventory().ClearForInventoryReply();
        auto& runtime = world->Runtime();
        runtime.panels.inventory = 1;
        runtime.ui.rightClick = true;
        runtime.slot78a6758 = 99;
        runtime.dialogItem.id = 0x4321;
        auto& item = world->Inventory().cells[9];
        item.id = type;
        item.occupied = 1;
        item.gridColumn = 1;
        item.gridRow = 1;
        item.scalar1Low = 0x12345678u;
        std::vector<Client::Protocol::Outgoing> sent;
        Client::Scenes::InventoryServices services{.items = &items,
                                                   .sendGamePacket = [&sent](Client::Protocol::Outgoing packet)
                                                   { sent.push_back(std::move(packet)); }};
        Client::Input::InputState input;
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::Scenes::StepTheGameUi({.world = *world,
                                                            .random = random,
                                                            .input = input,
                                                            .mouseX = runtime.hud.inventoryX + 15 + 20 + 5,
                                                            .mouseY = runtime.hud.inventoryY + 200 + 20 + 5,
                                                            .inventory = &services});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        const auto decoded = Client::Network::Xor32Decode(sent[0].packet);
        EXPECT_TRUE(Client::Tests::SameBytes(decoded.Bytes(), Client::Tests::Bytes({0xc1, 4, 0x91, kind})));
        EXPECT_FALSE(runtime.ui.rightClick);
        EXPECT_EQ(item.id, type);
        EXPECT_EQ(runtime.slot78a6758, kind == 4 ? 21 : 99);
        EXPECT_EQ(runtime.dialogItem.id, kind == 4 ? 0x1a1d : 0x4321);
        if (kind == 4)
        {
            EXPECT_EQ(runtime.dialogItem.scalar1Low, 0x12345678u);
        }
    }
}
} // namespace
