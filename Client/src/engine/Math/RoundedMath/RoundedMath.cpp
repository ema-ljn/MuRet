#include "Math/RoundedMath.h"

#include "shared/math/acos.h"
#include "shared/math/acosf.h"
#include "shared/math/cos.h"
#include "shared/math/cosf.h"
#include "shared/math/log10.h"
#include "shared/math/sin.h"
#include "shared/math/sinf.h"
#include "shared/math/tan.h"
#include "shared/math/tanf.h"

#include <cmath>

namespace Client::Math
{
namespace Libc = LIBC_NAMESPACE::shared;

// Without FMA, LLVM-libc 23.1.6 rounds the sine and the tangent of a double wrong below 2^-968, where its
// x * 2^-54 underflows. That small, the nearest double to both is x.
constexpr double Tiny = 0x1p-900;

float RoundedSin(float x)
{
    return Libc::sinf(x);
}

double RoundedSin(double x)
{
    return std::fabs(x) < Tiny ? x : Libc::sin(x);
}

float RoundedCos(float x)
{
    return Libc::cosf(x);
}

double RoundedCos(double x)
{
    return Libc::cos(x);
}

float RoundedTan(float x)
{
    return Libc::tanf(x);
}

double RoundedTan(double x)
{
    return std::fabs(x) < Tiny ? x : Libc::tan(x);
}

float RoundedAcos(float x)
{
    return Libc::acosf(x);
}

double RoundedAcos(double x)
{
    return Libc::acos(x);
}

double RoundedLog10(double x)
{
    return Libc::log10(x);
}
} // namespace Client::Math
