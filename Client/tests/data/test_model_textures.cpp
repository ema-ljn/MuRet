#include "Data/Models/ModelTextures.h"

#include "Data/Textures/TextureSet.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

// Client::Data::Models: lo que dice el nombre de la textura de una malla.
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

// 0x60d5f0: la letra después del primer punto, con tolower, y no la extensión.
TEST(ModelTexturesTest, TheLoaderIsTheLetterAfterTheFirstDot)
{
    EXPECT_EQ(ModelTextureLoaderOf("hair_r.tga"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("Hair_R.TGA"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("skin_wizard_01.jpg"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("bossmap1_R.jpg.jpg"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("x.tga.jpg"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("x.jpg.tga"), TextureLoader::Ozj);
    // Otra letra cualquiera es el OZJ, también una extensión que no es de textura.
    EXPECT_EQ(ModelTextureLoaderOf("x.bmp"), TextureLoader::Ozj);
}

// Sin punto, el índice del punto queda en 0 (0x60d66c) y la letra es la segunda.
TEST(ModelTexturesTest, WithoutADotItIsTheSecondLetter)
{
    EXPECT_EQ(ModelTextureLoaderOf("ataque"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("aTaque"), TextureLoader::Ozt);
    EXPECT_EQ(ModelTextureLoaderOf("piedra"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf("a"), TextureLoader::Ozj);
    EXPECT_EQ(ModelTextureLoaderOf(""), TextureLoader::Ozj);
}

// 0x60d5f0: cuatro principios en minúscula, byte a byte, mandan la malla a la piel o al pelo.
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

// 0x459940: lo que va del primer '_' al primer punto, hasta cuatro letras R, H, S y N.
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

    // Sin punto, hasta el final.
    const std::optional<TextureScript> hidden = TextureScriptOf("Object01_H");
    ASSERT_TRUE(hidden.has_value());
    EXPECT_TRUE(hidden->hidden);
}

// Solo mira cuatro letras: lo que sigue no cuenta, aunque no sea del script (0x45999d).
TEST(ModelTexturesTest, OnlyFourLettersCount)
{
    const std::optional<TextureScript> script = TextureScriptOf("x_RRRRzz.jpg");
    ASSERT_TRUE(script.has_value());
    EXPECT_TRUE(script->blendMesh);
}

// Una letra que no es del script, en minúscula, o nada después del '_', y la malla queda sin script:
// el llamador (0x4f17f3) solo lo guarda si 0x459940 vuelve 1.
TEST(ModelTexturesTest, AnyOtherLetterLeavesNoScript)
{
    for (const std::string_view name :
         {"skin_barbarian_01.jpg", "player_s.jpg", "x_Rx.jpg", "x_.jpg", "x_", "nada.jpg", "", "x_DC.jpg"})
    {
        EXPECT_EQ(TextureScriptOf(name).has_value(), false) << name;
    }
}
} // namespace
