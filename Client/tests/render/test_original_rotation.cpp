#include "Render/Models/OriginalRotation.h"

#include "Render/Models/HalfLifeMath.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

// Client::Render::Models::OriginalAngleMatrix y OriginalVectorRotate contra 0x5fc8f0 y 0x5fcbd0 del
// código real del main.exe, en 400 casos al azar (tests/vectors/world55/rotation.txt,
// make_rotation_vectors.py): cada float bit a bit.
namespace
{
float FromBits(const std::string& word)
{
    return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(word, nullptr, 16)));
}

TEST(OriginalRotationTest, FourHundredCasesMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "rotation.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "rotation 1");
    ASSERT_TRUE(std::getline(file, line));
    std::size_t cases = 0;
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream words(line);
        std::string word;
        words >> word;
        ASSERT_EQ(word, "case");
        const auto next = [&words]()
        {
            std::string value;
            words >> value;
            return FromBits(value);
        };
        glm::vec3 angles{};
        glm::vec3 vector{};
        for (int k = 0; k < 3; ++k)
        {
            angles[k] = next();
        }
        for (int k = 0; k < 3; ++k)
        {
            vector[k] = next();
        }
        const auto matrix = Client::Render::Models::OriginalAngleMatrix(angles);
        for (std::size_t k = 0; k < matrix.size(); ++k)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(matrix[k]), std::bit_cast<std::uint32_t>(next()))
                << "caso " << cases << ", elemento " << k;
        }
        const glm::vec3 rotated = Client::Render::Models::OriginalVectorRotate(vector, matrix);
        for (int k = 0; k < 3; ++k)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(rotated[k]), std::bit_cast<std::uint32_t>(next()))
                << "caso " << cases << ", componente " << k;
        }
        ++cases;
    }
    EXPECT_EQ(cases, 400u);
}

// Una BoneTransform de glm (m[columna][fila]) pasa a la 3 × 4 por filas del original: con doce valores
// distintos, cada uno va a su lugar, la traslación en la columna 3 de cada fila (m[3] de glm).
TEST(OriginalRotationTest, ABoneTransformGoesByRows)
{
    Client::Render::Models::BoneTransform bone{};
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            bone[column][row] = static_cast<float>((row * 10) + column);
        }
    }
    const Client::Render::Models::OriginalMatrix rows = Client::Render::Models::OriginalMatrixOf(bone);
    for (std::size_t k = 0; k < rows.size(); ++k)
    {
        EXPECT_EQ(rows[k], static_cast<float>(((k / 4) * 10) + (k % 4))) << k;
    }
    // Y un punto pasa por las dos igual: el giro y la traslación de glm son los del original.
    const glm::vec3 point{3.0f, -5.0f, 7.0f};
    EXPECT_EQ(Client::Render::Models::OriginalVectorTransform(point, rows),
              Client::Render::Models::VectorTransform(point, bone));
}
} // namespace
