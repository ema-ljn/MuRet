#include "Render/Models/OriginalRetainedMath.h"

#include <boost/multiprecision/cpp_bin_float.hpp> // IWYU pragma: keep; define el backend.
#include <boost/multiprecision/detail/default_ops.hpp>
#include <boost/multiprecision/fwd.hpp>

#include <bit>
#include <cstdint>
#include <limits>

namespace Client::Render::Models::Detail
{
namespace
{
namespace Mp = boost::multiprecision;
template <unsigned Bits>
using Binary = Mp::number<Mp::cpp_bin_float<Bits, Mp::digit_base_2, void, std::int32_t>, Mp::et_off>;
using Rounded = Binary<53>;
using Retained = Binary<64>;
using Wide = Binary<113>;
static_assert(std::numeric_limits<Rounded>::digits == 53);
static_assert(std::numeric_limits<Retained>::digits == 64);
static_assert(std::numeric_limits<Wide>::digits == 113);

// Constantes binary32 archivadas de las dos Waves y 0x4e9481 (ADR0014).
constexpr float FirstFrequency = std::bit_cast<float>(0x3a83126fU);
constexpr float SecondFrequency = std::bit_cast<float>(0x3a03126fU);
constexpr float SecondPhase = 50.0f;
constexpr float Degrees = std::bit_cast<float>(0x42652ee0U);
constexpr int RetainedDigits = 64;
constexpr int SignedMagnitudeBits = 63;

Retained64 StoreRetained(const Retained& value)
{
    const bool negative = Mp::signbit(value);
    if (Mp::isnan(value))
    {
        return {0, 0, RetainedClass::NaN, negative};
    }
    if (Mp::isinf(value))
    {
        return {0, 0, RetainedClass::Infinity, negative};
    }
    if (value == 0)
    {
        return {0, 0, RetainedClass::Finite, negative};
    }
    int exponent = 0;
    const Retained fraction = Mp::frexp(Mp::abs(value), &exponent);
    const Retained scaled = Mp::ldexp(fraction, RetainedDigits);
    return {scaled.convert_to<std::uint64_t>(), exponent - RetainedDigits, RetainedClass::Finite, negative};
}

Retained LoadRetained(const Retained64& value)
{
    Retained result;
    if (value.classification == RetainedClass::NaN)
    {
        result = std::numeric_limits<Retained>::quiet_NaN();
    }
    else if (value.classification == RetainedClass::Infinity)
    {
        result = std::numeric_limits<Retained>::infinity();
    }
    else
    {
        result = Mp::ldexp(Retained(value.significand), value.exponent);
    }
    return value.negative ? -result : result;
}

LeafSine EvaluateLeafArgument(const Rounded& argument)
{
    const Rounded limit = Mp::ldexp(Rounded(1), SignedMagnitudeBits);
    const bool argumentRetained = Mp::isfinite(argument) && Mp::abs(argument) >= limit;
    if (argumentRetained)
    {
        return {StoreRetained(Retained(argument)), true};
    }
    if (!Mp::isfinite(argument))
    {
        return {StoreRetained(std::numeric_limits<Retained>::quiet_NaN()), false};
    }
    const Wide sine = Mp::sin(Wide(argument));
    return {StoreRetained(Retained(sine)), false};
}

Rounded DegreesProduct(const Retained& phi)
{
    // 64 + 24 bits caben en113: se redondea una sola vez a53 después del producto.
    const Wide product = Wide(phi) * Wide(Degrees);
    return Rounded(product);
}
} // namespace

LeafSine FirstLeafSine(float time)
{
    const Rounded argument = Rounded(time) * Rounded(FirstFrequency);
    return EvaluateLeafArgument(argument);
}

LeafSine SecondLeafSine(float time)
{
    const Rounded product = Rounded(time) * Rounded(SecondFrequency);
    const Rounded argument = product + Rounded(SecondPhase);
    return EvaluateLeafArgument(argument);
}

SignedConversion TruncateLeafSine(const Retained64& value)
{
    const Retained retained = LoadRetained(value);
    const Retained limit = Mp::ldexp(Retained(1), SignedMagnitudeBits);
    if (!Mp::isfinite(retained) || retained < -limit || retained >= limit)
    {
        return {std::numeric_limits<std::int64_t>::min(), false};
    }
    const Retained integral = Mp::trunc(retained);
    return {integral.convert_to<std::int64_t>(), true};
}

double AtanDegreesProductPC53(double quotient)
{
    const Wide phiWide = Mp::atan(Wide(quotient));
    const Retained phi(phiWide);
    return DegreesProduct(phi).convert_to<double>();
}
} // namespace Client::Render::Models::Detail
