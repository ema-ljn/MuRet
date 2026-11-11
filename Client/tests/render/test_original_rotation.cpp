#include "Render/Models/OriginalRotation.h"

#include "Render/Models/HalfLifeMath.h"

#include "support/HexFloatFixture.h"
#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>

// Client::Render::Models::OriginalAngleMatrix y OriginalVectorRotate contra 0x5fc8f0 y 0x5fcbd0 del
// código real del main.exe, en 400 casos al azar (tests/vectors/world55/rotation.txt,
// make_rotation_vectors.py): cada float bit a bit.
namespace
{
TEST(OriginalRotationTest, FourHundredCasesMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "rotation.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "rotation 1");
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "cases 400");
    std::size_t cases = 0;
    while (std::getline(file, line) && line != "end")
    {
        SCOPED_TRACE(cases);
        std::istringstream words(line);
        std::string word;
        words >> word;
        ASSERT_EQ(word, "case");
        const auto next = [&words]() { return Client::Tests::ReadHexFloat(words); };
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
        Client::Render::Models::OriginalMatrix expectedMatrix{};
        for (float& value : expectedMatrix)
        {
            value = next();
        }
        glm::vec3 expectedRotated{};
        for (int k = 0; k < 3; ++k)
        {
            expectedRotated[k] = next();
        }
        ASSERT_FALSE(words.fail());
        ASSERT_TRUE((words >> std::ws).eof());
        const auto matrix = Client::Render::Models::OriginalAngleMatrix(angles);
        for (std::size_t k = 0; k < matrix.size(); ++k)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(matrix[k]), std::bit_cast<std::uint32_t>(expectedMatrix[k]))
                << "caso " << cases << ", elemento " << k;
        }
        const glm::vec3 rotated = Client::Render::Models::OriginalVectorRotate(vector, matrix);
        for (int k = 0; k < 3; ++k)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(rotated[k]), std::bit_cast<std::uint32_t>(expectedRotated[k]))
                << "caso " << cases << ", componente " << k;
        }
        ++cases;
    }
    ASSERT_EQ(line, "end");
    EXPECT_TRUE((file >> std::ws).eof());
    EXPECT_EQ(cases, 400u);
}

// La transpuesta y el signo fijan estos dos componentes del offset histórico de la cámara.
TEST(OriginalRotationTest, TheInverseRotatePreservesTheHistoricalCameraOffset)
{
    using Client::Render::Models::OriginalAngleMatrix;
    using Client::Render::Models::OriginalVectorIRotate;
    const auto offset = OriginalVectorIRotate({0.0f, -1000.0f, 0.0f}, OriginalAngleMatrix({0.0f, 0.0f, -45.0f}));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(14350.0 + static_cast<double>(offset.x))), 0x466b446du);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(12050.0 + static_cast<double>(offset.y))), 0x46313b93u);
}

// Salidas de 0x5fcc30 real: el fixture fija la expectativa sin repetir la fórmula de la implementación.
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
        const auto next = [&words]() { return Client::Tests::ReadHexFloat(words); };
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
        glm::vec3 expected{};
        for (int k = 0; k < 3; ++k)
        {
            expected[k] = next();
        }
        ASSERT_FALSE(words.fail());
        ASSERT_TRUE((words >> std::ws).eof());
        const auto actual = Client::Render::Models::OriginalVectorIRotate(vector, matrix);
        for (int k = 0; k < 3; ++k)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[k]), std::bit_cast<std::uint32_t>(expected[k])) << k;
        }
        ++cases;
    }
    ASSERT_EQ(line, "end");
    EXPECT_TRUE((file >> std::ws).eof());
    EXPECT_EQ(cases, 403u);
}

// En double, (2^70 + 1) - 2^70 da 0; (2^70 - 2^70) + 1 da 1. Cada par aísla una
// componente y rechaza las otras dos asociaciones: los vectores al azar no alcanzan esa cancelación.
TEST(OriginalRotationTest, TheInverseRotateKeepsTheBinarySumOrderWhenProductsCancel)
{
    using Client::Render::Models::OriginalMatrix;
    using Client::Render::Models::OriginalVectorIRotate;
    constexpr float Huge = 0x1p70f;
    OriginalMatrix xMatrix{};
    xMatrix[0] = xMatrix[4] = xMatrix[8] = 1.0f;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({-Huge, 1.0f, Huge}, xMatrix).x), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({-Huge, Huge, 1.0f}, xMatrix).x), 0u);

    OriginalMatrix yMatrix{};
    yMatrix[1] = yMatrix[9] = yMatrix[5] = 1.0f;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({Huge, -Huge, 1.0f}, yMatrix).y), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({1.0f, -Huge, Huge}, yMatrix).y), 0u);

    OriginalMatrix zMatrix{};
    zMatrix[2] = zMatrix[10] = zMatrix[6] = 1.0f;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({Huge, -Huge, 1.0f}, zMatrix).z), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(OriginalVectorIRotate({1.0f, -Huge, Huge}, zMatrix).z), 0u);
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
// 0x5fca90 por el ASM: la segunda y la tercera fila suman desde la columna 2, (a6·b8 + a5·b4) + a4·b0.
// Con a4·b0 apenas sobre 1 y a6·b8 apenas bajo −1, el término chico se redondea en otra grilla según el
// orden: el del binario da −0x1.48808ap−29 y el natural, (a4·b0 + a5·b4) + a6·b8, −0x1.48808cp−29
// (búsqueda en Python con productos exactos en double y FSTP a float). La primera fila es 1 · b.
TEST(OriginalRotationTest, TheConcatSumsTheLowerRowsFromTheThirdColumn)
{
    using Client::Render::Models::OriginalMatrix;
    constexpr float A4 = 0x1.000006p+0f;
    constexpr float A5 = -0x1.d37b24p-21f;
    constexpr float A6 = -0x1.fffffep-1f;
    constexpr float B0 = 0x1.000004p+0f;
    constexpr float B4 = 0x1.c90596p-1f;
    constexpr float B8 = 0x1.fffffcp-1f;
    const OriginalMatrix parent{1.0f, 0.0f, 0.0f, 0.0f, A4, A5, A6, 0.0f, A4, A5, A6, 0.0f};
    const OriginalMatrix child{B0, 0.0f, 0.0f, B0, B4, 0.0f, 0.0f, B4, B8, 0.0f, 0.0f, B8};

    const OriginalMatrix out = Client::Render::Models::OriginalConcatTransforms(parent, child);

    constexpr float Sum = -0x1.48808ap-29f;
    const OriginalMatrix expected{B0, 0.0f, 0.0f, B0, Sum, 0.0f, 0.0f, Sum, Sum, 0.0f, 0.0f, Sum};
    for (std::size_t k = 0; k < out.size(); ++k)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(out[k]), std::bit_cast<std::uint32_t>(expected[k])) << k;
    }
}

// BoneTransformOf deshace OriginalMatrixOf: la 3 × 4 vuelve por filas, cada valor a su lugar.
TEST(OriginalRotationTest, ARowMatrixGoesBackToTheBoneTransform)
{
    Client::Render::Models::OriginalMatrix rows{};
    for (std::size_t k = 0; k < rows.size(); ++k)
    {
        rows[k] = static_cast<float>(k) + 0.5f;
    }
    const Client::Render::Models::BoneTransform bone = Client::Render::Models::BoneTransformOf(rows);
    EXPECT_EQ(Client::Render::Models::OriginalMatrixOf(bone), rows);
    EXPECT_EQ(bone[3], glm::vec3(3.5f, 7.5f, 11.5f));
}
} // namespace
