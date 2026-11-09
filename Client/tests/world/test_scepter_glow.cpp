#include "World/ScepterGlow.h"

#include "Core/IClock.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Sprites.h"

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

// Los seis rand() que pide un frame, después de los de `random`.
CrtRandom After(std::uint32_t seed, int frames)
{
    CrtRandom random(seed);
    for (int k = 0; k < 6 * frames; ++k)
    {
        (void)random.Next();
    }
    return random;
}

// Los cinco sprites que dejó la tabla.
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

// 0x509340 sortea el brillo en cada frame dibujado del original, a 21,33 por segundo (Q-C20 y Q-C124 con la
// mascota y la tela): MuRet lo sortea una vez por cada frame del original (46,875 ms) que pasó desde el
// suyo anterior, y en un frame sin ninguno repite el último sorteo, en el hueso de ahora. El primero
// sortea.
TEST(ScepterGlowTest, TheGlowIsDrawnOncePerOriginalFrame)
{
    const auto bone = Client::Render::Models::OriginalMatrixOf(Client::Render::Models::BoneTransform(1.0f));
    Client::World::ScepterGlowCarry carry;
    CrtRandom random(5);

    FrameSprites first;
    carry.Create(first, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{1000});
    EXPECT_EQ(random.State(), After(5, 1).State());
    FrameSprites expected;
    CrtRandom sorted(5);
    Client::World::CreateScepterGlow(expected, bone, 1.0f, glm::vec3(0.0f), sorted);
    ExpectSameGlow(GlowOf(first), GlowOf(expected));

    FrameSprites repeated;
    carry.Create(repeated, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{1040});
    EXPECT_EQ(random.State(), After(5, 1).State()) << "sin un frame del original, ningún rand()";
    ExpectSameGlow(GlowOf(repeated), GlowOf(first));

    // 1046,875 ms: uno.
    FrameSprites next;
    carry.Create(next, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::microseconds{1046875});
    EXPECT_EQ(random.State(), After(5, 2).State());
    FrameSprites expectedNext;
    CrtRandom sortedNext = After(5, 1);
    Client::World::CreateScepterGlow(expectedNext, bone, 1.0f, glm::vec3(0.0f), sortedNext);
    ExpectSameGlow(GlowOf(next), GlowOf(expectedNext));

    // Tres de golpe: los dos primeros solo sacan sus rand(), y el último se dibuja.
    FrameSprites late;
    carry.Create(late, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::microseconds{1046875 + (3 * 46875)});
    EXPECT_EQ(random.State(), After(5, 5).State());
    FrameSprites expectedLate;
    CrtRandom sortedLate = After(5, 4);
    Client::World::CreateScepterGlow(expectedLate, bone, 1.0f, glm::vec3(0.0f), sortedLate);
    ExpectSameGlow(GlowOf(late), GlowOf(expectedLate));
}

// Un reloj que vuelve no corre frames: repite el último sorteo.
TEST(ScepterGlowTest, AClockThatGoesBackRepeatsTheGlow)
{
    const auto bone = Client::Render::Models::OriginalMatrixOf(Client::Render::Models::BoneTransform(1.0f));
    Client::World::ScepterGlowCarry carry;
    CrtRandom random(9);
    FrameSprites first;
    carry.Create(first, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{500});

    FrameSprites back;
    carry.Create(back, bone, 1.0f, glm::vec3(0.0f), random, std::chrono::milliseconds{100});

    EXPECT_EQ(random.State(), After(9, 1).State());
    ExpectSameGlow(GlowOf(back), GlowOf(first));
}

// En el modo fiel, un sorteo por dibujo, aunque el frame no dure justo 46,875 ms: por el reloj, el segundo
// de estos repetiría el sorteo y el tercero sacaría dos.
TEST(ScepterGlowTest, InTheFaithfulModeEachDrawSortsOnce)
{
    using namespace std::chrono_literals;
    using Client::Render::Models::OriginalFrameTime;
    const auto bone = Client::Render::Models::OriginalMatrixOf(Client::Render::Models::BoneTransform(1.0f));
    Client::World::ScepterGlowCarry carry;
    CrtRandom random(5);
    const std::array<Client::Core::Duration, 4> clocks{(3 * OriginalFrameTime) + 300us, (4 * OriginalFrameTime) + 100us,
                                                       (5 * OriginalFrameTime) + 300us,
                                                       (5 * OriginalFrameTime) + 300us + 2s};
    for (std::size_t k = 0; k < clocks.size(); ++k)
    {
        FrameSprites sprites;
        carry.Create(sprites, bone, 1.0f, glm::vec3(0.0f), random, clocks[k],
                     Client::Render::Models::OriginalFramePace::OncePerDraw);
        EXPECT_EQ(random.State(), After(5, static_cast<int>(k) + 1).State()) << k;
    }
}

// Después de un frame retenido (Q-C85, P-C27 f), RestartTheClock: el próximo dibujo sortea uno, como el primero,
// aunque pasaron 5 s (por el reloj serían 64), con el hueso de ahora. Después sigue por el reloj.
TEST(ScepterGlowTest, AfterARestartTheNextDrawSortsOnce)
{
    using namespace std::chrono_literals;
    using Client::Render::Models::OriginalFrameTime;
    const auto bone = Client::Render::Models::OriginalMatrixOf(Client::Render::Models::BoneTransform(1.0f));
    Client::World::ScepterGlowCarry carry;
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
    Client::World::CreateScepterGlow(expected, bone, 1.0f, glm::vec3(0.0f), sorted);
    ExpectSameGlow(GlowOf(held), GlowOf(expected));

    FrameSprites next;
    carry.Create(next, bone, 1.0f, glm::vec3(0.0f), random, 6s + OriginalFrameTime);
    EXPECT_EQ(random.State(), After(5, 3).State());
}
} // namespace
