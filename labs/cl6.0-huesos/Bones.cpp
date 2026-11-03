#include "Bones.h"

#include "Quaternion.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Labs
{
namespace
{
// El float de 0x6ba360: π/180, redondeado una vez (los bits 0x3c8efa35).
constexpr float DegreesToRadians = 0.017453292f;

// Las cuentas de 0x5fca90, 0x5fccc0 y 0x5fcbd0 quedan en la pila de la x87 hasta el resultado:
// cada elemento se redondea a float una vez. El binario suma en otro orden en cada elemento; en
// double no se nota.
double Row(const Matrix34& m, std::size_t row, const Vec3& v)
{
    return static_cast<double>(m[row][0]) * v[0] + static_cast<double>(m[row][1]) * v[1] +
           static_cast<double>(m[row][2]) * v[2];
}
} // namespace

Matrix34 Concat(const Matrix34& a, const Matrix34& b)
{
    Matrix34 out{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            double sum = 0;
            for (std::size_t k = 0; k < 3; ++k)
            {
                sum += static_cast<double>(a[row][k]) * b[k][column];
            }
            // La fila de abajo de b, (0, 0, 0, 1): la traslación de a solo entra en la columna 3.
            if (column == 3)
            {
                sum += a[row][3];
            }
            out[row][column] = static_cast<float>(sum);
        }
    }
    return out;
}

// Como 0x5fc8f0: cada ángulo por el float de π/180 en la pila, el seno y el coseno de z, de y y de
// x, y los nueve elementos de la mathlib del studiomdl de Half-Life.
Matrix34 AngleMatrix(Vec3 degrees)
{
    const double k = DegreesToRadians;
    const double sy = static_cast<float>(std::sin(degrees[2] * k));
    const double cy = std::cos(degrees[2] * k);
    const double sp = static_cast<float>(std::sin(degrees[1] * k));
    const double cp = static_cast<float>(std::cos(degrees[1] * k));
    const double sr = static_cast<float>(std::sin(degrees[0] * k));
    const double cr = static_cast<float>(std::cos(degrees[0] * k));

    Matrix34 m{};
    m[0][0] = static_cast<float>(cp * cy);
    m[1][0] = static_cast<float>(cp * sy);
    m[2][0] = static_cast<float>(-sp);
    m[0][1] = static_cast<float>(sr * sp * cy - cr * sy);
    m[1][1] = static_cast<float>(cr * cy + sr * sp * sy);
    m[2][1] = static_cast<float>(sr * cp);
    m[0][2] = static_cast<float>(sr * sy + cr * sp * cy);
    m[1][2] = static_cast<float>(cr * sp * sy - sr * cy);
    m[2][2] = static_cast<float>(cr * cp);
    return m;
}

Matrix34 ObjectMatrix(Vec3 degrees, float scale, Vec3 position)
{
    Matrix34 m = AngleMatrix(degrees);
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            m[row][column] *= scale;
        }
        m[row][3] = position[row];
    }
    return m;
}

Matrix34 LocalMatrix(const Bone& bone)
{
    Matrix34 m = QuaternionMatrix(bone.rotation);
    for (std::size_t row = 0; row < 3; ++row)
    {
        m[row][3] = bone.position[row];
    }
    return m;
}

std::optional<std::vector<Matrix34>> PoseChain(std::span<const Bone> bones, const Matrix34& object)
{
    std::vector<Matrix34> world;
    world.reserve(bones.size());
    for (const Bone& bone : bones)
    {
        const Matrix34 local = LocalMatrix(bone);
        if (bone.parent == -1)
        {
            world.push_back(Concat(object, local));
            continue;
        }
        // El padre ya calculado es uno de los anteriores: su índice es menor que el de este hueso,
        // que es world.size().
        if (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= world.size())
        {
            return std::nullopt;
        }
        world.push_back(Concat(world[static_cast<std::size_t>(bone.parent)], local));
    }
    return world;
}

Vec3 Transform(const Matrix34& matrix, Vec3 point)
{
    Vec3 out{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        out[row] = static_cast<float>(Row(matrix, row, point) + matrix[row][3]);
    }
    return out;
}

Vec3 Rotate(const Matrix34& matrix, Vec3 vector)
{
    Vec3 out{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        out[row] = static_cast<float>(Row(matrix, row, vector));
    }
    return out;
}
} // namespace Labs
