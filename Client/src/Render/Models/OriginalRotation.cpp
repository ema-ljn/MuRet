#include "Render/Models/OriginalRotation.h"

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
} // namespace Client::Render::Models
