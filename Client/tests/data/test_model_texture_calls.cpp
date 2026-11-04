#include "Data/Models/ModelTextureCalls.h"

#include "Data/Models/ModelFiles.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <ios>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Client::Data::Models::PlayerTextureCalls, ItemTextureCalls y PlayerFixedTextures: las llamadas de
// 0x60f7d0 y 0x611850 a 0x60d5f0, en su orden y con su carpeta, y lo que 0x60f7d0 sube a mano
// (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario").
namespace
{
using Client::Data::Models::ItemModelFiles;
using Client::Data::Models::ItemTextureCalls;
using Client::Data::Models::ModelFile;
using Client::Data::Models::ModelTextureCall;
using Client::Data::Models::ModelTextureSampling;
using Client::Data::Models::PlayerFixedTextures;
using Client::Data::Models::PlayerModelFiles;
using Client::Data::Models::PlayerSkeletonModel;
using Client::Data::Models::PlayerTextureCalls;
using Client::Data::Textures::Filter;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;

bool Calls(std::span<const ModelTextureCall> calls, std::uint16_t model)
{
    return std::ranges::any_of(calls, [model](const ModelTextureCall& call) { return call.model == model; });
}

TEST(ModelTextureCallsTest, ThePlayerLoaderCallsTwoHundredAndFourteenTimesWithPlayer)
{
    const std::span<const ModelTextureCall> calls = PlayerTextureCalls();

    ASSERT_EQ(calls.size(), 214u);
    for (const ModelTextureCall& call : calls)
    {
        EXPECT_EQ(call.folder, "Player\\") << std::hex << call.model;
    }
    EXPECT_EQ(calls.front().model, 0x2212);
    EXPECT_EQ(calls.back().model, 0x1033);
}

// El lazo de 0x1227 a 0x122a saltea el 0x1229 - 0x200 (0x60fa0b).
TEST(ModelTextureCallsTest, ThePlayerLoaderSkips0x1029)
{
    EXPECT_FALSE(Calls(PlayerTextureCalls(), 0x1029));
    EXPECT_TRUE(Calls(PlayerTextureCalls(), 0x1229));
    EXPECT_TRUE(Calls(PlayerTextureCalls(), 0x1028));
}

// 291 llamadas, casi todas con Item\; las otras seis, en su orden.
TEST(ModelTextureCallsTest, TheItemLoaderCallsTwoHundredAndNinetyOneTimes)
{
    // Con int y no std::uint16_t: armar el par desde los literales int haría la conversión
    // adentro de std::pair, y MSVC la avisa (C4242).
    const std::span<const ModelTextureCall> calls = ItemTextureCalls();
    std::vector<std::pair<int, std::string_view>> others;
    for (const ModelTextureCall& call : calls)
    {
        if (call.folder != "Item\\")
        {
            others.emplace_back(call.model, call.folder);
        }
    }

    ASSERT_EQ(calls.size(), 291u);
    EXPECT_EQ(calls.front().model, 0x1c22);
    EXPECT_EQ(calls.back().model, 0x1c37);
    const std::vector<std::pair<int, std::string_view>> expected{
        {0x1c16, "Skill\\"}, {0x1c17, "Skill\\"}, {0x224c, "Monster\\"},
        {0x1c15, "Skill\\"}, {0x1c2f, "Npc\\"},   {0xdb, "Item\\Bow\\"},
    };
    EXPECT_EQ(others, expected);
}

// Los Helper01 a 03 están en Player\, pero sus texturas son de Item\ (el lazo de 0x412, + 0x1800):
// con Player\, fairy.jpg, satan.jpg y unicon.jpg no están, y el original se cerraría (Q-C23).
TEST(ModelTextureCallsTest, TheHelpersTakeTheirTexturesFromItem)
{
    for (const std::uint16_t helper : {std::uint16_t{0x1c12}, std::uint16_t{0x1c13}, std::uint16_t{0x1c14}})
    {
        EXPECT_FALSE(Calls(PlayerTextureCalls(), helper)) << std::hex << helper;
        const auto item = std::ranges::find(ItemTextureCalls(), helper, &ModelTextureCall::model);
        ASSERT_NE(item, ItemTextureCalls().end()) << std::hex << helper;
        EXPECT_EQ(item->folder, "Item\\");
    }
}

// Cada modelo de las dos tablas tiene sus texturas en alguno de los dos, salvo el esqueleto, que no
// tiene mallas.
TEST(ModelTextureCallsTest, EveryModelButTheSkeletonGetsItsTextures)
{
    std::set<std::uint16_t> called;
    for (const std::span<const ModelTextureCall> calls : {PlayerTextureCalls(), ItemTextureCalls()})
    {
        for (const ModelTextureCall& call : calls)
        {
            called.insert(call.model);
        }
    }

    for (const std::span<const ModelFile> table : {PlayerModelFiles(), ItemModelFiles()})
    {
        for (const ModelFile& file : table)
        {
            EXPECT_EQ(called.contains(file.model), file.model != PlayerSkeletonModel) << std::hex << file.model;
        }
    }
}

// 0x2901 y 0x2600 en todas: repitiendo y sin filtro.
TEST(ModelTextureCallsTest, TheModelTexturesRepeatWithoutFilter)
{
    EXPECT_EQ(ModelTextureSampling.filter, Filter::Nearest);
    EXPECT_EQ(ModelTextureSampling.wrap, Wrap::Repeat);
}

// La piel, el pelo y las cuatro capas, sin filtro y sin repetir; 0x12d y 0x135 quedan vacíos.
TEST(ModelTextureCallsTest, TheFixedTexturesAreTheSkinTheHairAndTheRobes)
{
    std::vector<std::uint16_t> slots;
    for (const TextureSetEntry& entry : PlayerFixedTextures())
    {
        slots.push_back(entry.slot);
        EXPECT_EQ(entry.sampling.filter, Filter::Nearest) << entry.path;
        EXPECT_EQ(entry.sampling.wrap, Wrap::Clamp) << entry.path;
        EXPECT_TRUE(entry.path.starts_with("Player\\")) << entry.path;
    }

    const std::vector<std::uint16_t> expected{0x12e, 0x12f, 0x130, 0x131, 0x132, 0x133, 0x134,
                                              0x136, 0x137, 0x250, 0x251, 0x252, 0x257};
    ASSERT_EQ(slots, expected);
    EXPECT_EQ(PlayerFixedTextures()[11].path, "Player\\Robe03.tga");
    EXPECT_EQ(PlayerFixedTextures()[12].path, "Player\\DarklordRobe.tga");
}
} // namespace
