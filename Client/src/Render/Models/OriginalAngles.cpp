#include "Render/Models/OriginalAngles.h"

#include <cmath>

namespace Client::Render::Models
{
namespace
{
// 0x4e93f0.
constexpr float Tiny = 0.0001f;
constexpr double Degrees = static_cast<double>(57.2957763671875f);
constexpr double Quarter = 90.0;
constexpr double HalfTurn = 180.0;
constexpr double ThreeQuarters = 270.0;
constexpr double Turn = 360.0;

float F32(double value)
{
    return static_cast<float>(value);
}

// fld, fadd 360 y fstp: un negativo pasa a [0, 360) en float.
double Positive(float angle)
{
    const auto value = static_cast<double>(angle);
    return value < 0.0 ? static_cast<double>(F32(value + Turn)) : value;
}
} // namespace

double CreateAngle(float x1, float y1, float x2, float y2)
{
    const float nx = F32(static_cast<double>(x2) - static_cast<double>(x1));
    const float ny = F32(static_cast<double>(y2) - static_cast<double>(y1));
    if (std::fabs(nx) < Tiny)
    {
        return ny < 0.0f ? 0.0 : HalfTurn;
    }
    if (std::fabs(ny) < Tiny)
    {
        return nx < 0.0f ? ThreeQuarters : Quarter;
    }
    double angle = (std::atan(static_cast<double>(ny) / static_cast<double>(nx)) * Degrees) + Quarter;
    if (nx < 0.0f)
    {
        angle += HalfTurn;
    }
    return angle;
}

double TurnAngle(float from, float to, float step)
{
    double a = static_cast<double>(from);
    double b = static_cast<double>(to);
    const double d = static_cast<double>(step);
    if (a < 0.0)
    {
        a = static_cast<double>(F32(a + Turn));
    }
    if (b < 0.0)
    {
        b = static_cast<double>(F32(b + Turn));
    }
    if (a < HalfTurn)
    {
        const auto low = static_cast<double>(F32(a - d));
        if (!(b < a + d) && a + HalfTurn > b)
        {
            return a + d;
        }
        if (!(low < 0.0))
        {
            if (a + HalfTurn <= b || b < low)
            {
                return low;
            }
            return b;
        }
        if (a + HalfTurn > b || low + Turn <= b)
        {
            return b;
        }
        return low + Turn;
    }
    const auto high = static_cast<double>(F32(a + d));
    if (b < a - d && a - HalfTurn <= b)
    {
        return a - d;
    }
    if (high < Turn)
    {
        if (a - HalfTurn > b)
        {
            return high;
        }
        if (b < high)
        {
            return b;
        }
        return high;
    }
    if (a - HalfTurn <= b || high - Turn > b)
    {
        return b;
    }
    return high - Turn;
}

double AngleDistance(float from, float to, bool absolute)
{
    const double a = Positive(from);
    const double b = Positive(to);
    double distance = b - a;
    if (a < HalfTurn)
    {
        // 0x4e9744: fadd 180, fcomp `to` y `test ah, 0x41`: sigue con la resta si a + 180 > b.
        if (!(a + HalfTurn > b))
        {
            distance = (Turn - b) + a;
        }
    }
    else if (a - HalfTurn > b)
    {
        // 0x4e9764: fsub 180, fcomp `to` y `test ah, 0x41` con jne: la vuelta solo si a − 180 > b.
        distance = (Turn - a) + b;
    }
    if (absolute && distance < 0.0)
    {
        distance = -distance;
    }
    return distance;
}
} // namespace Client::Render::Models
