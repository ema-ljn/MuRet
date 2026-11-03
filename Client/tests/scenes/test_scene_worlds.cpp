#include "Scenes/SceneWorlds.h"

#include "Data/WorldFolder.h"
#include "Scenes/SceneId.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>

// El mundo de cada escena como dato (CL5.4): el login usa el mapa 55 (0x6349b2, en 0x6349a0) y la
// selección el 54 (0x633f44, en 0x633f20). La escena se lo pasa al pase 3D (IWorldView), que lo
// dibuja desde la CL7 con cualquier mapa.
namespace
{
using Client::Data::OriginalMaps;
using Client::Data::WorldFolder;
using Client::Scenes::SceneId;
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

// La carga no tiene mundo; los de la carga del mundo y del juego los da el servidor.
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
