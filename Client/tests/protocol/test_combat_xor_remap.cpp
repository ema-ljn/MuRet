#include "Protocol/Combat/Combat.h"

#include "App/LabXorRemapTables.h"
#include "Network/XorRemap/XorRemapObfuscation.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>

// What the XOR and remap layer of the lab server does to the combat packets (test_combat.cpp has the rest).
namespace
{
using Client::App::LabXorRemapTables;
using Client::Network::XorRemap::XorRemapObfuscation;
namespace Combat = Client::Protocol::Combat;

// The D7 of the attack leaves as A4, the GS's PMSG_ATTACK (CGAttack 0x4254d0).
TEST(CombatProtocolXorRemapTest, TheAttackLeavesAsA4)
{
    auto sent = Combat::WriteAttack(0x1234, Combat::AttackAction, 5);
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const auto wire = xorRemap.BeforeSend(std::move(sent.packet));
    EXPECT_EQ(wire.Bytes()[2], std::byte{0xa4});
    EXPECT_EQ(wire.Bytes().size(), 7u);
}

// The DF of the elf's cell leaves as it is (Q-C1163).
TEST(CombatProtocolXorRemapTest, TheElfCellLeavesAsDf)
{
    auto sent = Combat::WriteElfCell(183, 115);
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const auto wire = xorRemap.BeforeSend(std::move(sent.packet));
    EXPECT_EQ(wire.Bytes()[2], std::byte{0xdf});
}
} // namespace
