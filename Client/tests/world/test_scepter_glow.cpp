#include "Actors/Drawing/ScepterGlow.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Particles/Sprites.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Client::Actors::Drawing::CreateScepterGlow against case 0x620 of 0x509340 in the real main.exe code, in 300 random
// cases (tests/vectors/world55/scepter.txt, make_scepter_vectors.py): with the real 0x4ec110 and 0x5491e0, the five
// sprites it leaves and the rand() at the end, bit by bit.
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::FrameSprites;
using Client::Particles::Sprite;

using Client::Math::Bits;

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
        Client::Math::OriginalMatrix bone{};
        for (float& value : bone)
        {
            value = next();
        }
        const float scale = next();
        const glm::vec3 origin{next(), next(), next()};

        FrameSprites sprites;
        Client::Actors::Drawing::CreateScepterGlow(sprites, bone, scale, origin, random);
        for (std::size_t k = 0; k < Client::Actors::Drawing::ScepterSpriteCount; ++k)
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
        EXPECT_FALSE(sprites.Sprites()[Client::Actors::Drawing::ScepterSpriteCount].alive) << "no hay un sexto";
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

// Full table: 0x5491e0 creates none, but the case takes its six rand() anyway.
TEST(ScepterGlowTest, AFullTableTakesTheRandsAnyway)
{
    FrameSprites sprites;
    for (std::size_t k = 0; k < Client::Particles::SpriteCount; ++k)
    {
        ASSERT_TRUE(sprites.Create(0x5ac, glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0));
    }
    CrtRandom random(1);
    CrtRandom expected(1);
    for (int k = 0; k < 6; ++k)
    {
        (void)expected.Next();
    }
    Client::Actors::Drawing::CreateScepterGlow(
        sprites, Client::Math::OriginalMatrixOf(Client::Math::BoneTransform(1.0f)), 1.0f, glm::vec3(0.0f), random);
    EXPECT_EQ(random.State(), expected.State());
}

// The six rand() a frame asks for, after those of `random`.
CrtRandom After(std::uint32_t seed, int frames)
{
    CrtRandom random(seed);
    for (int k = 0; k < 6 * frames; ++k)
    {
        (void)random.Next();
    }
    return random;
}

std::vector<Sprite> GlowOf(FrameSprites& sprites)
{
    std::vector<Sprite> glow;
    for (const Sprite& sprite : sprites.Sprites())
    {
        if (sprite.alive)
        {
            glow.push_back(sprite);
        }
    }
    return glow;
}

void ExpectSameGlow(const std::vector<Sprite>& actual, const std::vector<Sprite>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t k = 0; k < actual.size(); ++k)
    {
        EXPECT_EQ(actual[k].texture, expected[k].texture) << k;
        EXPECT_EQ(actual[k].scale, expected[k].scale) << k;
        EXPECT_EQ(actual[k].position, expected[k].position) << k;
        EXPECT_EQ(actual[k].rotation, expected[k].rotation) << k;
    }
}

// 0x509340 rolls the glow on every drawn frame of the original, at 21.33 per second (Q-C20 and Q-C124 with the pet
// and the cloth): MuRet rolls it once per original frame (46.875 ms) elapsed since its previous one, and in a frame
// with none it repeats the last roll, on the current bone. The first one rolls.
TEST(ScepterGlowTest, TheGlowIsDrawnOncePerOriginalFrame)
{
    const auto bone = Client::Math::OriginalMatrixOf(Client::Math::BoneTransform(1.0f));
    Client::Actors::Drawing::ScepterGlowCarry carry;
    CrtRandom random(5);

    FrameSprites first;
    carry.Create(first, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{1000});
    EXPECT_EQ(random.State(), After(5, 1).State());
    FrameSprites expected;
    CrtRandom sorted(5);
    Client::Actors::Drawing::CreateScepterGlow(expected, bone, 1.0f, glm::vec3(0.0f), sorted);
    ExpectSameGlow(GlowOf(first), GlowOf(expected));

    FrameSprites repeated;
    carry.Create(repeated, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{1040});
    EXPECT_EQ(random.State(), After(5, 1).State()) << "sin un frame del original, ningún rand()";
    ExpectSameGlow(GlowOf(repeated), GlowOf(first));

    // 1046.875 ms: one.
    FrameSprites next;
    carry.Create(next, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::microseconds{1046875});
    EXPECT_EQ(random.State(), After(5, 2).State());
    FrameSprites expectedNext;
    CrtRandom sortedNext = After(5, 1);
    Client::Actors::Drawing::CreateScepterGlow(expectedNext, bone, 1.0f, glm::vec3(0.0f), sortedNext);
    ExpectSameGlow(GlowOf(next), GlowOf(expectedNext));

    // Three at once: the first two only take their rand(), and the last one is drawn.
    FrameSprites late;
    carry.Create(late, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::microseconds{1046875 + (3 * 46875)});
    EXPECT_EQ(random.State(), After(5, 5).State());
    FrameSprites expectedLate;
    CrtRandom sortedLate = After(5, 4);
    Client::Actors::Drawing::CreateScepterGlow(expectedLate, bone, 1.0f, glm::vec3(0.0f), sortedLate);
    ExpectSameGlow(GlowOf(late), GlowOf(expectedLate));
}

TEST(ScepterGlowTest, AClockThatGoesBackRepeatsTheGlow)
{
    const auto bone = Client::Math::OriginalMatrixOf(Client::Math::BoneTransform(1.0f));
    Client::Actors::Drawing::ScepterGlowCarry carry;
    CrtRandom random(9);
    FrameSprites first;
    carry.Create(first, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{500});

    FrameSprites back;
    carry.Create(back, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{100});

    EXPECT_EQ(random.State(), After(9, 1).State());
    ExpectSameGlow(GlowOf(back), GlowOf(first));
}

// In the faithful mode, one roll per draw, even if the frame does not last exactly 46.875 ms: by the clock, the second
// of these would repeat the roll and the third would take two.
TEST(ScepterGlowTest, InTheFaithfulModeEachDrawSortsOnce)
{
    using namespace std::chrono_literals;
    using Client::Models::OriginalFrameTime;
    const auto bone = Client::Math::OriginalMatrixOf(Client::Math::BoneTransform(1.0f));
    Client::Actors::Drawing::ScepterGlowCarry carry;
    CrtRandom random(5);
    const std::array<Client::Core::Duration, 4> clocks{(3 * OriginalFrameTime) + 300us, (4 * OriginalFrameTime) + 100us,
                                                       (5 * OriginalFrameTime) + 300us,
                                                       (5 * OriginalFrameTime) + 300us + 2s};
    for (std::size_t k = 0; k < clocks.size(); ++k)
    {
        FrameSprites sprites;
        carry.Create(sprites, bone, 1.0f, glm::vec3(0.0f), random, clocks[k],
                     Client::Models::OriginalFramePace::OncePerDraw);
        EXPECT_EQ(random.State(), After(5, static_cast<int>(k) + 1).State()) << k;
    }
}

// After a held frame (Q-C85, P-C27 f), RestartTheClock: the next draw rolls one, like the first, even though 5 s
// passed (by the clock it would be 64), with the current bone. Then it follows the clock.
TEST(ScepterGlowTest, AfterARestartTheNextDrawSortsOnce)
{
    using namespace std::chrono_literals;
    using Client::Models::OriginalFrameTime;
    const auto bone = Client::Math::OriginalMatrixOf(Client::Math::BoneTransform(1.0f));
    Client::Actors::Drawing::ScepterGlowCarry carry;
    CrtRandom random(5);
    FrameSprites first;
    carry.Create(first, bone, 1.0f, glm::vec3(0.0f), random, 1s);
    ASSERT_EQ(random.State(), After(5, 1).State());

    carry.RestartTheClock();
    FrameSprites held;
    carry.Create(held, bone, 1.0f, glm::vec3(0.0f), random, 6s);
    EXPECT_EQ(random.State(), After(5, 2).State()) << "un sorteo, no 64";
    FrameSprites expected;
    CrtRandom sorted = After(5, 1);
    Client::Actors::Drawing::CreateScepterGlow(expected, bone, 1.0f, glm::vec3(0.0f), sorted);
    ExpectSameGlow(GlowOf(held), GlowOf(expected));

    FrameSprites next;
    carry.Create(next, bone, 1.0f, glm::vec3(0.0f), random, 6s + OriginalFrameTime);
    EXPECT_EQ(random.State(), After(5, 3).State());
}
} // namespace
