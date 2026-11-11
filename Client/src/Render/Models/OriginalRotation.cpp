#include "Render/Models/OriginalRotation.h"

#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>

namespace Client::Render::Models
{
namespace
{
// π/180 en float (0x6ba360).
constexpr float Degrees = std::bit_cast<float>(0x3C8EFA35u);

float F32(double value)
{
    return static_cast<float>(value);
}
} // namespace

OriginalMatrix OriginalMatrixOf(const BoneTransform& m)
{
    OriginalMatrix rows{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            rows[static_cast<std::size_t>((row * 4) + column)] = m[column][row];
        }
    }
    return rows;
}

BoneTransform BoneTransformOf(const OriginalMatrix& rows)
{
    BoneTransform m(1.0f);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            m[column][row] = rows[static_cast<std::size_t>((row * 4) + column)];
        }
    }
    return m;
}

// 0x5fca90: a es el padre y b el hijo, por filas. La primera fila suma a0·b, a1·b y a2·b en ese orden
// en la columna 0 y a1·b, a0·b, a2·b en las otras; la segunda y la tercera, de la columna 2 a la 0.
OriginalMatrix OriginalConcatTransforms(const OriginalMatrix& parent, const OriginalMatrix& child)
{
    const auto a = [&parent](std::size_t k) { return static_cast<double>(parent[k]); };
    const auto b = [&child](std::size_t k) { return static_cast<double>(child[k]); };
    OriginalMatrix out{};
    out[0] = F32(((a(0) * b(0)) + (a(1) * b(4))) + (a(2) * b(8)));
    for (std::size_t j = 1; j < 4; ++j)
    {
        const double sum = ((a(1) * b(4 + j)) + (a(0) * b(j))) + (a(2) * b(8 + j));
        out[j] = F32(j == 3 ? sum + a(3) : sum);
    }
    for (std::size_t row = 1; row < 3; ++row)
    {
        const std::size_t r = row * 4;
        for (std::size_t j = 0; j < 4; ++j)
        {
            const double sum = ((a(r + 2) * b(8 + j)) + (a(r + 1) * b(4 + j))) + (a(r) * b(j));
            out[r + j] = F32(j == 3 ? sum + a(r + 3) : sum);
        }
    }
    return out;
}

// 0x5fc8f0 como la x87: el seno y el coseno de y y de x guardados en float, el coseno de z en el
// registro, cada producto en 53 bits y cada elemento guardado en float. Por filas: m[i · 4 + j].
OriginalMatrix OriginalAngleMatrix(const glm::vec3& degrees)
{
    const double z = static_cast<double>(degrees.z) * static_cast<double>(Degrees);
    const double y = static_cast<double>(degrees.y) * static_cast<double>(Degrees);
    const double x = static_cast<double>(degrees.x) * static_cast<double>(Degrees);
    const double sinZ = F32(std::sin(z));
    const double cosZ = std::cos(z);
    const double sinY = F32(std::sin(y));
    const double cosY = F32(std::cos(y));
    const double sinX = F32(std::sin(x));
    const double cosX = F32(std::cos(x));
    OriginalMatrix m{};
    m[0] = F32(cosY * cosZ);
    m[4] = F32(cosY * sinZ);
    m[8] = F32(-sinY);
    const double sxsy = sinX * sinY;
    m[1] = F32((sxsy * cosZ) - (cosX * sinZ));
    m[5] = F32((sxsy * sinZ) + (cosX * cosZ));
    m[9] = F32(sinX * cosY);
    const double cxsy = cosX * sinY;
    m[2] = F32((cxsy * cosZ) + (sinX * sinZ));
    m[6] = F32((cxsy * sinZ) - (sinX * cosZ));
    m[10] = F32(cosX * cosY);
    return m;
}

// 0x5fcbd0: el vector por el giro, cada componente en 53 bits con el orden de sus sumas.
glm::vec3 OriginalVectorRotate(const glm::vec3& v, const OriginalMatrix& m)
{
    const auto at = [&m](std::size_t k) { return static_cast<double>(m[k]); };
    const double x = v.x;
    const double y = v.y;
    const double z = v.z;
    return {F32(((at(1) * y) + (at(2) * z)) + (x * at(0))), F32(((at(4) * x) + (at(5) * y)) + (at(6) * z)),
            F32(((at(8) * x) + (at(9) * y)) + (at(10) * z))};
}

void OriginalVectorRotateInPlace(glm::vec3& v, const OriginalMatrix& m)
{
    const auto at = [&m](std::size_t k) { return static_cast<double>(m[k]); };
    // 0x5fcbf0/0x5fcc07: cada fila se guarda antes de que la siguiente relea [eax].
    v.x = F32(((at(1) * v.y) + (at(2) * v.z)) + (static_cast<double>(v.x) * at(0)));
    v.y = F32(((at(4) * v.x) + (at(5) * v.y)) + (at(6) * v.z));
    v.z = F32(((at(8) * v.x) + (at(9) * v.y)) + (at(10) * v.z));
}

// 0x5fcc30: las columnas del giro; el orden de las sumas también fija los bits de la cámara.
glm::vec3 OriginalVectorIRotate(const glm::vec3& v, const OriginalMatrix& m)
{
    const auto at = [&m](std::size_t k) { return static_cast<double>(m[k]); };
    const double x = v.x;
    const double y = v.y;
    const double z = v.z;
    return {F32(((at(8) * z) + (at(4) * y)) + (x * at(0))), F32(((at(1) * x) + (at(9) * z)) + (at(5) * y)),
            F32(((at(2) * x) + (at(10) * z)) + (at(6) * y))};
}

// 0x5fccc0: como 0x5fcbd0 más la traslación de cada fila, sumada al final en 53 bits.
glm::vec3 OriginalVectorTransform(const glm::vec3& v, const OriginalMatrix& m)
{
    const auto at = [&m](std::size_t k) { return static_cast<double>(m[k]); };
    const double x = v.x;
    const double y = v.y;
    const double z = v.z;
    return {F32((((at(1) * y) + (at(2) * z)) + (x * at(0))) + at(3)),
            F32((((at(4) * x) + (at(5) * y)) + (at(6) * z)) + at(7)),
            F32((((at(8) * x) + (at(9) * y)) + (at(10) * z)) + at(11))};
}

// 0x4ec110: los tres productos por la escala en la pila; el de y y el de z se guardan en float antes de
// sumarles el origen, el de x no.
glm::vec3 OriginalTransformPosition(const OriginalMatrix& bone, const glm::vec3& offset, float bodyScale,
                                    const glm::vec3& bodyOrigin)
{
    const glm::vec3 p = OriginalVectorTransform(offset, bone);
    const double scale = bodyScale;
    const double x = scale * static_cast<double>(p.x);
    const float y = F32(scale * static_cast<double>(p.y));
    const float z = F32(scale * static_cast<double>(p.z));
    return {F32(x + static_cast<double>(bodyOrigin.x)), F32(static_cast<double>(y) + static_cast<double>(bodyOrigin.y)),
            F32(static_cast<double>(z) + static_cast<double>(bodyOrigin.z))};
}
} // namespace Client::Render::Models
