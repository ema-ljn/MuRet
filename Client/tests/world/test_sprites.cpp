#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>

// Client::Particles::FrameSprites (0x5491e0, 0x549510 and 0x549290's alpha; 06 § Los efectos del login).
namespace
{
using Client::Particles::FrameSprites;
using Client::Particles::Sprite;

// 0x5491e0 takes the first of the 1000 slots without the alive byte and sets it up with alpha at 1.
TEST(SpritesTest, TheFirstFreePlaceOfAThousand)
{
    FrameSprites sprites;
    for (std::size_t k = 0; k < Client::Particles::SpriteCount; ++k)
    {
        ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(static_cast<float>(k)), 1.0f, glm::vec3(1.0f), 0.0f, 0));
    }
    EXPECT_FALSE(sprites.Create(0x5ac, glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0));
    const Sprite& first = sprites.Sprites().front();
    EXPECT_TRUE(first.alive);
    EXPECT_EQ(first.texture, 0x5ac);
    EXPECT_EQ(first.alpha, 1.0f);
    EXPECT_EQ(sprites.Sprites()[7].position.x, 7.0f);
}

// 0x549510 sets +0x16c of the live ones.
TEST(SpritesTest, TheLiveOnesAreSeen)
{
    FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0));
    sprites.MarkSeen();
    EXPECT_TRUE(sprites.Sprites()[0].seen);
    EXPECT_FALSE(sprites.Sprites()[1].seen);
}

// 0x549290: seen, alpha rises 0.1 up to 1; unseen, it falls 0.1 down to 0.2.
TEST(SpritesTest, TheAlphaRisesWhenSeenAndFallsWhenNot)
{
    Sprite sprite;
    sprite.alpha = 0.5f;
    sprite.seen = true;
    Client::Particles::StepSpriteAlpha(sprite);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.alpha), std::bit_cast<std::uint32_t>(0.5f + 0.1f));
    sprite.alpha = 0.95f;
    Client::Particles::StepSpriteAlpha(sprite);
    EXPECT_EQ(sprite.alpha, 1.0f);
    sprite.seen = false;
    sprite.alpha = 0.25f;
    Client::Particles::StepSpriteAlpha(sprite);
    EXPECT_EQ(sprite.alpha, 0.2f);
}
} // namespace
