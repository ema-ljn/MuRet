#include "Scenes/CrywolfMissileDrawing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/ParticleOwner.h"
#include "World/Sprites.h"
#include "support/PlayerActors.h"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <memory>

namespace
{
TEST(CrywolfMissileDrawingTest, TheTailEmitsFourParticlesAndTenSpritesFromTheGlobalPose)
{
    for (const int type : {0xed, 0xee})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        Client::World::FrameSprites sprites;
        Client::World::ModelEffect effect;
        effect.type = static_cast<std::int16_t>(type);
        effect.transform.position = {1000, 2000, 300};
        effect.transform.scale = 1;
        std::array<Client::Render::Models::BoneTransform, 2> pose{Client::Render::Models::BoneTransform(0.0f),
                                                                  Client::Render::Models::BoneTransform(1.0f)};
        pose[1][3] = {100, 200, 300};
        Client::Render::Models::CrtRandom random(7);
        const auto drawn = Client::Scenes::DrawCrywolfMissileTail(effect, 19, *particles, sprites, random, pose, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 16).State());
        const auto out = particles->All();
        EXPECT_EQ(out[0].type, 0x5da);
        EXPECT_EQ(out[0].subtype, 8);
        EXPECT_EQ(out[1].type, 0x5da);
        EXPECT_EQ(out[1].subtype, 8);
        EXPECT_EQ(out[2].type, 0x5f2);
        EXPECT_EQ(out[2].subtype, 38);
        EXPECT_EQ(out[3].type, 0x5da);
        EXPECT_EQ(out[3].subtype, 9);
        EXPECT_EQ(out[3].light, (glm::vec3{1, 0.4f, 0}));
        EXPECT_EQ(out[2].typedOwner, (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::ModelEffect, 19}));
        const auto glows = sprites.Sprites();
        EXPECT_EQ(glows[0].texture, 0x5c6);
        EXPECT_EQ(glows[0].position, (glm::vec3{100, 201.5f, 300}));
        EXPECT_EQ(glows[0].scale, 3);
        EXPECT_EQ(glows[0].light, (glm::vec3{1, 0.4f, 0}));
        EXPECT_EQ(glows[9].position, (glm::vec3{235, 201.5f, 300}));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(glows[9].scale), 0x41319999u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(glows[9].light.r), 0x3dcccccbU);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(glows[9].light.g), 0x3d23d70eU);
        EXPECT_EQ(effect.light, glows[9].light);
        EXPECT_FALSE(glows[10].alive);
    }
}

TEST(CrywolfMissileDrawingTest, TheFullParticlePoolStillRunsTheSpritesAndTheEmptyScratchIsNativeBss)
{
    auto particles = std::make_unique<Client::World::Particles>();
    for (auto& p : particles->All())
        p.alive = true;
    Client::World::FrameSprites sprites;
    Client::World::ModelEffect effect;
    effect.type = 0xee;
    effect.transform.scale = 1;
    Client::Render::Models::CrtRandom random(7);
    const auto drawn = Client::Scenes::DrawCrywolfMissileTail(effect, 0, *particles, sprites, random, {}, 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), 7u);
    EXPECT_EQ(sprites.Sprites()[0].position, glm::vec3(0));
    EXPECT_TRUE(sprites.Sprites()[9].alive);
    EXPECT_EQ(effect.light, sprites.Sprites()[9].light);
}

TEST(CrywolfMissileDrawingTest, PartialParticleCapacityDoesNotChangeTheCallerOrTheFullSpritePool)
{
    constexpr std::array<int, 5> draws{0, 6, 12, 13, 16};
    for (std::size_t room = 0; room < draws.size(); ++room)
    {
        auto particles = std::make_unique<Client::World::Particles>();
        for (auto& p : particles->All())
            p.alive = true;
        for (std::size_t i = 0; i < room; ++i)
            particles->All()[i].alive = false;
        Client::World::FrameSprites sprites;
        for (auto& sprite : sprites.Sprites())
            sprite.alive = true;
        Client::World::ModelEffect effect;
        effect.type = 0xed;
        effect.transform.scale = 1;
        Client::Render::Models::CrtRandom random(7);
        const auto drawn = Client::Scenes::DrawCrywolfMissileTail(effect, 7, *particles, sprites, random, {}, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, draws[room]).State());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.r), 0x3dcccccbU);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.g), 0x3d23d70eU);
    }
}
} // namespace
