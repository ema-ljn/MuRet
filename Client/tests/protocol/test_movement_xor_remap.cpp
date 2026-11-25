#include "Protocol/Movement.h"

#include "App/LabXorRemapTables.h"
#include "Network/XorRemapObfuscation.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

// What the XOR and remap layer of the lab server does to the walk (test_movement.cpp has the rest).
namespace
{
using Client::App::LabXorRemapTables;
using Client::Network::XorRemapObfuscation;
using Client::Protocol::Movement::Cell;
using Client::Protocol::Movement::WriteWalk;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

// 04 § Caminar y pegar: the D3 of the walk leaves as A3, with its XOR32 applied again.
TEST(MovementXorRemapTest, TheWalkLeavesAsA3)
{
    constexpr std::array<Cell, 4> Path{{{0xb6, 0x73}, {0xb5, 0x73}, {0xb4, 0x74}, {0xb3, 0x74}}};
    auto sent = WriteWalk(Path, {0, 0}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const auto wire = xorRemap.BeforeSend(std::move(sent.Value().packet));
    EXPECT_TRUE(SameBytes(wire.Bytes(), Bytes({0xc1, 0x08, 0xa3, 0x9c, 0x53, 0x92, 0x7b, 0x78})));
}
} // namespace
