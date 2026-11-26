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

// 0x60f7d0's and 0x611850's calls to 0x60d5f0, in their order and with their folder, and what 0x60f7d0 uploads by
// hand (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario").
namespace
{
using Client::Data::Models::FenrirTextureCalls;
using Client::Data::Models::ItemModelFiles;
using Client::Data::Models::ItemTextureCalls;
using Client::Data::Models::ModelFile;
using Client::Data::Models::ModelTextureCall;
using Client::Data::Models::ModelTextureSampling;
using Client::Data::Models::PlayerFixedTextures;
using Client::Data::Models::PlayerModelFiles;
using Client::Data::Models::PlayerSkeletonModel;
using Client::Data::Models::PlayerTextureCalls;
using Client::Data::Models::SkillModelFiles;
using Client::Data::Models::SkillTextureCalls;
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

// The loop from 0x1227 to 0x122a skips 0x1229 - 0x200 (0x60fa0b).
TEST(ModelTextureCallsTest, ThePlayerLoaderSkips0x1029)
{
    EXPECT_FALSE(Calls(PlayerTextureCalls(), 0x1029));
    EXPECT_TRUE(Calls(PlayerTextureCalls(), 0x1229));
    EXPECT_TRUE(Calls(PlayerTextureCalls(), 0x1028));
}

// Almost all with Item\; the other six, in their order.
TEST(ModelTextureCallsTest, TheItemLoaderCallsTwoHundredAndNinetyOneTimes)
{
    // int and not std::uint16_t: building the pair from int literals would convert inside std::pair, and MSVC warns
    // (C4242).
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

// Helper01 to 03 are in Player\, but their textures come from Item\ (the loop of 0x412, + 0x1800): with Player\,
// fairy.jpg, satan.jpg and unicon.jpg are not found and the original would close (Q-C23).
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

// NativeSkillLoadTest checks the sweep of 0x616e20 and the separate calls for Gate01/02 and LaceArrow (0x11c),
// outside these tables.
TEST(ModelTextureCallsTest, TheTablesCoverTheModelsQueriedOutsideTheFinalSweep)
{
    std::set<std::uint16_t> called;
    for (const std::span<const ModelTextureCall> calls :
         {PlayerTextureCalls(), ItemTextureCalls(), FenrirTextureCalls(), SkillTextureCalls()})
    {
        for (const ModelTextureCall& call : calls)
        {
            called.insert(call.model);
        }
    }

    const std::set<std::uint16_t> outsideTheseTables{
        0xc1,  0xc2,  0xc3,  0xc6,  0xc8,  0xc9,  0xca,  0xcb,  0xcc,  0xcd,  0xce,  0xcf,  0xd0,  0xd1,
        0xd2,  0xd3,  0xd4,  0xd5,  0xd6,  0xd7,  0xd8,  0xd9,  0xdc,  0xdd,  0xde,  0xdf,  0xe0,  0xe1,
        0xe2,  0xe3,  0xe4,  0xe5,  0xe6,  0xec,  0xf1,  0xf5,  0xf6,  0xf7,  0xfe,  0x100, 0x105, 0x109,
        0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x10f, 0x117, 0x11c, 0x136, 0x138, 0x139, 0x13a, 0x13b};
    for (const std::span<const ModelFile> table : {PlayerModelFiles(), ItemModelFiles(), SkillModelFiles()})
    {
        for (const ModelFile& file : table)
        {
            EXPECT_EQ(called.contains(file.model),
                      file.model != PlayerSkeletonModel && !outsideTheseTables.contains(file.model))
                << std::hex << file.model;
        }
    }
}

// The 45 explicit lookups of 0x615700 after its 0x60d530(0x3b8): first 0x145, 0x146 and 0x147, then the horse's
// (0x6169a7), the raven's (0x6169c2), HellGate's (0x6169e0) and the Dinorant's (0x6169fb).
TEST(ModelTextureCallsTest, TheSkillLoaderAsksForTheDarkHorseFourthAndTheRavenFifth)
{
    const std::span<const ModelTextureCall> calls = SkillTextureCalls();

    std::vector<std::uint16_t> models;
    for (const ModelTextureCall& call : calls)
    {
        models.push_back(call.model);
        const std::string_view folder = call.model == 0x12b                            ? "Npc\\"
                                        : (call.model == 0x12f || call.model == 0x130) ? "Object31\\"
                                        : (call.model == 0x131 || call.model == 0x132 || call.model == 0x133 ||
                                           call.model == 0x134 || call.model == 0x13c)
                                            ? "Monster\\"
                                        : call.model == 0x137 ? "Effect\\"
                                        : call.model == 0x140 ? "Item\\"
                                                              : "Skill\\";
        EXPECT_EQ(call.folder, folder) << std::hex << call.model;
        const bool clamped = call.model == 0x113 || call.model == 0x11a || call.model == 0x11b;
        EXPECT_EQ(call.wrap, clamped ? Wrap::Clamp : Wrap::Repeat) << std::hex << call.model;
    }
    EXPECT_EQ(models, (std::vector<std::uint16_t>{0x145, 0x146, 0x147, 0xbf,  0xc0,  0xc5,  0x110, 0xfb,  0xfc,
                                                  0xfd,  0xff,  0x101, 0x102, 0x103, 0x104, 0x107, 0x108, 0x111,
                                                  0x112, 0x113, 0x114, 0x115, 0x116, 0x118, 0x119, 0x11a, 0x11b,
                                                  0x129, 0x12a, 0x12b, 0x12c, 0x12d, 0x12e, 0x12f, 0x130, 0x131,
                                                  0x132, 0x133, 0x134, 0x135, 0x13c, 0x13d, 0x13e, 0x137, 0x140}));
}

// 0x615f77, 0x615fab and 0x615fdc: the Fenrir, with Skill\, and 0x616010: their bolt 0x14d, with Effect\, before
// 0x615700's 0x60d530(0x3b8). 0x616181 adds Arrow_Spark's to the same block.
TEST(ModelTextureCallsTest, TheFenrirAskForTheirTexturesBeforeTheSkillBlock)
{
    std::vector<std::uint16_t> models;
    for (const ModelTextureCall& call : FenrirTextureCalls())
    {
        models.push_back(call.model);
        EXPECT_EQ(call.folder, call.model == 0x14d ? "Effect\\" : "Skill\\") << std::hex << call.model;
    }
    EXPECT_EQ(models, (std::vector<std::uint16_t>{0x14a, 0x14b, 0x14c, 0x14d, 0xf9}));
}

// 0x2901 and 0x2600 in all: repeating and unfiltered.
TEST(ModelTextureCallsTest, TheModelTexturesRepeatWithoutFilter)
{
    EXPECT_EQ(ModelTextureSampling.filter, Filter::Nearest);
    EXPECT_EQ(ModelTextureSampling.wrap, Wrap::Repeat);
}

// Unfiltered and clamped; 0x12d and 0x135 stay empty.
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
