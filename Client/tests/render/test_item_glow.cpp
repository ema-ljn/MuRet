#include "Render/Models/ItemGlow.h"

#include "Render/Models/Shine.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

// Client::Render::Models::ItemGlowOf: los pases de brillo de una parte por su nivel, como 0x60b3d0
// (06 § El modelo en el binario, Q-C97).
namespace
{
using Client::Render::Models::GlowPass;
using Client::Render::Models::ItemGlow;
using Client::Render::Models::ItemGlowOf;
using Client::Render::Models::QuiverGlowLevel;
using Client::Render::Models::ShinePass;

std::vector<GlowPass> Shines(std::initializer_list<ShinePass> passes)
{
    std::vector<GlowPass> glow;
    for (const ShinePass pass : passes)
    {
        glow.push_back(GlowPass{pass});
    }
    return glow;
}

// La tabla del 06, una fila por nivel: el factor del color base y los pases después del base, en
// orden. El 13 es el de la traza: el 141, el 115 y el 120. Q-C97: el 14 y el 15 no brillan.
TEST(ItemGlowTest, LevelSetsThePasses)
{
    struct Row
    {
        std::uint8_t level;
        float factor;
        std::vector<GlowPass> passes;
    };
    const std::vector<Row> rows = {
        {0, 1.0f, {}},
        {2, 1.0f, {}},
        {3, 1.0f, {}},
        {6, 1.0f, {}},
        {7, 0.8f, Shines({ShinePass::Chrome})},
        {8, 0.8f, Shines({ShinePass::Chrome})},
        {9, 0.9f, Shines({ShinePass::Chrome, ShinePass::Shiny})},
        {10, 0.9f, Shines({ShinePass::Chrome, ShinePass::Shiny})},
        {11, 0.9f, Shines({ShinePass::Chrome2, ShinePass::Shiny, ShinePass::Chrome})},
        {12, 0.9f, Shines({ShinePass::Chrome2, ShinePass::Shiny, ShinePass::Chrome})},
        {13, 0.9f, Shines({ShinePass::Chrome4, ShinePass::Shiny, ShinePass::Chrome})},
        {14, 1.0f, {}},
        {15, 1.0f, {}},
    };
    for (const Row& row : rows)
    {
        const ItemGlow glow = ItemGlowOf(row.level, false, false);

        EXPECT_FLOAT_EQ(glow.baseFactor, row.factor) << "nivel " << int{row.level};
        EXPECT_EQ(glow.passes, row.passes) << "nivel " << int{row.level};
    }
}

// Con el byte de las opciones (DAT_006c9dcc) menor que 4, el nivel se recorta a 2 · byte + 5.
TEST(ItemGlowTest, DetailCapsTheLevel)
{
    EXPECT_EQ(ItemGlowOf(13, false, false, 1).passes, Shines({ShinePass::Chrome}));
    EXPECT_EQ(ItemGlowOf(13, false, false, 2).passes, Shines({ShinePass::Chrome, ShinePass::Shiny}));
    EXPECT_EQ(ItemGlowOf(13, false, false, 0).passes, std::vector<GlowPass>{});
    EXPECT_EQ(ItemGlowOf(13, false, false, 4).passes,
              Shines({ShinePass::Chrome4, ShinePass::Shiny, ShinePass::Chrome}));
}

// Un ítem excelente suma el pase 0x42 (la textura de la malla, aditiva), y uno de set el 0x840. Son
// excluyentes: 0x60b3d0 vuelve después del 0x42 (LAB_0060cb3b), así que el de set solo va sin él.
TEST(ItemGlowTest, ExcellentAndAncientAreExclusive)
{
    const std::vector<GlowPass> excellent = ItemGlowOf(0, true, false).passes;
    ASSERT_EQ(excellent.size(), 1u);
    EXPECT_FALSE(excellent[0].shine.has_value());

    EXPECT_EQ(ItemGlowOf(0, false, true).passes, Shines({ShinePass::Chrome3}));
    const std::vector<GlowPass> both = ItemGlowOf(7, true, true).passes;
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(both[0], GlowPass{ShinePass::Chrome});
    EXPECT_FALSE(both[1].shine.has_value());
}
// 0x60b3d0 con las saetas (0xa19) y las flechas (0xa21): el nivel 0 queda en 0 y los demás van a
// 2 · nivel + 1; de 14 para arriba, sin pases (LAB_0060cb0f).
TEST(ItemGlowTest, TheQuiverGlowsAtTwiceItsLevelPlusOne)
{
    EXPECT_EQ(QuiverGlowLevel(0), 0);
    EXPECT_EQ(QuiverGlowLevel(3), 7);
    EXPECT_EQ(QuiverGlowLevel(5), 11);
    EXPECT_EQ(QuiverGlowLevel(13), 27);
    EXPECT_TRUE(ItemGlowOf(27, false, false).passes.empty());
    EXPECT_FLOAT_EQ(ItemGlowOf(27, false, false).baseFactor, 1.0f);
}
} // namespace
