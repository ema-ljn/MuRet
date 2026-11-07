#include "World/WorldObjectRegistrations.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace
{
using Client::World::WorldObjectRegistrations;

// Detecta búsqueda por puntero/null o por active == 1, y borrado de metadata al reservar un slot.
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
    Client::World::RegisterWorldObject(registry, 42);
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

// Detecta deduplicación y rechazo de null, ninguno existe en 0x606630.
TEST(WorldObjectRegistrationsTest, AcceptsNullAndRepeatedIdentities)
{
    WorldObjectRegistrations registry;
    Client::World::RegisterWorldObject(registry, 0);
    Client::World::RegisterWorldObject(registry, 42);
    Client::World::RegisterWorldObject(registry, 42);
    EXPECT_EQ(registry.entries[0].active, 1);
    EXPECT_EQ(registry.entries[0].object, 0u);
    EXPECT_EQ(registry.entries[1].active, 1);
    EXPECT_EQ(registry.entries[1].object, 42u);
    EXPECT_EQ(registry.entries[2].active, 1);
    EXPECT_EQ(registry.entries[2].object, 42u);
    EXPECT_EQ(registry.entries[3].active, 0);
}

// Detecta capacidad 199/201, sobrescritura al llenarse y side effects en el contador moderno.
TEST(WorldObjectRegistrationsTest, FullTableMakesNoStores)
{
    WorldObjectRegistrations registry;
    for (std::size_t index = 0; index < 200; ++index)
    {
        Client::World::RegisterWorldObject(registry, index + 1);
    }
    ASSERT_EQ(registry.entries[199].active, 1);
    ASSERT_EQ(registry.entries[199].object, 200u);
    const WorldObjectRegistrations before = registry;
    Client::World::RegisterWorldObject(registry, 999);
    EXPECT_EQ(registry.entries, before.entries);
    EXPECT_EQ(registry.nextIdentity, before.nextIdentity);
}
} // namespace
