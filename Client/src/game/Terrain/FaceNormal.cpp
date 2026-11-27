#include "Terrain/FaceNormal.h"

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <limits>

namespace Client::Terrain
{
namespace
{
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);

// An FSTP float: the register's value, rounded to float.
float Spill(double value)
{
    return static_cast<float>(value);
}

// FLD float and FSUB float: the difference stays in the register.
double Difference(float minuend, float subtrahend)
{
    return static_cast<double>(minuend) - static_cast<double>(subtrahend);
}
} // namespace

glm::vec3 FaceNormal(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
{
    // 0x5fd00f..0x5fd027: uy, vz and uz stay on the stack; vy goes through a float.
    const double uy = Difference(p2.y, p1.y);
    const double vz = Difference(p3.z, p1.z);
    const double uz = Difference(p2.z, p1.z);
    const float vy = Spill(Difference(p3.y, p1.y));
    const float nx = Spill((vz * uy) - (static_cast<double>(vy) * uz));
    const float vx = Spill(Difference(p3.x, p1.x));
    const float ux = Spill(Difference(p2.x, p1.x));
    const float ny = Spill((static_cast<double>(vx) * uz) - (static_cast<double>(ux) * vz));
    // 0x5fd059..0x5fd07c: nz does not leave the register, and the length adds nz², ny² and nx², in that order.
    const double nz = (static_cast<double>(ux) * static_cast<double>(vy)) - (static_cast<double>(vx) * uy);
    const double length = std::sqrt(((nz * nz) + (static_cast<double>(ny) * static_cast<double>(ny))) +
                                    (static_cast<double>(nx) * static_cast<double>(nx)));
    return {Spill(static_cast<double>(nx) / length), Spill(static_cast<double>(ny) / length), Spill(nz / length)};
}
} // namespace Client::Terrain
