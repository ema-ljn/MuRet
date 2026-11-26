#include "Models/ItemGlow.h"

#include "Models/Shine.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

// Like 0x60b3d0 (06 § El modelo en el binario, Q-C97).
namespace
{
using Client::Models::GlowPass;
using Client::Models::ItemGlow;
using Client::Models::ItemGlowOf;
using Client::Models::QuiverGlowLevel;
using Client::Models::ShinePass;

std::vector<GlowPass> Shines(std::initializer_list<ShinePass> passes)
{
    std::vector<GlowPass> glow;
    for (const ShinePass pass : passes)
    {
        glow.push_back(GlowPass{pass});
    }
    return glow;
}

// 06's table, one row per level: the base color factor and the passes after the base, in order. 13 is the trace's:
// 141, 115 and 120. Q-C97: 14 and 15 do not glow.
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

// With the options byte (DAT_006c9dcc) below 4, the level is clamped to 2 · byte + 5.
TEST(ItemGlowTest, DetailCapsTheLevel)
{
    EXPECT_EQ(ItemGlowOf(13, false, false, 1).passes, Shines({ShinePass::Chrome}));
    EXPECT_EQ(ItemGlowOf(13, false, false, 2).passes, Shines({ShinePass::Chrome, ShinePass::Shiny}));
    EXPECT_EQ(ItemGlowOf(13, false, false, 0).passes, std::vector<GlowPass>{});
    EXPECT_EQ(ItemGlowOf(13, false, false, 4).passes,
              Shines({ShinePass::Chrome4, ShinePass::Shiny, ShinePass::Chrome}));
}

// An excellent item adds pass 0x42 (the mesh's texture, additive), and a set item 0x840. They are exclusive: 0x60b3d0
// returns after 0x42 (LAB_0060cb3b), so the set one only goes without it.
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
// 0x60b3d0 with bolts (0xa19) and arrows (0xa21): level 0 stays 0 and the rest go to 2 · level + 1; from 14 up, no
// passes (LAB_0060cb0f).
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
