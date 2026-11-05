#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>

// Client::World::FrameSprites (0x5491e0, 0x549510 y el alpha de 0x549290; 06 § Los efectos del login).
namespace
{
using Client::World::FrameSprites;
using Client::World::Sprite;

// 0x5491e0 toma el primer lugar sin el byte vivo de los 1.000 y lo arma con el alpha en 1.
TEST(SpritesTest, TheFirstFreePlaceOfAThousand)
{
    FrameSprites sprites;
    for (std::size_t k = 0; k < Client::World::SpriteCount; ++k)
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

// 0x549510 prende +0x16c de los vivos.
TEST(SpritesTest, TheLiveOnesAreSeen)
{
    FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0));
    sprites.MarkSeen();
    EXPECT_TRUE(sprites.Sprites()[0].seen);
    EXPECT_FALSE(sprites.Sprites()[1].seen);
}

// 0x549290: visto, el alpha sube 0,1 con tope 1; sin ver, baja 0,1 con piso 0,2.
TEST(SpritesTest, TheAlphaRisesWhenSeenAndFallsWhenNot)
{
    Sprite sprite;
    sprite.alpha = 0.5f;
    sprite.seen = true;
    Client::World::StepSpriteAlpha(sprite);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.alpha), std::bit_cast<std::uint32_t>(0.5f + 0.1f));
    sprite.alpha = 0.95f;
    Client::World::StepSpriteAlpha(sprite);
    EXPECT_EQ(sprite.alpha, 1.0f);
    sprite.seen = false;
    sprite.alpha = 0.25f;
    Client::World::StepSpriteAlpha(sprite);
    EXPECT_EQ(sprite.alpha, 0.2f);
}
} // namespace
