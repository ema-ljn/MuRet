#include "Render/Models/OriginalRotation.h"

#include "Render/Models/HalfLifeMath.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// La transpuesta y el signo fijan los bits de la traza. Los 400 casos prueban la cuenta en double;
// la sensibilidad al orden de sumas se prueba aparte con cancelaciones.
TEST(OriginalRotationTest, TheInverseRotateSumsLikeTheBinary)
{
    using Client::Render::Models::OriginalAngleMatrix;
    using Client::Render::Models::OriginalVectorIRotate;
    const auto offset = OriginalVectorIRotate({0.0f, -1000.0f, 0.0f}, OriginalAngleMatrix({0.0f, 0.0f, -45.0f}));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(14350.0 + static_cast<double>(offset.x))), 0x466b446du);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(12050.0 + static_cast<double>(offset.y))), 0x46313b93u);

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
        glm::vec3 angles{}, vector{};
        for (int k = 0; k < 3; ++k)
            angles[k] = next();
        for (int k = 0; k < 3; ++k)
            vector[k] = next();
        Client::Render::Models::OriginalMatrix matrix{};
        for (float& value : matrix)
            value = next();
        const double x = vector.x, y = vector.y, z = vector.z;
        const glm::vec3 expected{
            static_cast<float>((static_cast<double>(matrix[8]) * z + static_cast<double>(matrix[4]) * y) +
                               x * static_cast<double>(matrix[0])),
            static_cast<float>((static_cast<double>(matrix[1]) * x + static_cast<double>(matrix[9]) * z) +
                               static_cast<double>(matrix[5]) * y),
            static_cast<float>((static_cast<double>(matrix[2]) * x + static_cast<double>(matrix[10]) * z) +
                               static_cast<double>(matrix[6]) * y)};
        const auto actual = OriginalVectorIRotate(vector, matrix);
        for (int k = 0; k < 3; ++k)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[k]), std::bit_cast<std::uint32_t>(expected[k]))
                << cases << ":" << k;
        ++cases;
    }
    EXPECT_EQ(cases, 400u);
}

// Salidas de 0x5fcc30 real: no comparten la fórmula del test manual ni de la implementación.
TEST(OriginalRotationTest, FourHundredAndThreeInverseCasesMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir().parent_path() / "game" / "inverse_rotation.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "inverse_rotation 1");
    std::size_t cases = 0;
    while (std::getline(file, line) && line != "end")
    {
        SCOPED_TRACE(cases);
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
        glm::vec3 vector{};
        for (int k = 0; k < 3; ++k)
        {
            vector[k] = next();
        }
        Client::Render::Models::OriginalMatrix matrix{};
        for (float& value : matrix)
        {
            value = next();
        }
        const auto actual = Client::Render::Models::OriginalVectorIRotate(vector, matrix);
        for (int k = 0; k < 3; ++k)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[k]), std::bit_cast<std::uint32_t>(next())) << k;
        }
        ASSERT_FALSE(words.fail());
        ++cases;
    }
    EXPECT_EQ(line, "end");
    EXPECT_EQ(cases, 403u);
}

// En double, (2^70 + 1) - 2^70 da 0; (2^70 - 2^70) + 1 da 1. Las matrices
// sintéticas aíslan cada componente: los 400 vectores al azar no alcanzan esa cancelación.
TEST(OriginalRotationTest, TheInverseRotateKeepsTheBinarySumOrderWhenProductsCancel)
{
    using Client::Render::Models::OriginalMatrix;
    using Client::Render::Models::OriginalVectorIRotate;
    constexpr float Huge = 0x1p70f;
    OriginalMatrix xMatrix{};
    xMatrix[0] = xMatrix[4] = xMatrix[8] = 1.0f;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({-Huge, 1.0f, Huge}, xMatrix).x), 0u);

    OriginalMatrix yMatrix{};
    yMatrix[1] = yMatrix[9] = yMatrix[5] = 1.0f;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({Huge, -Huge, 1.0f}, yMatrix).y), 0u);

    OriginalMatrix zMatrix{};
    zMatrix[2] = zMatrix[10] = zMatrix[6] = 1.0f;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({Huge, -Huge, 1.0f}, zMatrix).z), 0u);
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
