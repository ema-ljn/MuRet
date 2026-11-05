#include "Scenes/CharacterHead.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

// Client::Scenes::StepCharacterHead contra el código real del main.exe emulado: el tramo de 0x505de0 que
// mueve la cabeza (0x5068eb..0x5069ab), con 0x4e96f0 y 0x4e9560, bit a bit (tests/vectors/world55/head.txt,
// de docs/research/scripts/login/make_head_vectors.py).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::CharacterHead;
using Client::Scenes::StepCharacterHead;

std::uint32_t Hex(const std::string& text)
{
    return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
}

float FromBits(const std::string& text)
{
    return std::bit_cast<float>(Hex(text));
}

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

TEST(CharacterHeadTest, TurnsLikeTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "head.txt");
    ASSERT_TRUE(file.is_open());
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "head 1");
    std::size_t cases = 0;
    std::size_t steps = 0;
    std::size_t sorted = 0;
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream head(line);
        std::string word;
        std::string seed;
        std::string kind;
        int count = 0;
        int hero = 0;
        head >> word >> seed >> kind >> count >> hero;
        ASSERT_EQ(word, "case");
        ++cases;
        CrtRandom random(Hex(seed));
        CharacterHead state;
        if (kind == "walk")
        {
            // Los comienzos de make_head_vectors.py.
            const std::size_t index = cases - 1;
            const glm::vec2 heads[] = {{0.0f, 0.0f}, {0.0f, 0.0f}, {350.0f, 10.0f}, {300.0f, 5.0f}};
            const glm::vec2 targets[] = {{0.0f, 0.0f}, {0.0f, 0.0f}, {20.0f, 340.0f}, {10.0f, 200.0f}};
            ASSERT_LT(index, std::size(heads));
            state.angles = heads[index];
            state.target = targets[index];
        }
        for (int step = 0; step < count; ++step)
        {
            ASSERT_TRUE(std::getline(file, line));
            std::istringstream row(line);
            std::string state32;
            row >> word >> state32;
            ASSERT_EQ(word, "step");
            if (kind == "pairs")
            {
                std::string a;
                std::string b;
                std::string c;
                std::string d;
                row >> a >> b >> c >> d;
                state.angles = {FromBits(a), FromBits(b)};
                state.target = {FromBits(c), FromBits(d)};
            }
            const glm::vec2 before = state.target;
            StepCharacterHead(state, hero == 0, random);
            if (state.target != before)
            {
                ++sorted;
            }
            std::string values[4];
            row >> values[0] >> values[1] >> values[2] >> values[3];
            const std::string where = "caso " + std::to_string(cases) + ", paso " + std::to_string(step);
            ASSERT_EQ(random.State(), Hex(state32)) << where << ": el rand()";
            EXPECT_EQ(Bits(state.angles.x), Hex(values[0])) << where << ": la cabeza x";
            EXPECT_EQ(Bits(state.angles.y), Hex(values[1])) << where << ": la cabeza y";
            EXPECT_EQ(Bits(state.target.x), Hex(values[2])) << where << ": el blanco x";
            EXPECT_EQ(Bits(state.target.y), Hex(values[3])) << where << ": el blanco y";
            ++steps;
        }
    }
    EXPECT_EQ(cases, 6u);
    EXPECT_EQ(steps, 3400u);
    EXPECT_GT(sorted, 40u) << "los pasos seguidos sortean blancos nuevos";
}

// El héroe no sortea: no saca rand(), y la cabeza sigue girando hacia el blanco que tenía.
TEST(CharacterHeadTest, TheHeroOnlyTurns)
{
    CrtRandom random(3);
    const std::uint32_t before = random.State();
    CharacterHead state{.angles = {0.0f, 0.0f}, .target = {50.0f, 10.0f}};

    StepCharacterHead(state, false, random);

    EXPECT_EQ(random.State(), before);
    EXPECT_FLOAT_EQ(state.angles.x, 10.0f);
    EXPECT_FLOAT_EQ(state.angles.y, 2.0f);
}

// 0x4e96f0: la resta, o la vuelta por el otro lado, y con el tercer argumento, sin signo.
TEST(CharacterHeadTest, TheAngleDistanceTakesTheShortWay)
{
    using Client::Render::Models::AngleDistance;
    EXPECT_EQ(AngleDistance(10.0f, 50.0f, true), 40.0);
    EXPECT_EQ(AngleDistance(50.0f, 10.0f, false), -40.0);
    EXPECT_EQ(AngleDistance(50.0f, 10.0f, true), 40.0);
    EXPECT_EQ(AngleDistance(10.0f, 350.0f, true), 20.0);
    EXPECT_EQ(AngleDistance(350.0f, 10.0f, true), 20.0);
    EXPECT_EQ(AngleDistance(-10.0f, 10.0f, true), 20.0) << "−10 pasa a 350, y 350 − 180 > 10: 360 − 350 + 10";
}
} // namespace
