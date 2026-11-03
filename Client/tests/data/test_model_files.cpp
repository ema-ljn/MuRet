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

// Client::Data::Models: el nombre del archivo de un modelo, los que no pueden faltar y la lectura desde
// un Data\ sintético en una carpeta temporal.
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

// El esqueleto del personaje, como lo pide 0x60dbe0: el modelo 0x20a, "Data\\Player\\Player.bmd".
constexpr ModelFile PlayerSkeleton{0x20a, "Player\\", "Player", -1};
// Un casco, como los pide 0x60dbe0: "Data\\Player\\HelmMale01.bmd".
constexpr ModelFile FirstMaleHelm{0x20b, "Player\\", "HelmMale", 1};

// Escribe `bytes` en `root / relative`, con sus carpetas.
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

// El modelo de los tests sin mallas: así viene el esqueleto (Q-C88).
Model SampleSkeleton()
{
    Model skeleton = SampleModel();
    skeleton.meshes.clear();
    return skeleton;
}

// 0x60da30: -1 sin número, uno de un dígito con un 0 adelante, y los demás tal cual. Con signo, como
// el %d del original: -2 da "0-2".
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

// Q-C23: solo Monster, Player y PlayerTest cierran el juego si faltan, comparados byte a byte como en
// 0x60daec. El número no cuenta: Monster50.bmd es "Monster".
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

// El que falta dice la ruta como la pide el original, con Data\ y las barras de Windows.
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

// Sin la carpeta de datos no se puede buscar: falta, con lo que dijo el sistema.
TEST(ModelFilesTest, AFolderThatCannotBeSearchedSaysWhy)
{
    const TemporaryFolder data("model-files-no-root");

    const auto loaded = LoadModelFile(data.Path() / "no-existe", FirstMaleHelm);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error().kind, ModelLoadErrorKind::Missing);
    EXPECT_TRUE(loaded.Error().system);
}

// Q-C88: el esqueleto tiene que venir sin mallas. Con mallas es un error; otro modelo con mallas, no,
// y PlayerTest tampoco es el esqueleto.
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

// El esqueleto sin mallas carga, con sus huesos y sus acciones.
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

// La copia del Data\ del laboratorio tiene player\ en minúscula: se busca como en Windows
// (ResolveDataPath).
TEST(ModelFilesTest, FindsTheFileInAnyCase)
{
    const TemporaryFolder data("model-files-case");
    WriteFile(data.Path(), "player/helmmale01.BMD", EncryptedBmd(BmdContent(SampleModel())));

    const auto helm = LoadModelFile(data.Path(), FirstMaleHelm);

    ASSERT_TRUE(helm.HasValue());
    EXPECT_EQ(helm.Value().name, SampleModel().name);
    ASSERT_EQ(helm.Value().meshes.size(), 1u);
}

// Uno que está y LoadBmd no acepta: no se lee, con lo que dijo LoadBmd.
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
