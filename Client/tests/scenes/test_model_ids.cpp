#include "Assets/ModelIds.h"

#include "Data/Models/ModelFiles.h"
#include "Data/Models/PlayerModels.h"
#include "Maps/MapId.h"
#include "Scenes/WorldSpecialModels.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

namespace
{
namespace Assets = Client::Assets;
namespace Maps = Client::Maps;
using Client::Data::Models::ModelFile;

std::string FileIn(std::span<const ModelFile> files, std::uint16_t model)
{
    const auto file = std::ranges::find(files, model, &ModelFile::model);
    if (file == files.end())
        return {};
    return std::string(file->folder) + Client::Data::Models::ModelFileName(file->name, file->number);
}

std::string FileOfMap(std::int32_t map, std::uint16_t model)
{
    return FileIn(Client::Scenes::SpecialWorldModelsOf(static_cast<std::uint8_t>(map)).files, model);
}

TEST(ModelIdsTest, EachFaunaModelHasTheSlotOfTheOriginal)
{
    EXPECT_EQ(Assets::BirdModel, 0xad);
    EXPECT_EQ(Assets::ButterflyModel, 0xae);
    EXPECT_EQ(Assets::BatModel, 0xaf);
    EXPECT_EQ(Assets::RatModel, 0xb0);
    EXPECT_EQ(Assets::Bug01Model, 0xb1);
    EXPECT_EQ(Assets::Fish01Model, 0xb2);
    EXPECT_EQ(Assets::Bug02Model, 0xb2);
    EXPECT_EQ(Assets::Fish02Model, 0xb3);
    EXPECT_EQ(Assets::Fish04Model, 0xb5);
    EXPECT_EQ(Assets::Fish08Model, 0xb9);
    EXPECT_EQ(Assets::Fish09Model, 0xba);
    EXPECT_EQ(Assets::CrowModel, 0xbc);
    EXPECT_EQ(Assets::RidingSpearModel, 0x10f);
    EXPECT_EQ(Assets::ScorpionModel, 0x148);
    EXPECT_EQ(Assets::Monster32Model, 0x171);
    EXPECT_EQ(Assets::Monster34Model, 0x173);
}

TEST(ModelIdsTest, EachMapLoadsTheFileOfTheName)
{
    EXPECT_EQ(FileOfMap(Maps::LorenciaMap, Assets::BirdModel), "Object1\\Bird01.bmd");
    EXPECT_EQ(FileOfMap(Maps::NoriaMap, Assets::ButterflyModel), "Object1\\Butterfly01.bmd");
    EXPECT_EQ(FileOfMap(Maps::DungeonMap, Assets::BatModel), "Object2\\Bat01.bmd");
    EXPECT_EQ(FileOfMap(Maps::LostTowerMap, Assets::BatModel), "Object2\\Bat01.bmd");
    EXPECT_EQ(FileOfMap(Maps::DungeonMap, Assets::RatModel), "Object2\\Rat01.bmd");
    EXPECT_EQ(FileOfMap(Maps::ArenaMap, Assets::Bug01Model), "Object7\\Bug01.bmd");
    EXPECT_EQ(FileOfMap(Maps::LorenciaMap, Assets::Fish01Model), "Object1\\Fish01.bmd");
    EXPECT_EQ(FileOfMap(Maps::AtlansMap, Assets::Fish02Model), "Object8\\Fish02.bmd");
    EXPECT_EQ(FileOfMap(Maps::AtlansMap, Assets::Fish04Model), "Object8\\Fish04.bmd");
    EXPECT_EQ(FileOfMap(Maps::AtlansMap, Assets::Fish08Model), "Object8\\Fish08.bmd");
    EXPECT_EQ(FileOfMap(Maps::AtlansMap, Assets::Fish09Model), "Object8\\Fish09.bmd");
    EXPECT_EQ(FileOfMap(Maps::FirstBloodCastleMap, Assets::CrowModel), "Object12\\Crow01.bmd");
    EXPECT_EQ(FileOfMap(Maps::LastBloodCastleMap, Assets::CrowModel), "Object12\\Crow01.bmd");
    EXPECT_EQ(FileOfMap(Maps::CrywolfMap, Assets::ScorpionModel), "Object35\\scorpion.bmd");
}

TEST(ModelIdsTest, TarkanAndAidaLoadTheSecondBugInTheSlotOfTheFirstFish)
{
    EXPECT_EQ(FileOfMap(Maps::TarkanMap, Assets::Bug02Model), "Object9\\Bug02.bmd");
    EXPECT_EQ(FileOfMap(Maps::AidaMap, Assets::Bug02Model), "Object9\\Bug02.bmd");
}

TEST(ModelIdsTest, TheRidingSpearIsASkillModel)
{
    EXPECT_EQ(FileIn(Client::Data::Models::SkillModelFiles(), Assets::RidingSpearModel), "Skill\\RidingSpear01.bmd");
}

TEST(ModelIdsTest, AMonsterModelIsNamedByTheNumberOfItsFile)
{
    constexpr int Monster01Model = 0x152;
    EXPECT_EQ(Assets::Monster32Model, Monster01Model + 32 - 1);
    EXPECT_EQ(Assets::Monster34Model, Monster01Model + 34 - 1);
}
} // namespace
