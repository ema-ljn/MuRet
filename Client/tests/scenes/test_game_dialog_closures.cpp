#include "Network/Xor32Packet.h"
#include "Scenes/GameUi.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Scenes/InventoryMoves.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
class GameDialogClosuresTest : public ::testing::TestWithParam<std::uint8_t>
{
};

// 623f17→657880: Esc usa su propio registro; los servicios de otro GamePanels del paso no llegan aquí.
TEST_P(GameDialogClosuresTest, EscapeClosesTheNpcThroughTheTransportBorrowedByTheDialogStep)
{
    const auto world = std::make_unique<Client::Scenes::WorldState>();
    auto& runtime = world->Runtime();
    const auto kind = GetParam();
    if (kind == 0)
        runtime.panels.warehouse = 1;
    else if (kind == 1)
        runtime.panels.chaosBox = 1;
    else
        runtime.panels.delegated.trainer = 1;
    Client::Crt::CrtRandom random(4242);
    Client::Input::InputState input;
    input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    const Client::Scenes::InventoryServices inventory;
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Scenes::GameUiContext context{.world = *world, .random = random, .input = input, .inventory = &inventory};
    context.sendNpcWindow = [&](Client::Protocol::Outgoing packet)
    {
        sent.push_back(std::move(packet));
        return Client::Core::Expected<void, std::string>{};
    };
    const auto stepped = Client::Scenes::StepTheGameDialogs(context);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(sent.size(), 1u);
    const std::uint8_t opcode = kind == 0 ? 0x82 : kind == 1 ? 0x87 : 0x31;
    const auto plain = Client::Network::Xor32Decode(sent[0].packet);
    EXPECT_TRUE(Client::Tests::SameBytes(plain.Bytes(), Client::Tests::Bytes({0xc1, 3, opcode})));
    EXPECT_EQ(runtime.panels.warehouse, 0);
    EXPECT_EQ(runtime.panels.chaosBox, 0);
    EXPECT_EQ(runtime.panels.delegated.trainer, 0);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

INSTANTIATE_TEST_SUITE_P(NpcKinds, GameDialogClosuresTest,
                         ::testing::Values(std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}));
} // namespace
