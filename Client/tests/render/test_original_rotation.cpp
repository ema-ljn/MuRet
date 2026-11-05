#include "Render/Models/OriginalRotation.h"

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
} // namespace
