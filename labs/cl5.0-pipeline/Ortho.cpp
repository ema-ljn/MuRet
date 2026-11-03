#include "Ortho.h"

#include <array>
#include <cstddef>

namespace Labs
{
namespace
{
// El elemento de la columna `column` y la fila `row` en column-major.
constexpr std::size_t At(std::size_t column, std::size_t row)
{
    return column * 4 + row;
}
} // namespace

// La página de manual de glOrtho, con near = −1 y far = 1 como gluOrtho2D: z pasa a −z, y la
// traslación en z, −(far + near)/(far − near), es 0.
Matrix4 Ortho2D(float left, float right, float bottom, float top)
{
    Matrix4 matrix{};
    matrix[At(0, 0)] = 2 / (right - left);
    matrix[At(1, 1)] = 2 / (top - bottom);
    matrix[At(2, 2)] = -1;
    matrix[At(3, 0)] = -(right + left) / (right - left);
    matrix[At(3, 1)] = -(top + bottom) / (top - bottom);
    matrix[At(3, 3)] = 1;
    return matrix;
}

// Cada fila del resultado es la fila de la matriz por el punto: la columna c multiplica a la
// coordenada c.
Vec4 Multiply(const Matrix4& matrix, Vec4 point)
{
    const std::array<float, 4> in{point.x, point.y, point.z, point.w};
    std::array<float, 4> out{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            out[row] += matrix[At(column, row)] * in[column];
        }
    }
    return Vec4{out[0], out[1], out[2], out[3]};
}
} // namespace Labs
