#include "World/CastleSiegeObjectStep.h"
#include "World/ModelEffects.h"
#include "World/WorldObjects.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
namespace
{
TEST(CastleGateCollapseTest, TheFragmentAndSoundPrecedeHidingOnlyMeshZero)
{
    Client::World::WorldObject gate;
    gate.type = 0x3d;
    gate.field08 = 99;
    gate.seen = true;
    gate.state.hiddenMesh = -1;
    std::vector<int> calls;
    const Client::World::CastleGateCollapseServices services{
        .emit = [&](const Client::World::ModelEffectArguments& effect) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(gate.state.hiddenMesh, -1);
            EXPECT_EQ(effect.type, 0x3d);
            EXPECT_EQ(effect.subtype, 1);
            EXPECT_EQ(effect.owner, nullptr);
            calls.push_back(1);
            return {};
        },
        .sound =
            [&](int sound)
        {
            EXPECT_EQ(sound, 0x1ec);
            EXPECT_EQ(gate.state.hiddenMesh, -1);
            calls.push_back(2);
        }};
    ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(gate, true, 0, services).HasValue());
    EXPECT_EQ(gate.state.hiddenMesh, 0);
    EXPECT_EQ(calls, (std::vector<int>{1, 2}));
    ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(gate, true, 0, services).HasValue());
    EXPECT_EQ(calls.size(), 2u);
}
TEST(CastleGateCollapseTest, UnseenOrNotYetDestroyedGatesDoNotEmitAndIntactGatesRestoreTheMesh)
{
    Client::World::WorldObject gate;
    gate.type = 0x40;
    gate.state.hiddenMesh = 9;
    ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(gate, true, 0).HasValue());
    EXPECT_EQ(gate.state.hiddenMesh, -1);
    gate.field08 = 99;
    gate.seen = false;
    ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(gate, true, 0).HasValue());
    EXPECT_EQ(gate.state.hiddenMesh, -1);
    gate.seen = true;
    gate.state.hiddenMesh = 4;
    ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(gate, true, 0).HasValue());
    EXPECT_EQ(gate.state.hiddenMesh, 0);
}
} // namespace
