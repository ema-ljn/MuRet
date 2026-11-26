#include "Data/Models/ModelFiles.h"

#include "Data/DataError.h"
#include "Data/Models/Model.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Models::IsRequiredModel;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::Model;
using Client::Data::Models::ModelFile;
using Client::Data::Models::ModelFileName;
using Client::Data::Models::ModelLoadErrorKind;
using Client::Tests::BmdContent;
using Client::Tests::EncryptedBmd;
using Client::Tests::SampleModel;
using Client::Tests::TemporaryFolder;

namespace fs = std::filesystem;

// As 0x60dbe0 asks for it: model 0x20a, "Data\\Player\\Player.bmd".
constexpr ModelFile PlayerSkeleton{0x20a, "Player\\", "Player", -1};
// As 0x60dbe0 asks for it: "Data\\Player\\HelmMale01.bmd".
constexpr ModelFile FirstMaleHelm{0x20b, "Player\\", "HelmMale", 1};

void WriteFile(const fs::path& root, const fs::path& relative, std::span<const std::byte> bytes)
{
    const fs::path path = root / relative;
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    for (const std::byte value : bytes)
    {
        file.put(static_cast<char>(value));
    }
    ASSERT_TRUE(file.good()) << "no se pudo escribir " << relative.generic_string();
}

// The skeleton comes that way (Q-C88).
Model SampleSkeleton()
{
    Model skeleton = SampleModel();
    skeleton.meshes.clear();
    return skeleton;
}

// 0x60da30: -1 without a number, one digit with a leading 0, the rest as is. Signed, like the original's %d: -2
// gives "0-2".
TEST(ModelFileNameTest, FollowsTheThreeFormats)
{
    EXPECT_EQ(ModelFileName("Player", -1), "Player.bmd");
    EXPECT_EQ(ModelFileName("HelmMale", 0), "HelmMale00.bmd");
    EXPECT_EQ(ModelFileName("HelmMale", 9), "HelmMale09.bmd");
    EXPECT_EQ(ModelFileName("HelmMale", 10), "HelmMale10.bmd");
    EXPECT_EQ(ModelFileName("ArmorMale", 17), "ArmorMale17.bmd");
    EXPECT_EQ(ModelFileName("Monster", 50), "Monster50.bmd");
    EXPECT_EQ(ModelFileName("Wing", -2), "Wing0-2.bmd");
}

// Q-C23: compared byte by byte as at 0x60daec. The number does not count: Monster50.bmd is "Monster".
TEST(ModelFilesTest, OnlyMonsterPlayerAndPlayerTestAreRequired)
{
    EXPECT_TRUE(IsRequiredModel("Monster"));
    EXPECT_TRUE(IsRequiredModel("Player"));
    EXPECT_TRUE(IsRequiredModel("PlayerTest"));
    for (const std::string_view name :
         {"player", "PLAYER", "Players", "Playe", "playerTest", "Monster50", "HelmMale", "Logo03", ""})
    {
        EXPECT_FALSE(IsRequiredModel(name)) << name;
    }
}

// As the original asks for it, with Data\ and Windows separators.
TEST(ModelFilesTest, AMissingFileSaysItsPath)
{
    const TemporaryFolder data("model-files-missing");

    const auto loaded = LoadModelFile(data.Path(), FirstMaleHelm);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error().kind, ModelLoadErrorKind::Missing);
    EXPECT_EQ(loaded.Error().path, "Data\\Player\\HelmMale01.bmd");
    EXPECT_FALSE(loaded.Error().data.has_value());
    EXPECT_FALSE(loaded.Error().system);
}

// Missing, with what the system said.
TEST(ModelFilesTest, AFolderThatCannotBeSearchedSaysWhy)
{
    const TemporaryFolder data("model-files-no-root");

    const auto loaded = LoadModelFile(data.Path() / "no-existe", FirstMaleHelm);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error().kind, ModelLoadErrorKind::Missing);
    EXPECT_TRUE(loaded.Error().system);
}

// Q-C88: another model with meshes is fine, and PlayerTest is not the skeleton either.
TEST(ModelFilesTest, TheSkeletonWithMeshesIsAnError)
{
    const TemporaryFolder data("model-files-skeleton");
    const std::vector<std::byte> withMeshes = EncryptedBmd(BmdContent(SampleModel()));
    WriteFile(data.Path(), "Player/Player.bmd", withMeshes);
    WriteFile(data.Path(), "Player/PlayerTest.bmd", withMeshes);
    WriteFile(data.Path(), "Player/HelmMale01.bmd", withMeshes);

    const auto skeleton = LoadModelFile(data.Path(), PlayerSkeleton);
    const auto test = LoadModelFile(data.Path(), {0x20a, "Player\\", "PlayerTest", -1});
    const auto helm = LoadModelFile(data.Path(), FirstMaleHelm);

    ASSERT_FALSE(skeleton.HasValue());
    EXPECT_EQ(skeleton.Error().kind, ModelLoadErrorKind::SkeletonWithMeshes);
    EXPECT_EQ(skeleton.Error().path, "Data\\Player\\Player.bmd");
    EXPECT_TRUE(test.HasValue());
    EXPECT_TRUE(helm.HasValue());
}

TEST(ModelFilesTest, TheSkeletonWithoutMeshesLoads)
{
    const TemporaryFolder data("model-files-bones");
    WriteFile(data.Path(), "Player/Player.bmd", EncryptedBmd(BmdContent(SampleSkeleton())));

    const auto skeleton = LoadModelFile(data.Path(), PlayerSkeleton);

    ASSERT_TRUE(skeleton.HasValue());
    EXPECT_TRUE(skeleton.Value().meshes.empty());
    EXPECT_EQ(skeleton.Value().bones.size(), 3u);
    EXPECT_EQ(skeleton.Value().actions.size(), 2u);
}

// The lab's Data\ copy has player\ lowercase: it is found as on Windows (ResolveDataPath).
TEST(ModelFilesTest, FindsTheFileInAnyCase)
{
    const TemporaryFolder data("model-files-case");
    WriteFile(data.Path(), "player/helmmale01.BMD", EncryptedBmd(BmdContent(SampleModel())));

    const auto helm = LoadModelFile(data.Path(), FirstMaleHelm);

    ASSERT_TRUE(helm.HasValue());
    EXPECT_EQ(helm.Value().name, SampleModel().name);
    ASSERT_EQ(helm.Value().meshes.size(), 1u);
}

TEST(ModelFilesTest, AnUnreadableFileSaysWhatLoadBmdSaid)
{
    const TemporaryFolder data("model-files-short");
    const std::vector<std::byte> shortFile = {std::byte{'B'}, std::byte{'M'}, std::byte{'D'}};
    WriteFile(data.Path(), "Player/HelmMale01.bmd", shortFile);

    const auto helm = LoadModelFile(data.Path(), FirstMaleHelm);

    ASSERT_FALSE(helm.HasValue());
    EXPECT_EQ(helm.Error().kind, ModelLoadErrorKind::Unreadable);
    EXPECT_EQ(helm.Error().path, "Data\\Player\\HelmMale01.bmd");
    EXPECT_EQ(helm.Error().data, std::optional{DataError::FileTooShort});
}
} // namespace
