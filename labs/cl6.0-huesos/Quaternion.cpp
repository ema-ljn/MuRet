#include "Quaternion.h"

#include <cmath>

namespace Labs
{
namespace
{
// El double de 0x6ba718: el medio ángulo.
constexpr double Half = 0.5;
} // namespace

// Como en el desensamblado de 0x5fcd20: el medio ángulo de cada eje en la pila de la x87, el seno
// guardado en un float y el coseno en la pila sin redondear. Los cuatro productos, sin redondear
// hasta el resultado.
Quat AngleQuaternion(Vec3 radians)
{
    const double sy = static_cast<float>(std::sin(radians[2] * Half));
    const double cy = std::cos(radians[2] * Half);
    const double sp = static_cast<float>(std::sin(radians[1] * Half));
    const double cp = std::cos(radians[1] * Half);
    const double sr = static_cast<float>(std::sin(radians[0] * Half));
    const double cr = std::cos(radians[0] * Half);

    return Quat{
        .x = static_cast<float>(sr * cp * cy - cr * sp * sy),
        .y = static_cast<float>(cr * sp * cy + sr * cp * sy),
        .z = static_cast<float>(cr * cp * sy - sr * sp * cy),
        .w = static_cast<float>(cr * cp * cy + sr * sp * sy),
    };
}

// La de Half-Life: 1 − 2 (y² + z²) en la diagonal y los productos cruzados afuera, cada elemento
// redondeado una vez al guardarlo, como 0x5fcdb0.
Matrix34 QuaternionMatrix(Quat q)
{
    const double x = q.x;
    const double y = q.y;
    const double z = q.z;
    const double w = q.w;

    Matrix34 m{};
    m[0][0] = static_cast<float>(1.0 - 2.0 * y * y - 2.0 * z * z);
    m[1][0] = static_cast<float>(2.0 * (x * y + z * w));
    m[2][0] = static_cast<float>(2.0 * x * z - 2.0 * w * y);

    m[0][1] = static_cast<float>(2.0 * x * y - 2.0 * z * w);
    m[1][1] = static_cast<float>(1.0 - 2.0 * x * x - 2.0 * z * z);
    m[2][1] = static_cast<float>(2.0 * (x * w + z * y));

    m[0][2] = static_cast<float>(2.0 * (w * y + z * x));
    m[1][2] = static_cast<float>(2.0 * z * y - 2.0 * x * w);
    m[2][2] = static_cast<float>(1.0 - 2.0 * x * x - 2.0 * y * y);
    return m;
}
} // namespace Labs
