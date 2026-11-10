#include "App/TeleportHistory.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
using Client::App::TeleportHistory;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

// 0x4e3e98..0x4e3f1d: conserva sólo diez recepciones, incluidas las que no se aplican.
TEST(TeleportHistoryTest, AnEleventhReceptionDropsOnlyTheOldestOwnedCopy)
{
    TeleportHistory history;
    for (std::uint8_t gate = 0; gate < 11; ++gate)
    {
        auto packet = Bytes({0xc1, 8, 0x1c, gate, 0, 1, 2, 3});
        history.Record(packet);
        packet[3] = std::byte{0xff};
    }
    ASSERT_EQ(history.Entries().size(), 10u);
    EXPECT_TRUE(SameBytes(history.Entries().front(), Bytes({0xc1, 8, 0x1c, 1, 0, 1, 2, 3})));
    EXPECT_TRUE(SameBytes(history.Entries().back(), Bytes({0xc1, 8, 0x1c, 10, 0, 1, 2, 3})));
}

// El desplazamiento antecede a cmp len,256: al estar lleno, uno largo deja sólo nueve.
TEST(TeleportHistoryTest, AnOversizedReceptionShiftsTheFullHistoryBeforeBeingDiscarded)
{
    TeleportHistory history;
    for (int gate = 0; gate < 10; ++gate)
    {
        history.Record(Bytes({0xc1, 4, 0x1c, gate}));
    }
    history.Record(std::vector<std::byte>(257, std::byte{0xaa}));
    ASSERT_EQ(history.Entries().size(), 9u);
    EXPECT_EQ(history.Entries().front()[3], std::byte{1});
    EXPECT_EQ(history.Entries().back()[3], std::byte{9});
    history.Record(std::vector<std::byte>(256, std::byte{0xbb}));
    ASSERT_EQ(history.Entries().size(), 10u);
    EXPECT_EQ(history.Entries().back().size(), 256u);
    EXPECT_EQ(history.Entries().back().front(), std::byte{0xbb});
}

TEST(TeleportHistoryTest, AnOversizedReceptionDoesNotEraseAShorterHistory)
{
    TeleportHistory history;
    history.Record(Bytes({0xc1, 3, 0x1c}));
    history.Record(std::vector<std::byte>(257, std::byte{0xaa}));
    ASSERT_EQ(history.Entries().size(), 1u);
    EXPECT_TRUE(SameBytes(history.Entries().front(), Bytes({0xc1, 3, 0x1c})));
}
} // namespace
