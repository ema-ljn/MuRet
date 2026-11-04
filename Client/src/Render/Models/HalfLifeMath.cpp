#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <cmath>
#include <cstddef>

namespace Client::Render::Models
{
namespace
{
// Los doubles del original: 1 en 0x6b8d60, 1e-8 en 0x6ba728 y 0,001 en 0x6b8a88. El de 0x6ba720 es
// π/2 redondeado a float y guardado en double (los bits 0x3ff921fb60000000).
constexpr double One = 1.0;
constexpr double Epsilon = 1e-8;
constexpr double QuarterTurn = 1.5707963705062866;
constexpr double Tolerance = 0.001;

// Los cuaterniones como los recorre el original: x, y, z y w.
using Components = std::array<float, 4>;

Components ToComponents(const glm::quat& q)
{
    return Components{q.x, q.y, q.z, q.w};
}

glm::quat FromComponents(const Components& c)
{
    return glm::quat::wxyz(c[3], c[0], c[1], c[2]);
}

// El lazo de 0x5fceb2: las dos sumas quedan en la pila de la x87, sin redondear, y se comparan ahí.
// Σ(p + q)² − Σ(p − q)² es 4 · p·q: q se da vuelta cuando el producto es negativo.
Components FlipTowards(const Components& p, Components q)
{
    double minus = 0;
    double plus = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double difference = static_cast<double>(p[i]) - q[i];
        minus += difference * difference;
        const double sum = static_cast<double>(q[i]) + p[i];
        plus += sum * sum;
    }
    if (minus > plus)
    {
        for (float& component : q)
        {
            component = -component;
        }
    }
    return q;
}

// 0x5fcef4: el producto en el orden del binario, z, y, w y x, también en la pila.
double Dot(const Components& p, const Components& q)
{
    const double zy = static_cast<double>(q[2]) * p[2] + static_cast<double>(q[1]) * p[1];
    return zy + static_cast<double>(q[3]) * p[3] + static_cast<double>(p[0]) * q[0];
}

// Por dónde sale el slerp.
enum class SlerpCase
{
    Sines,    // sin((1 − t)·ω)/sin ω y sin(t·ω)/sin ω
    Linear,   // 1 − t y t, con 1 − cos ≤ 1e-8 y el cos en float
    Opposite, // 1 + cos ≤ 1e-8: hacia un cuaternión perpendicular a p
};

// Las dos comparaciones de 0x5fcf13 y 0x5fcf26: la primera con el cos que quedó en la pila, la
// segunda con el float que guardó el FST de 0x5fcf10.
SlerpCase Branch(double cosine)
{
    if (!(cosine + One > Epsilon))
    {
        return SlerpCase::Opposite;
    }
    const auto rounded = static_cast<double>(static_cast<float>(cosine));
    return One - rounded > Epsilon ? SlerpCase::Sines : SlerpCase::Linear;
}
} // namespace

glm::quat AngleQuaternion(const glm::vec3& radians)
{
    return glm::quat(radians);
}

glm::quat QuaternionSlerp(const glm::quat& p, const glm::quat& q, float t)
{
    const Components first = ToComponents(p);
    const Components second = FlipTowards(first, ToComponents(q));
    const double cosine = Dot(first, second);
    const SlerpCase branch = Branch(cosine);

    if (branch == SlerpCase::Opposite)
    {
        // 0x5fcf9f: un cuaternión perpendicular a p, y de p hacia él con los senos de π/2. El lazo es
        // de tres: la w queda en p.z, como en Half-Life.
        Components result{-first[1], first[0], -first[3], first[2]};
        const double fromP = std::sin((One - t) * QuarterTurn);
        const double toResult = std::sin(t * QuarterTurn);
        for (std::size_t i = 0; i < 3; ++i)
        {
            result[i] = static_cast<float>(fromP * first[i] + toResult * result[i]);
        }
        return FromComponents(result);
    }

    double fromP = One - t;
    double toQ = t;
    if (branch == SlerpCase::Sines)
    {
        // 0x5fcf3c: acos del cos en float; ω se guarda en float y su seno también. Los dos pesos
        // quedan en la pila.
        const double omega = std::acos(static_cast<double>(static_cast<float>(cosine)));
        const auto roundedOmega = static_cast<double>(static_cast<float>(omega));
        const auto sinOmega = static_cast<double>(static_cast<float>(std::sin(omega)));
        fromP = std::sin((One - t) * roundedOmega) / sinOmega;
        toQ = std::sin(roundedOmega * t) / sinOmega;
    }

    Components result{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        result[i] = static_cast<float>(fromP * first[i] + toQ * second[i]);
    }
    return FromComponents(result);
}

bool QuaternionsAlmostEqual(const glm::quat& p, const glm::quat& q)
{
    const Components first = ToComponents(p);
    const Components second = ToComponents(q);
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (std::fabs(static_cast<double>(first[i]) - second[i]) > Tolerance)
        {
            return false;
        }
    }
    return true;
}

BoneTransform QuaternionMatrix(const glm::quat& q)
{
    return BoneTransform(glm::mat3_cast(q));
}

BoneTransform AngleMatrix(const glm::vec3& degrees)
{
    return QuaternionMatrix(glm::quat(glm::radians(degrees)));
}

BoneTransform ConcatTransforms(const BoneTransform& parent, const BoneTransform& child)
{
    return parent * glm::mat4(child);
}

glm::vec3 VectorTransform(const glm::vec3& v, const BoneTransform& m)
{
    return m * glm::vec4(v, 1.0f);
}

glm::vec3 VectorRotate(const glm::vec3& v, const BoneTransform& m)
{
    return glm::mat3(m) * v;
}
} // namespace Client::Render::Models
