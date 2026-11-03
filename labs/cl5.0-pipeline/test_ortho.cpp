#include "Ortho.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <gtest/gtest.h>

#include <cstddef>

// Labs::Ortho2D: la matriz de gluOrtho2D a mano (CL5.0, paso 3), contra glm::ortho.
namespace
{
using Labs::Matrix4;
using Labs::Multiply;
using Labs::Ortho2D;
using Labs::Vec4;

struct Size
{
    float width;
    float height;
};

// Las dos ventanas del plan: la de referencia y la de las trazas del 06.
constexpr Size Sizes[] = {{640, 480}, {1024, 768}};

// gluOrtho2D(0, W, 0, H), la de 0x6219b0: (0, 0) es la esquina de abajo a la izquierda del NDC y
// (W, H) la de arriba a la derecha. La y va para arriba, como en GL.
TEST(OrthoTest, MapsTheCornersToNdc)
{
    for (const Size size : Sizes)
    {
        SCOPED_TRACE(size.width);
        const Matrix4 ortho = Ortho2D(0, size.width, 0, size.height);

        const Vec4 bottomLeft = Multiply(ortho, Vec4{0, 0});
        EXPECT_FLOAT_EQ(bottomLeft.x, -1);
        EXPECT_FLOAT_EQ(bottomLeft.y, -1);
        EXPECT_FLOAT_EQ(bottomLeft.z, 0);
        EXPECT_FLOAT_EQ(bottomLeft.w, 1);

        const Vec4 topRight = Multiply(ortho, Vec4{size.width, size.height});
        EXPECT_FLOAT_EQ(topRight.x, 1);
        EXPECT_FLOAT_EQ(topRight.y, 1);
        EXPECT_FLOAT_EQ(topRight.w, 1);

        const Vec4 center = Multiply(ortho, Vec4{size.width / 2, size.height / 2});
        EXPECT_FLOAT_EQ(center.x, 0);
        EXPECT_FLOAT_EQ(center.y, 0);
    }
}

// Los 16 floats, bit a bit: glm::ortho de cuatro argumentos es gluOrtho2D (near −1, far 1), y
// glm::value_ptr los da en el orden en que están en memoria, column-major, el que espera
// glUniformMatrix4fv con transpose en GL_FALSE.
TEST(OrthoTest, MatchesGlm)
{
    for (const Size size : Sizes)
    {
        SCOPED_TRACE(size.width);
        const Matrix4 byHand = Ortho2D(0, size.width, 0, size.height);
        const glm::mat4 fromGlm = glm::ortho(0.0f, size.width, 0.0f, size.height);
        const float* glmFloats = glm::value_ptr(fromGlm);
        for (std::size_t i = 0; i < byHand.size(); ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_EQ(byHand[i], glmFloats[i]);
        }
    }
}

// La traslación es la cuarta columna: en column-major va en los elementos 12, 13 y 14, y no en 3, 7
// y 11, donde la pondría un arreglo fila por fila. glm la guarda igual: fromGlm[3] es la columna 3.
TEST(OrthoTest, IsColumnMajor)
{
    const Matrix4 ortho = Ortho2D(0, 1024, 0, 768);
    EXPECT_FLOAT_EQ(ortho[0], 2.0f / 1024);
    EXPECT_FLOAT_EQ(ortho[5], 2.0f / 768);
    EXPECT_FLOAT_EQ(ortho[10], -1);
    EXPECT_FLOAT_EQ(ortho[12], -1);
    EXPECT_FLOAT_EQ(ortho[13], -1);
    EXPECT_FLOAT_EQ(ortho[14], 0);
    EXPECT_FLOAT_EQ(ortho[15], 1);
    EXPECT_FLOAT_EQ(ortho[3], 0);
    EXPECT_FLOAT_EQ(ortho[7], 0);
    EXPECT_FLOAT_EQ(ortho[11], 0);

    const glm::mat4 fromGlm = glm::ortho(0.0f, 1024.0f, 0.0f, 768.0f);
    EXPECT_EQ(fromGlm[3][0], ortho[12]);
    EXPECT_EQ(fromGlm[3][1], ortho[13]);
}
} // namespace
