#include "World/ScepterGlow.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Sprites.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

// Client::World::CreateScepterGlow contra el caso 0x620 de 0x509340 del código real del main.exe, en 300
// casos al azar (tests/vectors/world55/scepter.txt, make_scepter_vectors.py): con 0x4ec110 y 0x5491e0
// reales, los cinco sprites que deja y el rand() al final, bit a bit.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::FrameSprites;
using Client::World::Sprite;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

std::uint32_t Hex(const std::string& word)
{
    return static_cast<std::uint32_t>(std::stoul(word, nullptr, 16));
}

float FromBits(const std::string& word)
{
    return std::bit_cast<float>(Hex(word));
}

TEST(ScepterGlowTest, ThreeHundredCasesMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "scepter.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "scepter 1");
    ASSERT_TRUE(std::getline(file, line));
    std::size_t cases = 0;
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream head(line);
        std::string word;
        head >> word;
        ASSERT_EQ(word, "case");
        head >> word;
        CrtRandom random(Hex(word));
        const auto next = [&head]()
        {
            std::string value;
            head >> value;
            return FromBits(value);
        };
        Client::Render::Models::OriginalMatrix bone{};
        for (float& value : bone)
        {
            value = next();
        }
        const float scale = next();
        const glm::vec3 origin{next(), next(), next()};

        FrameSprites sprites;
        Client::World::CreateScepterGlow(sprites, bone, scale, origin, random);
        for (std::size_t k = 0; k < Client::World::ScepterSpriteCount; ++k)
        {
            ASSERT_TRUE(std::getline(file, line));
            std::istringstream row(line);
            std::string texture;
            int blend = 0;
            row >> word >> texture >> blend;
            ASSERT_EQ(word, "sprite");
            const auto value = [&row]()
            {
                std::string bits;
                row >> bits;
                return Hex(bits);
            };
            const Sprite& sprite = sprites.Sprites()[k];
            const std::string where = "caso " + std::to_string(cases) + ", sprite " + std::to_string(k);
            EXPECT_TRUE(sprite.alive) << where;
            EXPECT_EQ(sprite.texture, Hex(texture)) << where;
            EXPECT_EQ(sprite.blend, blend) << where;
            EXPECT_EQ(Bits(sprite.scale), value()) << where << ": la escala";
            for (int c = 0; c < 3; ++c)
            {
                EXPECT_EQ(Bits(sprite.position[c]), value()) << where << ": la posición " << c;
            }
            for (int c = 0; c < 3; ++c)
            {
                EXPECT_EQ(Bits(sprite.light[c]), value()) << where << ": la luz " << c;
            }
            EXPECT_EQ(Bits(sprite.rotation), value()) << where << ": la rotación";
            EXPECT_EQ(Bits(sprite.alpha), value()) << where << ": el alpha";
        }
        EXPECT_FALSE(sprites.Sprites()[Client::World::ScepterSpriteCount].alive) << "no hay un sexto";
        ASSERT_TRUE(std::getline(file, line));
        std::istringstream after(line);
        after >> word >> word;
        EXPECT_EQ(random.State(), Hex(word)) << "el rand() al final del caso " << cases;
        ++cases;
        if (HasFailure())
        {
            break;
        }
    }
    EXPECT_EQ(cases, 300u);
}

// La tabla llena: 0x5491e0 no crea ninguno, pero el caso saca igual sus seis rand().
TEST(ScepterGlowTest, AFullTableTakesTheRandsAnyway)
{
    FrameSprites sprites;
    for (std::size_t k = 0; k < Client::World::SpriteCount; ++k)
    {
        ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0));
    }
    CrtRandom random(1);
    CrtRandom expected(1);
    for (int k = 0; k < 6; ++k)
    {
        (void)expected.Next();
    }
    Client::World::CreateScepterGlow(
        sprites, Client::Render::Models::OriginalMatrixOf(Client::Render::Models::BoneTransform(1.0f)), 1.0f,
        glm::vec3(0.0f), random);
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
