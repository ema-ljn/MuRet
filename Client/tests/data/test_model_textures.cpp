#include "Data/Models/ModelTextures.h"

#include "Data/Textures/TextureSet.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

namespace
{
using Client::Data::Models::HairTextureSlot;
using Client::Data::Models::HiddenTextureSlot;
using Client::Data::Models::ModelTextureLoaderOf;
using Client::Data::Models::SkinSlotOf;
using Client::Data::Models::SkinTextureSlot;
using Client::Data::Models::TextureScript;
using Client::Data::Models::TextureScriptOf;
using Client::Data::Textures::TextureLoader;

// 0x60d5f0: the letter after the first dot, with tolower, not the extension.
TEST(ModelTexturesTest, TheLoaderIsTheLetterAfterTheFirstDot)
{
    EXPECT_EQ(ModelTextureLoaderOf("hair_r.tga"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("Hair_R.TGA"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("skin_wizard_01.jpg"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("bossmap1_R.jpg.jpg"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("x.tga.jpg"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("x.jpg.tga"), TextureLoader::Ozj);
    // Any other letter is the OZJ, also an extension that is not a texture's.
    EXPECT_EQ(ModelTextureLoaderOf("x.bmp"), TextureLoader::Ozj);
}

// Without a dot the original reads the byte after the name's terminating 0 (0x60d68a leaves the index at the length
// and 0x60d6dd looks at the next one), whatever the file has. MuRet does not: without a dot it is the OZJ, like the
// original with that byte at 0 (Q-C107). No lab mesh has a name without a dot. With the dot last, the letter is the 0.
TEST(ModelTexturesTest, WithoutADotItIsAnOzj)
{
    EXPECT_EQ(ModelTextureLoaderOf("ataque"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("aTaque"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("piedra"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("t"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf(""), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("ataque."), TextureLoader::Ozj);
}

// 0x60d5f0: four lowercase prefixes, byte by byte, send the mesh to the skin or the hair.
TEST(ModelTexturesTest, SkinPrefixesGoToTheirSlots)
{
    EXPECT_EQ(SkinSlotOf("skin_barbarian_01.jpg"), std::optional{SkinTextureSlot});
    EXPECT_EQ(SkinSlotOf("level_man01.jpg"), std::optional{SkinTextureSlot});
    EXPECT_EQ(SkinSlotOf("hide.jpg"), std::optional{HiddenTextureSlot});
    EXPECT_EQ(SkinSlotOf("hair_r.tga"), std::optional{HairTextureSlot});
    EXPECT_EQ(SkinTextureSlot, 0x12e);
    EXPECT_EQ(HiddenTextureSlot, 0x12d);
    EXPECT_EQ(HairTextureSlot, 0x137);
    for (const std::string_view name : {"Hair_r.tga", "Skin_01.jpg", "LEVEL.jpg", "sk.jpg", "lev", "armor.jpg", ""})
    {
        EXPECT_EQ(SkinSlotOf(name), std::nullopt) << name;
    }
}

// 0x459940: from the first '_' to the first dot, up to four letters R, H, S and N.
TEST(ModelTexturesTest, TheScriptIsBetweenTheUnderscoreAndTheDot)
{
    const std::optional<TextureScript> blend = TextureScriptOf("Monster_R.jpg");
    ASSERT_TRUE(blend.has_value());
    EXPECT_TRUE(blend->blendMesh);
    EXPECT_FALSE(blend->hidden);
    EXPECT_FALSE(blend->scrollUv);
    EXPECT_FALSE(blend->withoutShine);

    const std::optional<TextureScript> all = TextureScriptOf("x_NSHR.jpg");
    ASSERT_TRUE(all.has_value());
    EXPECT_TRUE(all->blendMesh && all->hidden && all->scrollUv && all->withoutShine);

    // Without a dot, to the end.
    const std::optional<TextureScript> hidden = TextureScriptOf("Object01_H");
    ASSERT_TRUE(hidden.has_value());
    EXPECT_TRUE(hidden->hidden);
}

// What follows does not count, even if it is not part of the script (0x45999d).
TEST(ModelTexturesTest, OnlyFourLettersCount)
{
    const std::optional<TextureScript> script = TextureScriptOf("x_RRRRzz.jpg");
    ASSERT_TRUE(script.has_value());
    EXPECT_TRUE(script->blendMesh);
}

// The caller (0x4f17f3) keeps it only if 0x459940 returns 1.
TEST(ModelTexturesTest, AnyOtherLetterLeavesNoScript)
{
    for (const std::string_view name :
         {"skin_barbarian_01.jpg", "player_s.jpg", "x_Rx.jpg", "x_.jpg", "x_", "nada.jpg", "", "x_DC.jpg"})
    {
        EXPECT_EQ(TextureScriptOf(name).has_value(), false) << name;
    }
}
} // namespace
