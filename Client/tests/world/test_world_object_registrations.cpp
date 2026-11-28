#include "WorldObjects/WorldObjectRegistrations.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace
{
using Client::WorldObjects::WorldObjectRegistrations;

// Catches lookup by pointer/null or by active == 1, and metadata erased when reserving a slot.
TEST(WorldObjectRegistrationsTest, UsesTheFirstZeroFlagAndPreservesAllOtherState)
{
    WorldObjectRegistrations registry;
    registry.nextIdentity = 123;
    for (std::size_t index = 0; index < registry.entries.size(); ++index)
    {
        registry.entries[index].active = 2;
        registry.entries[index].object = 900 + index;
        registry.entries[index].metadata = {1, 2, 3, 4, 5, 6, 7};
    }
    registry.entries[3].active = 0;
    registry.entries[17].active = 0;
    const WorldObjectRegistrations before = registry;
    Client::WorldObjects::RegisterWorldObject(registry, 42);
    for (std::size_t index = 0; index < registry.entries.size(); ++index)
    {
        SCOPED_TRACE(index);
        if (index == 3)
        {
            EXPECT_EQ(registry.entries[index].active, 1);
            EXPECT_EQ(registry.entries[index].object, 42u);
            EXPECT_EQ(registry.entries[index].metadata, before.entries[index].metadata);
        }
        else
        {
            EXPECT_EQ(registry.entries[index], before.entries[index]);
        }
    }
    EXPECT_EQ(registry.nextIdentity, 123u);
}

// Catches deduplication and rejection of null; neither exists in 0x606630.
TEST(WorldObjectRegistrationsTest, AcceptsNullAndRepeatedIdentities)
{
    WorldObjectRegistrations registry;
    Client::WorldObjects::RegisterWorldObject(registry, 0);
    Client::WorldObjects::RegisterWorldObject(registry, 42);
    Client::WorldObjects::RegisterWorldObject(registry, 42);
    EXPECT_EQ(registry.entries[0].active, 1);
    EXPECT_EQ(registry.entries[0].object, 0u);
    EXPECT_EQ(registry.entries[1].active, 1);
    EXPECT_EQ(registry.entries[1].object, 42u);
    EXPECT_EQ(registry.entries[2].active, 1);
    EXPECT_EQ(registry.entries[2].object, 42u);
    EXPECT_EQ(registry.entries[3].active, 0);
}

// Catches capacity 199/201, overwriting when full and side effects on the modern counter.
TEST(WorldObjectRegistrationsTest, FullTableMakesNoStores)
{
    WorldObjectRegistrations registry;
    for (std::size_t index = 0; index < 200; ++index)
    {
        Client::WorldObjects::RegisterWorldObject(registry, index + 1);
    }
    ASSERT_EQ(registry.entries[199].active, 1);
    ASSERT_EQ(registry.entries[199].object, 200u);
    const WorldObjectRegistrations before = registry;
    Client::WorldObjects::RegisterWorldObject(registry, 999);
    EXPECT_EQ(registry.entries, before.entries);
    EXPECT_EQ(registry.nextIdentity, before.nextIdentity);
}
} // namespace
