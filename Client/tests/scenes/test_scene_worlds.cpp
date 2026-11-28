#include "Scenes/SceneWorlds.h"

#include "Maps/WorldFolder.h"
#include "Kernel/SceneId.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>

// Each scene's world as data (CL5.4): the login uses map 55 (0x6349b2, in 0x6349a0) and the selection 54 (0x633f44,
// in 0x633f20). The scene passes it to the 3D pass (IWorldView), which draws it with any map since CL7.
namespace
{
using Client::Kernel::SceneId;
using Client::Maps::OriginalMaps;
using Client::Maps::WorldFolder;
using Client::Scenes::SceneWorlds;
using Client::Scenes::WorldOf;

TEST(SceneWorldsTest, TheLoginIsTheMap55)
{
    ASSERT_TRUE(WorldOf(SceneId::Login).has_value());
    EXPECT_EQ(WorldOf(SceneId::Login)->map, 55);
    EXPECT_EQ(WorldFolder(WorldOf(SceneId::Login)->map), "World56");
}

TEST(SceneWorldsTest, TheSelectionIsTheMap54)
{
    ASSERT_TRUE(WorldOf(SceneId::CharacterSelect).has_value());
    EXPECT_EQ(WorldOf(SceneId::CharacterSelect)->map, 54);
    EXPECT_EQ(WorldFolder(WorldOf(SceneId::CharacterSelect)->map), "World55");
}

// The loading has no world; the world loading's and the game's come from the server.
TEST(SceneWorldsTest, TheOtherScenesHaveNoFixedWorld)
{
    EXPECT_EQ(WorldOf(SceneId::Loading), std::nullopt);
    EXPECT_EQ(WorldOf(SceneId::WorldLoading), std::nullopt);
    EXPECT_EQ(WorldOf(SceneId::Game), std::nullopt);
}

TEST(SceneWorldsTest, EveryWorldIsAnOriginalMap)
{
    for (const auto& entry : SceneWorlds)
    {
        EXPECT_NE(std::find(OriginalMaps.begin(), OriginalMaps.end(), entry.world.map), OriginalMaps.end())
            << int{entry.world.map};
    }
}
} // namespace
