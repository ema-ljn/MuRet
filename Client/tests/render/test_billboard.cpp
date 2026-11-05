#include "Render/Billboard.h"

#include "Render/Models/OriginalRotation.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

// Client::Render::MakeBillboard contra 0x621400 del código real del main.exe, en 300 casos al azar
// (tests/vectors/world55/billboard.txt, make_billboard_vectors.py): las cuatro esquinas, sus UV y el
// color de GL, bit a bit.
namespace
{
std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

float FromBits(const std::string& word)
{
    return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(word, nullptr, 16)));
}

TEST(BillboardTest, ThreeHundredCasesMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "billboard.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "billboard 1");
    ASSERT_TRUE(std::getline(file, line));
    std::size_t cases = 0;
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream head(line);
        std::string word;
        head >> word;
        ASSERT_EQ(word, "case");
        int slot = 0;
        int components = 0;
        head >> slot >> components;
        const auto next = [&head]()
        {
            std::string value;
            head >> value;
            return FromBits(value);
        };
        Client::Render::Models::OriginalMatrix view{};
        for (float& value : view)
        {
            value = next();
        }
        const glm::vec3 position{next(), next(), next()};
        const float width = next();
        const float height = next();
        const glm::vec3 light{next(), next(), next()};
        const float rotation = next();
        const glm::vec2 uv{next(), next()};
        const glm::vec2 uvSize{next(), next()};
        const auto billboard = Client::Render::MakeBillboard(view, position, width, height, light, rotation, uv, uvSize,
                                                             static_cast<std::uint16_t>(slot), components);
        ASSERT_TRUE(std::getline(file, line));
        std::istringstream count(line);
        std::string label;
        std::size_t calls = 0;
        count >> label >> calls;
        std::size_t corner = 0;
        for (std::size_t c = 0; c < calls; ++c)
        {
            ASSERT_TRUE(std::getline(file, line));
            std::istringstream call(line);
            std::string name;
            call >> name;
            const auto value = [&call]()
            {
                std::string text;
                call >> text;
                return Bits(FromBits(text));
            };
            const std::string where = "caso " + std::to_string(cases) + ", " + name;
            if (name == "glBegin")
            {
                int mode = 0;
                call >> mode;
                EXPECT_EQ(mode, 7) << where << ": GL_QUADS";
            }
            else if (name == "glColor3fv")
            {
                EXPECT_EQ(components, 3) << where;
                EXPECT_EQ(Bits(billboard.color.red), value()) << where;
                EXPECT_EQ(Bits(billboard.color.green), value()) << where;
                EXPECT_EQ(Bits(billboard.color.blue), value()) << where;
                // glColor3fv deja el alpha de GL en 1.
                EXPECT_EQ(billboard.color.alpha, 1.0f) << where;
            }
            else if (name == "glColor4f")
            {
                EXPECT_EQ(Bits(billboard.color.red), value()) << where;
                EXPECT_EQ(Bits(billboard.color.green), value()) << where;
                EXPECT_EQ(Bits(billboard.color.blue), value()) << where;
                EXPECT_EQ(Bits(billboard.color.alpha), value()) << where;
            }
            else if (name == "glTexCoord2f")
            {
                ASSERT_LT(corner, 4u);
                EXPECT_EQ(Bits(billboard.corners[corner].uv.x), value()) << where << " " << corner;
                EXPECT_EQ(Bits(billboard.corners[corner].uv.y), value()) << where << " " << corner;
            }
            else if (name == "glVertex3fv")
            {
                ASSERT_LT(corner, 4u);
                for (int k = 0; k < 3; ++k)
                {
                    EXPECT_EQ(Bits(billboard.corners[corner].position[k]), value())
                        << where << " " << corner << " " << k;
                }
                ++corner;
            }
        }
        EXPECT_EQ(corner, 4u);
        ++cases;
    }
    EXPECT_EQ(cases, 300u);
}

// La vista de 0x620370: por filas, la traspuesta de la de glm.
TEST(BillboardTest, TheViewGoesByRows)
{
    glm::mat4 view(1.0f);
    view[3] = glm::vec4(10.0f, 20.0f, 30.0f, 1.0f);
    view[1][0] = 5.0f;
    const auto rows = Client::Render::ViewRows(view);
    EXPECT_EQ(rows[3], 10.0f);
    EXPECT_EQ(rows[7], 20.0f);
    EXPECT_EQ(rows[11], 30.0f);
    EXPECT_EQ(rows[1], 5.0f);
    EXPECT_EQ(rows[4], 0.0f);
}
} // namespace
