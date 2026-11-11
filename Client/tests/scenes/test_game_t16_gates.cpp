#include "test_game_t16_support.h"

#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameGates.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace
{
// Inventario independiente de Gate.bmd: 38 entradas distribuidas en 13 mapas fuente (H2 T16).
constexpr std::array<std::uint8_t, 38> EntryGates{1,  3,  5,   7,   9,   11,  13,  15,  18,  20,  23,  25, 28,
                                                  30, 32, 34,  36,  38,  40,  43,  45,  47,  53,  55,  62, 64,
                                                  96, 98, 102, 107, 109, 115, 117, 120, 121, 123, 127, 129};
constexpr std::array<std::uint8_t, 13> Sources{0, 1, 2, 3, 4, 6, 7, 8, 10, 30, 31, 33, 34};

class GameT16GateTransport : public Client::Tests::T16::SceneTest, public ::testing::WithParamInterface<std::uint8_t>
{
};

// Contrato de consumidor y transporte, no caminata de escena en los 13 mapas. La matriz de escena
// registra las fronteras que impiden llegar a este consumidor desde una entrada real.
TEST_P(GameT16GateTransport, EveryArchivedEntrySendsItsOwnEncryptedRequestBeforeAnyMacroBoundary)
{
    const auto index = GetParam();
    const auto record = Client::Data::Tables::ReadGateRecord(*m_data->gates, index);
    ASSERT_TRUE(record.has_value());
    ASSERT_EQ(record->flag, 1);
    const auto destination = Client::Data::Tables::ReadGateRecord(*m_data->gates, record->target);
    ASSERT_TRUE(destination.has_value());
    m_world->Runtime().map = record->map;
    m_world->Runtime().warpCounter = 0;
    Hero().cellX = record->x1;
    Hero().cellY = record->y1;
    m_world->OwnCharacter().level = 400;
    m_world->OwnCharacter().equipment[7].id = 0x1800; // Alas: requisito del gate62.
    const auto checked =
        Client::Scenes::CheckGameGates({*m_world, *m_data->gates, m_cloth->random, 3000,
                                        [&](std::uint8_t requested) { m_online->session.RequestGate(requested); }});
    const bool macro = destination->map == 30 || destination->map == 31;
    EXPECT_EQ(checked.HasValue(), !macro);
    if (!checked.HasValue())
    {
        EXPECT_NE(checked.Error().find("0x61c210"), std::string::npos) << checked.Error();
    }
    const auto sent = Sent();
    ASSERT_EQ(sent.packets.size(), 1u);
    ASSERT_EQ(sent.serials.size(), 1u); // C3 real, descifrado por el fakeGS con su serial.
    EXPECT_EQ(sent.packets[0].Opcode(), 0x1c);
    ASSERT_EQ(sent.packets[0].Bytes().size(), 6u);
    EXPECT_EQ(sent.packets[0].Bytes()[3], std::byte{index});
    EXPECT_EQ(m_world->Runtime().warpCounter, 9'999'999);
    EXPECT_EQ(m_world->Runtime().movement.warpPending, !macro);
    RecordProperty("scope", "real Gate.bmd consumer plus session transport; scene boundaries in GameT16SceneMatrix");
}

using GameT16GateInventory = Client::Tests::T16::SceneTest;
TEST_F(GameT16GateInventory, AllThirtyEightEntriesCoverExactlyTheThirteenSourceMaps)
{
    std::set<std::uint8_t> sources;
    std::set<std::uint8_t> entries;
    for (std::size_t index = 0; index < Client::Data::Tables::GateCount; ++index)
    {
        const auto record = Client::Data::Tables::ReadGateRecord(*m_data->gates, index);
        ASSERT_TRUE(record.has_value());
        if (record->flag != 1)
            continue;
        sources.insert(record->map);
        entries.insert(static_cast<std::uint8_t>(index));
    }
    EXPECT_EQ(entries, (std::set<std::uint8_t>{EntryGates.begin(), EntryGates.end()}));
    EXPECT_EQ(sources, (std::set<std::uint8_t>{Sources.begin(), Sources.end()}));
}

INSTANTIATE_TEST_SUITE_P(AllThirtyEightEntries, GameT16GateTransport, ::testing::ValuesIn(EntryGates),
                         [](const ::testing::TestParamInfo<std::uint8_t>& gateParameter)
                         { return "Gate" + std::to_string(gateParameter.param); });
} // namespace
