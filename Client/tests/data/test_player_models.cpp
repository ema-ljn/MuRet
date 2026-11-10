#include "Data/Models/PlayerModels.h"

#include "Data/Models/ModelFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::Data::Models: los modelos que cargan 0x60dbe0 y 0x60ff50, con los números de la CL6.1.
namespace
{
using Client::Data::Models::FirstItemModel;
using Client::Data::Models::HelmsWithMask;
using Client::Data::Models::ItemModel;
using Client::Data::Models::ItemModelFiles;
using Client::Data::Models::MaskHelmOffset;
using Client::Data::Models::MeshesWithoutShine;
using Client::Data::Models::ModelFile;
using Client::Data::Models::ModelMesh;
using Client::Data::Models::ModelTableSize;
using Client::Data::Models::PlayerModelFiles;
using Client::Data::Models::PlayerSkeletonModel;
using Client::Data::Models::SkillModelFiles;

// Las filas de `files` que se llaman `name`.
std::vector<ModelFile> Named(std::span<const ModelFile> files, std::string_view name)
{
    std::vector<ModelFile> named;
    std::ranges::copy_if(files, std::back_inserter(named), [name](const ModelFile& file) { return file.name == name; });
    return named;
}

// El esqueleto es la primera carga, sin número (0x60e90d).
TEST(PlayerModelsTest, ThePlayerIsModel0x20a)
{
    ASSERT_FALSE(PlayerModelFiles().empty());
    const ModelFile& player = PlayerModelFiles().front();
    EXPECT_EQ(player.model, PlayerSkeletonModel);
    EXPECT_EQ(player.model, 0x20a);
    EXPECT_EQ(player.folder, "Player\\");
    EXPECT_EQ(player.name, "Player");
    EXPECT_EQ(player.number, -1);
}

// 213 cargas, la última Shadow01.bmd en el 0x20b (0x60f2f4).
TEST(PlayerModelsTest, ShadowIsTheLastOfTwoHundredAndThirteen)
{
    ASSERT_EQ(PlayerModelFiles().size(), 213u);
    const ModelFile& shadow = PlayerModelFiles().back();
    EXPECT_EQ(shadow.model, 0x20b);
    EXPECT_EQ(shadow.name, "Shadow");
    EXPECT_EQ(shadow.number, 1);
}

// 0x60ff50: el 0x212 más 512 por grupo más el índice.
TEST(PlayerModelsTest, ItemModelsAreGroupTimes512PlusIndex)
{
    EXPECT_EQ(ItemModel(0, 0), FirstItemModel);
    EXPECT_EQ(ItemModel(0, 0), 0x212);
    EXPECT_EQ(ItemModel(7, 0), 0x1012);
    EXPECT_EQ(ItemModel(13, 5), 0x212 + 13 * 512 + 5);
    // Las partes de las clases son el grupo 16.
    EXPECT_EQ(ItemModel(16, 0), 0x2212);
}

// Las partes de cada clase, de la 1 a la 5, y las de la segunda clase, de la 1 a la 3: el casco
// 0x2212 + n − 1, la armadura 8 después, y así de a 8.
TEST(PlayerModelsTest, ClassPartsAreOneToFive)
{
    constexpr std::array<std::string_view, 5> Parts = {"HelmClass", "ArmorClass", "PantClass", "GloveClass",
                                                       "BootClass"};
    for (std::size_t part = 0; part < Parts.size(); ++part)
    {
        const std::vector<ModelFile> first = Named(PlayerModelFiles(), Parts[part]);
        ASSERT_EQ(first.size(), 5u) << Parts[part];
        for (std::size_t k = 0; k < first.size(); ++k)
        {
            EXPECT_EQ(first[k].number, static_cast<int>(k + 1)) << Parts[part];
            EXPECT_EQ(first[k].model, ItemModel(16, static_cast<std::uint16_t>(8 * part + k))) << Parts[part];
        }
        const std::string secondName = std::string(Parts[part]) + "2";
        const std::vector<ModelFile> second = Named(PlayerModelFiles(), secondName);
        ASSERT_EQ(second.size(), 3u) << secondName;
        EXPECT_EQ(second[0].model, ItemModel(16, static_cast<std::uint16_t>(8 * part + 5))) << secondName;
    }
}

// HelmMale N es el índice N − 1 del grupo 7, y ArmorMale N el del grupo 8 (0x60eaea..).
TEST(PlayerModelsTest, HelmMaleOneIsItemZero)
{
    const std::vector<ModelFile> helms = Named(PlayerModelFiles(), "HelmMale");
    const std::vector<ModelFile> armors = Named(PlayerModelFiles(), "ArmorMale");
    ASSERT_FALSE(helms.empty());
    ASSERT_FALSE(armors.empty());
    for (const ModelFile& helm : helms)
    {
        EXPECT_EQ(helm.model, ItemModel(7, static_cast<std::uint16_t>(helm.number - 1))) << helm.number;
    }
    for (const ModelFile& armor : armors)
    {
        EXPECT_EQ(armor.model, ItemModel(8, static_cast<std::uint16_t>(armor.number - 1))) << armor.number;
    }
    EXPECT_EQ(helms.front().number, 1);
}

// Cada fila es un modelo de la tabla del original, y ninguno se carga dos veces.
TEST(PlayerModelsTest, EveryRowIsAModelOfTheTable)
{
    for (const std::span<const ModelFile> files : {PlayerModelFiles(), ItemModelFiles()})
    {
        std::set<std::uint16_t> models;
        for (const ModelFile& file : files)
        {
            EXPECT_LT(file.model, ModelTableSize) << file.name << " " << file.number;
            EXPECT_TRUE(file.folder == "Player\\" || file.folder == "Item\\") << file.name;
            EXPECT_TRUE(models.insert(file.model).second) << "el " << file.model << " se repite";
        }
    }
}

// 0x615700: caballo/cuervo, Gate01/02 de Object12 (0x616304/0x61631a) y los tres de Skill que
// piden texturas antes del caballo. El orden de los archivos y el de sus texturas son distintos.
TEST(PlayerModelsTest, TheSkillLoaderKeepsItsNativeOrderAndTheWorldGates)
{
    const std::span<const ModelFile> files = SkillModelFiles();

    ASSERT_EQ(files.size(), 7u);
    std::vector<std::uint16_t> models;
    std::vector<std::string_view> names;
    for (const ModelFile& file : files)
    {
        models.push_back(file.model);
        names.push_back(file.name);
    }
    EXPECT_EQ(models, (std::vector<std::uint16_t>{0xbf, 0xc0, 0x10c, 0x10d, 0x145, 0x146, 0x147}));
    EXPECT_EQ(names, (std::vector<std::string_view>{"DarkHorse", "DarkSpirit", "Gate", "Gate", "elf_skill", "WaveForce",
                                                    "deathsp_eff"}));
    for (const ModelFile& file : files)
    {
        const bool gate = file.model == 0x10c || file.model == 0x10d;
        EXPECT_EQ(file.folder, gate ? "Object12\\" : "Skill\\") << file.name;
        EXPECT_EQ(file.number, gate ? static_cast<int>(file.model - 0x10b) : -1) << file.name;
    }
}

// 243 cargas de ítems, todos desde el 0x212. La primera es Sword01, el índice 0 del grupo 0.
TEST(PlayerModelsTest, TheItemLoaderStartsWithTheSwords)
{
    ASSERT_EQ(ItemModelFiles().size(), 243u);
    const ModelFile& sword = ItemModelFiles().front();
    EXPECT_EQ(sword.model, ItemModel(0, 0));
    EXPECT_EQ(sword.folder, "Item\\");
    EXPECT_EQ(sword.name, "Sword");
    EXPECT_EQ(sword.number, 1);
    EXPECT_TRUE(
        std::ranges::all_of(ItemModelFiles(), [](const ModelFile& file) { return file.model >= FirstItemModel; }));
}

// Las cinco mallas que 0x60ff50 marca al final son de ítems suyos: la 1 de Spear01, Sword11, Mace07 y
// MagicBox07 y la 2 de Staff07.
TEST(PlayerModelsTest, The0x60ff50MarksFiveMeshesOfItsItems)
{
    std::vector<std::string> marked;
    for (const ModelMesh& mesh : MeshesWithoutShine)
    {
        const auto file = std::ranges::find(ItemModelFiles(), mesh.model, &ModelFile::model);
        ASSERT_NE(file, ItemModelFiles().end()) << mesh.model;
        marked.push_back(std::string(file->name) + std::to_string(file->number) + ":" + std::to_string(mesh.mesh));
    }

    EXPECT_EQ(marked, (std::vector<std::string>{"Spear1:1", "Sword11:1", "Staff7:2", "Mace7:1", "MagicBox7:1"}));
}

// La máscara de cada casco de la clase 4 es el MaskHelmMale de su número, cargado por 0x60dbe0: los
// HelmMale01, 06, 07, 09 y 10 con MaskHelmMale01, 06, 07, 09 y 10.
TEST(PlayerModelsTest, EachMaskIsTheMaskHelmOfItsHelm)
{
    for (const std::uint16_t helm : HelmsWithMask)
    {
        const auto helmFile = std::ranges::find(PlayerModelFiles(), helm, &ModelFile::model);
        const auto maskFile =
            std::ranges::find(PlayerModelFiles(), static_cast<std::uint16_t>(helm + MaskHelmOffset), &ModelFile::model);
        ASSERT_NE(helmFile, PlayerModelFiles().end()) << helm;
        ASSERT_NE(maskFile, PlayerModelFiles().end()) << helm;
        EXPECT_EQ(helmFile->name, "HelmMale");
        EXPECT_EQ(maskFile->name, "MaskHelmMale");
        EXPECT_EQ(maskFile->number, helmFile->number);
    }
}
} // namespace
