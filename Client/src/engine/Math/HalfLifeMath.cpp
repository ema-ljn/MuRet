#include "Math/HalfLifeMath.h"

#include "Math/RoundedMath.h"

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

namespace Client::Math
{
namespace
{
// The original's doubles: 1 at 0x6b8d60, 1e-8 at 0x6ba728 and 0.001 at 0x6b8a88. The one at 0x6ba720 is
// π/2 rounded to float and stored as a double (bits 0x3ff921fb60000000).
constexpr double One = 1.0;
constexpr double Epsilon = 1e-8;
constexpr double QuarterTurn = 1.5707963705062866;
constexpr double Tolerance = 0.001;

// Quaternion components in the order the original walks them: x, y, z and w.
using Components = std::array<float, 4>;

Components ToComponents(const glm::quat& q)
{
    return Components{q.x, q.y, q.z, q.w};
}

glm::quat FromComponents(const Components& c)
{
    return glm::quat::wxyz(c[3], c[0], c[1], c[2]);
}

// The loop at 0x5fceb2: both sums stay unrounded on the x87 stack and are compared there.
// Σ(p + q)² − Σ(p − q)² is 4 · p·q: q flips when the product is negative.
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

// 0x5fcef4: the product in the binary's order, z, y, w and x, also on the x87 stack.
double Dot(const Components& p, const Components& q)
{
    const double zy = static_cast<double>(q[2]) * p[2] + static_cast<double>(q[1]) * p[1];
    return zy + static_cast<double>(q[3]) * p[3] + static_cast<double>(p[0]) * q[0];
}

enum class SlerpCase
{
    Sines,    // sin((1 − t)·ω)/sin ω and sin(t·ω)/sin ω
    Linear,   // 1 − t and t, with 1 − cos ≤ 1e-8 and the cos in float
    Opposite, // 1 + cos ≤ 1e-8: towards a quaternion perpendicular to p
};

// The two comparisons at 0x5fcf13 and 0x5fcf26: the first with the cos left on the x87 stack, the second with the
// float stored by the FST at 0x5fcf10.
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
    const glm::vec3 half = radians * 0.5f;
    const glm::vec3 c{RoundedCos(half.x), RoundedCos(half.y), RoundedCos(half.z)};
    const glm::vec3 s{RoundedSin(half.x), RoundedSin(half.y), RoundedSin(half.z)};
    return glm::quat::wxyz(c.x * c.y * c.z + s.x * s.y * s.z, s.x * c.y * c.z - c.x * s.y * s.z,
                           c.x * s.y * c.z + s.x * c.y * s.z, c.x * c.y * s.z - s.x * s.y * c.z);
}

glm::quat QuaternionSlerp(const glm::quat& p, const glm::quat& q, float t)
{
    const Components first = ToComponents(p);
    const Components second = FlipTowards(first, ToComponents(q));
    const double cosine = Dot(first, second);
    const SlerpCase branch = Branch(cosine);

    if (branch == SlerpCase::Opposite)
    {
        // 0x5fcf9f: a quaternion perpendicular to p, and from p towards it with the sines of π/2. The loop covers three
        // components: w stays p.z, as in Half-Life.
        Components result{-first[1], first[0], -first[3], first[2]};
        const double fromP = RoundedSin((One - t) * QuarterTurn);
        const double toResult = RoundedSin(t * QuarterTurn);
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
        // 0x5fcf3c: acos of the cos in float; ω is stored as a float and so is its sine. Both weights stay on the x87
        // stack.
        const double omega = RoundedAcos(static_cast<double>(static_cast<float>(cosine)));
        const auto roundedOmega = static_cast<double>(static_cast<float>(omega));
        const auto sinOmega = static_cast<double>(static_cast<float>(RoundedSin(omega)));
        fromP = RoundedSin((One - t) * roundedOmega) / sinOmega;
        toQ = RoundedSin(roundedOmega * t) / sinOmega;
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
    return QuaternionMatrix(AngleQuaternion(glm::radians(degrees)));
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
} // namespace Client::Math
