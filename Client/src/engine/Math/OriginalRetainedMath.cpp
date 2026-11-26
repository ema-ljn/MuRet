#include "Math/OriginalRetainedMath.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

// ADR0014: own binary arithmetic, no libraries. An intermediate value is sign, exponent and a 256-bit mantissa in
// 32-bit limbs: every partial product fits in uint64, so no __int128, long double or floating-point operation is
// needed. All integer and truncated, relative error below 2^-250; only the step to 64 bits rounds to nearest.
namespace Client::Math::Detail
{
namespace
{
// Archived binary32 constants of both Waves and 0x4e9481 (ADR0014).
constexpr float FirstFrequency = std::bit_cast<float>(0x3a83126fU);
constexpr float SecondFrequency = std::bit_cast<float>(0x3a03126fU);
constexpr float SecondPhase = 50.0f;
constexpr float Degrees = std::bit_cast<float>(0x42652ee0U);
constexpr int RetainedDigits = 64;
constexpr int SignedMagnitudeBits = 63;
constexpr double ArgumentLimit = 0x1p63;

constexpr int LimbBits = 32;
constexpr std::size_t LimbCount = 8;
constexpr int MantissaBits = LimbBits * static_cast<int>(LimbCount);
using Limbs = std::array<std::uint32_t, LimbCount>;

// (-1)^negative · mantissa · 2^(exponent - 256), limbs from least to most significant. When nonzero, bit 255 is set
// and the value lies in [2^(exponent - 1), 2^exponent).
struct Wide
{
    Limbs mantissa;
    int exponent;
    bool negative;
};

constexpr Wide Constant(const Limbs& mostSignificantFirst, int exponent)
{
    Wide result{{}, exponent, false};
    for (std::size_t index = 0; index < LimbCount; ++index)
    {
        result.mantissa[index] = mostSignificantFirst[LimbCount - 1 - index];
    }
    return result;
}

constexpr Wide Zero{{}, 0, false};
constexpr Wide One = Constant({0x80000000U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}, 1);

// Tables generated with Python integers, no floats: pi by Machin, 16·atan(1/5) - 4·atan(1/239), and atan(k/8) by
// Euler's series, in 1664-bit fixed point with an error bound. Each word is equal at both ends of the interval and
// matches mpmath 1.4.1 at 2000 bits. TwoOverPi is floor(2^385 / pi): the first 384 bits of 2/pi. The rest, rounded to
// nearest at 256 bits.
constexpr int TwoOverPiBits = 384;
constexpr std::array<std::uint32_t, 12> TwoOverPi{
    0xa2f9836eU, 0x4e441529U, 0xfc2757d1U, 0xf534ddc0U, 0xdb629599U, 0x3c439041U,
    0xfe5163abU, 0xdebbc561U, 0xb7246e3aU, 0x424dd2e0U, 0x06492eeaU, 0x09d1921cU,
};
constexpr Limbs HalfPiDigits{0xc90fdaa2U, 0x2168c234U, 0xc4c6628bU, 0x80dc1cd1U,
                             0x29024e08U, 0x8a67cc74U, 0x020bbea6U, 0x3b139b22U};
constexpr Wide HalfPi = Constant(HalfPiDigits, 1);
constexpr std::array<Wide, 8> ArctangentOfEighths{
    Constant({0xfeadd4d5U, 0x617b6e32U, 0xc897989fU, 0x3e888ef7U, 0x8b3957d9U, 0x5d9ad922U, 0xec93577dU, 0xfb6c1769U},
             -3),
    Constant({0xfadbafc9U, 0x6406eb15U, 0x6dc79ef5U, 0xf7a217e5U, 0xaa7fa903U, 0x88b3836bU, 0x7a3a767cU, 0x9449a766U},
             -2),
    Constant({0xb7b0ca0fU, 0x26f78473U, 0x8aa32122U, 0xdcfe4483U, 0x3d843977U, 0xae5455ccU, 0x4733e5caU, 0x334908e0U},
             -1),
    Constant({0xed63382bU, 0x0dda7b45U, 0x6fe445ecU, 0xbc3a8d03U, 0x6e141587U, 0x261cdf00U, 0xe2cf16e6U, 0xe962470aU},
             -1),
    Constant({0x8f005d5eU, 0xf7f59f9bU, 0x5c835e16U, 0x65c43747U, 0x918a67e0U, 0x652b375cU, 0xf53da46dU, 0x13389eb2U},
             0),
    Constant({0xa4bc7d19U, 0x34f70924U, 0x19a87f2aU, 0x457dac9eU, 0xe3f08689U, 0xeeb2b9e7U, 0x21486665U, 0x8cc4ef3bU},
             0),
    Constant({0xb8053e2bU, 0xc2319e73U, 0xcb2da552U, 0x10a4443dU, 0x3d7aecc1U, 0x14c79a80U, 0xa012155fU, 0x64cae530U},
             0),
    Constant(HalfPiDigits, 0),
};

// A series term below 2^-262 no longer moves the 64-bit rounding of a sum close to one.
constexpr int NegligibleExponent = -262;
// With |r| <= pi/4 or |u| <= 1/16 the series stop before term 34; this is only a hard bound.
constexpr std::uint32_t SeriesTermLimit = 64;
// Newton doubles the seed's 30 bits: 60, 120, 240 and the mantissa limit.
constexpr int ReciprocalSteps = 4;
// Below 1/2 the sine is evaluated without reduction; atan reduces with k/8 from 1/16.
constexpr double DirectSineLimit = 0.5;
constexpr int EighthsShift = 3;

bool IsZero(const Wide& value)
{
    return value.mantissa[LimbCount - 1] == 0U;
}

Wide Negate(Wide value)
{
    value.negative = !value.negative;
    return value;
}

std::array<std::uint32_t, 2> SplitLimbs(std::uint64_t value)
{
    return {static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value >> LimbBits)};
}

std::uint32_t LimbAt(std::span<const std::uint32_t> limbs, int index)
{
    if (index < 0 || index >= static_cast<int>(limbs.size()))
    {
        return 0U;
    }
    return limbs[static_cast<std::size_t>(index)];
}

// Bits [position, position + 32) of the integer; zero outside the array.
std::uint32_t BitsAt(std::span<const std::uint32_t> limbs, int position)
{
    const int index = position >= 0 ? position / LimbBits : -((LimbBits - 1 - position) / LimbBits);
    const int offset = position - index * LimbBits;
    const std::uint32_t low = LimbAt(limbs, index);
    if (offset == 0)
    {
        return low;
    }
    return (low >> offset) | (LimbAt(limbs, index + 1) << (LimbBits - offset));
}

int BitLength(std::span<const std::uint32_t> limbs)
{
    for (std::size_t index = limbs.size(); index > 0; --index)
    {
        if (limbs[index - 1] != 0U)
        {
            return static_cast<int>(index - 1) * LimbBits + static_cast<int>(std::bit_width(limbs[index - 1]));
        }
    }
    return 0;
}

// integer · 2^scale normalized to 256 bits; extra low bits are truncated.
Wide FromInteger(std::span<const std::uint32_t> integer, int scale, bool negative)
{
    const int length = BitLength(integer);
    if (length == 0)
    {
        return Zero;
    }
    Wide result{{}, length + scale, negative};
    const int lowest = length - MantissaBits;
    for (std::size_t index = 0; index < LimbCount; ++index)
    {
        result.mantissa[index] = BitsAt(integer, lowest + static_cast<int>(index) * LimbBits);
    }
    return result;
}

Wide Multiply(const Wide& left, const Wide& right)
{
    if (IsZero(left) || IsZero(right))
    {
        return Zero;
    }
    std::array<std::uint32_t, 2 * LimbCount> product{};
    for (std::size_t i = 0; i < LimbCount; ++i)
    {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < LimbCount; ++j)
        {
            const std::uint64_t partial = std::uint64_t{left.mantissa[i]} * right.mantissa[j] + product[i + j] + carry;
            product[i + j] = static_cast<std::uint32_t>(partial);
            carry = partial >> LimbBits;
        }
        product[i + LimbCount] = static_cast<std::uint32_t>(carry);
    }
    return FromInteger(product, left.exponent + right.exponent - 2 * MantissaBits, left.negative != right.negative);
}

bool MagnitudeBelow(const Wide& left, const Wide& right)
{
    if (left.exponent != right.exponent)
    {
        return left.exponent < right.exponent;
    }
    for (std::size_t index = LimbCount; index > 0; --index)
    {
        if (left.mantissa[index - 1] != right.mantissa[index - 1])
        {
            return left.mantissa[index - 1] < right.mantissa[index - 1];
        }
    }
    return false;
}

// Aligns the smaller addend with a guard limb and truncates what falls below. With different signs it subtracts the
// smaller from the larger, so the result never goes negative.
Wide Add(const Wide& left, const Wide& right)
{
    if (IsZero(left) || IsZero(right))
    {
        return IsZero(left) ? right : left;
    }
    const bool swap = MagnitudeBelow(left, right);
    const Wide& larger = swap ? right : left;
    const Wide& smaller = swap ? left : right;
    const int shift = larger.exponent - smaller.exponent;
    if (shift > MantissaBits + LimbBits)
    {
        return larger;
    }
    const bool subtract = larger.negative != smaller.negative;
    std::array<std::uint32_t, LimbCount + 2> sum{};
    std::uint64_t carry = 0;
    for (std::size_t index = 0; index <= LimbCount; ++index)
    {
        const std::uint64_t largerLimb = index == 0 ? 0U : larger.mantissa[index - 1];
        const std::uint64_t alignedLimb =
            BitsAt(smaller.mantissa, static_cast<int>(index) * LimbBits - LimbBits + shift);
        // carry is the sum's carry or the subtraction's borrow.
        const std::uint64_t operand = alignedLimb + carry;
        const std::uint64_t digit = subtract ? largerLimb - operand : largerLimb + operand;
        sum[index] = static_cast<std::uint32_t>(digit);
        carry = subtract ? (largerLimb < operand ? 1U : 0U) : digit >> LimbBits;
    }
    sum[LimbCount + 1] = subtract ? 0U : static_cast<std::uint32_t>(carry);
    return FromInteger(sum, larger.exponent - MantissaBits - LimbBits, larger.negative);
}

Wide MultiplySmall(const Wide& value, std::uint32_t factor)
{
    std::array<std::uint32_t, LimbCount + 1> product{};
    std::uint64_t carry = 0;
    for (std::size_t index = 0; index < LimbCount; ++index)
    {
        const std::uint64_t partial = std::uint64_t{value.mantissa[index]} * factor + carry;
        product[index] = static_cast<std::uint32_t>(partial);
        carry = partial >> LimbBits;
    }
    product[LimbCount] = static_cast<std::uint32_t>(carry);
    return FromInteger(product, value.exponent - MantissaBits, value.negative);
}

// Long division by a small integer, with one extra quotient limb as guard.
Wide DivideSmall(const Wide& value, std::uint32_t divisor)
{
    std::array<std::uint32_t, LimbCount + 1> quotient{};
    std::uint64_t remainder = 0;
    for (std::size_t index = LimbCount + 1; index > 0; --index)
    {
        const std::uint64_t digit = index > 1 ? value.mantissa[index - 2] : 0U;
        const std::uint64_t current = (remainder << LimbBits) | digit;
        quotient[index - 1] = static_cast<std::uint32_t>(current / divisor);
        remainder = current % divisor;
    }
    return FromInteger(quotient, value.exponent - MantissaBits - LimbBits, value.negative);
}

// 1/value by Newton, y ← y + y·(1 - value·y), from an integer seed with the top 32 bits.
Wide Reciprocal(const Wide& value)
{
    const std::uint64_t top = value.mantissa[LimbCount - 1];
    const std::uint64_t seed = std::numeric_limits<std::uint64_t>::max() / top;
    Wide estimate = FromInteger(SplitLimbs(seed), -LimbBits - value.exponent, value.negative);
    for (int step = 0; step < ReciprocalSteps; ++step)
    {
        const Wide error = Add(One, Negate(Multiply(value, estimate)));
        estimate = Add(estimate, Multiply(estimate, error));
    }
    return estimate;
}

bool Negligible(const Wide& term)
{
    return IsZero(term) || term.exponent < NegligibleExponent;
}

// sin(r) = r · Σ (-1)^k r^(2k) / (2k + 1)!, with |r| <= pi/4.
Wide SineSeries(const Wide& angle)
{
    const Wide square = Multiply(angle, angle);
    Wide sum = One;
    Wide term = One;
    for (std::uint32_t k = 1; k < SeriesTermLimit; ++k)
    {
        term = DivideSmall(Multiply(term, square), (2 * k) * (2 * k + 1));
        if (Negligible(term))
        {
            break;
        }
        sum = Add(sum, k % 2 == 1 ? Negate(term) : term);
    }
    return Multiply(angle, sum);
}

// cos(r) = Σ (-1)^k r^(2k) / (2k)!, with |r| <= pi/4.
Wide CosineSeries(const Wide& angle)
{
    const Wide square = Multiply(angle, angle);
    Wide sum = One;
    Wide term = One;
    for (std::uint32_t k = 1; k < SeriesTermLimit; ++k)
    {
        term = DivideSmall(Multiply(term, square), (2 * k - 1) * (2 * k));
        if (Negligible(term))
        {
            break;
        }
        sum = Add(sum, k % 2 == 1 ? Negate(term) : term);
    }
    return sum;
}

// atan(u) = u · Σ (-1)^n u^(2n) / (2n + 1), with |u| <= 1/16.
Wide ArctangentSeries(const Wide& value)
{
    const Wide square = Multiply(value, value);
    Wide sum = One;
    Wide power = One;
    for (std::uint32_t n = 1; n < SeriesTermLimit; ++n)
    {
        power = Multiply(power, square);
        const Wide term = DivideSmall(power, 2 * n + 1);
        if (Negligible(term))
        {
            break;
        }
        sum = Add(sum, n % 2 == 1 ? Negate(term) : term);
    }
    return Multiply(value, sum);
}

void KeepLowBits(std::span<std::uint32_t> limbs, int count)
{
    for (std::size_t index = 0; index < limbs.size(); ++index)
    {
        const int first = static_cast<int>(index) * LimbBits;
        if (first >= count)
        {
            limbs[index] = 0U;
        }
        else if (count - first < LimbBits)
        {
            limbs[index] &= (1U << (count - first)) - 1U;
        }
    }
}

void NegateInteger(std::span<std::uint32_t> limbs)
{
    std::uint64_t carry = 1;
    for (std::uint32_t& limb : limbs)
    {
        const std::uint64_t sum = std::uint64_t{std::numeric_limits<std::uint32_t>::max() - limb} + carry;
        limb = static_cast<std::uint32_t>(sum);
        carry = sum >> LimbBits;
    }
}

struct BinaryParts
{
    std::uint64_t significand;
    int exponent;
    bool negative;
};

// |value| = significand · 2^exponent for a finite IEEE binary, subnormals included.
template <class Float, class Bits> BinaryParts Decompose(Float value)
{
    constexpr int FractionBits = std::numeric_limits<Float>::digits - 1;
    constexpr int StorageBits = static_cast<int>(sizeof(Bits)) * 8;
    constexpr int QuantumExponent = std::numeric_limits<Float>::min_exponent - std::numeric_limits<Float>::digits;
    constexpr Bits BiasedMask = (Bits{1} << (StorageBits - 1 - FractionBits)) - 1;
    const auto bits = std::bit_cast<Bits>(value);
    const auto fraction = static_cast<std::uint64_t>(bits & ((Bits{1} << FractionBits) - 1));
    const auto biased = static_cast<int>((bits >> FractionBits) & BiasedMask);
    const bool negative = (bits >> (StorageBits - 1)) != 0;
    if (biased == 0)
    {
        return {fraction, QuantumExponent, negative};
    }
    return {fraction | (std::uint64_t{1} << FractionBits), QuantumExponent + biased - 1, negative};
}

BinaryParts DecomposeDouble(double value)
{
    return Decompose<double, std::uint64_t>(value);
}

BinaryParts DecomposeFloat(float value)
{
    return Decompose<float, std::uint32_t>(value);
}

Wide WideMagnitude(const BinaryParts& parts)
{
    return FromInteger(SplitLimbs(parts.significand), parts.exponent, false);
}

struct Reduced
{
    Wide angle;
    std::uint32_t quadrant;
};

// Payne-Hanek: |x| = significand · 2^exponent with 1/2 <= |x| < 2^63, so exponent <= 10. The integer product by
// TwoOverPi gives x·2/pi = n + f with error below 2^63 · 2^-384 = 2^-321. For every double in that range
// |f| >= 2^-61.14 (continued-fraction bound, ADR0014), and the mantissa of f keeps more than 256 exact bits. Returns
// r = f·pi/2 and n mod 4, with |f| <= 1/2.
Reduced ReduceQuarterTurns(const BinaryParts& magnitude)
{
    std::array<std::uint32_t, TwoOverPi.size() + 2> product{};
    const std::array<std::uint32_t, 2> factor = SplitLimbs(magnitude.significand);
    for (std::size_t i = 0; i < factor.size(); ++i)
    {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < TwoOverPi.size(); ++j)
        {
            const std::uint32_t word = TwoOverPi[TwoOverPi.size() - 1 - j];
            const std::uint64_t partial = std::uint64_t{factor[i]} * word + product[i + j] + carry;
            product[i + j] = static_cast<std::uint32_t>(partial);
            carry = partial >> LimbBits;
        }
        product[i + TwoOverPi.size()] = static_cast<std::uint32_t>(carry);
    }
    const int point = TwoOverPiBits - magnitude.exponent;
    std::uint32_t quadrant = BitsAt(product, point) & 3U;
    const bool upperHalf = (BitsAt(product, point - 1) & 1U) != 0U;
    KeepLowBits(product, point);
    if (upperHalf)
    {
        // f >= 1/2: take f - 1, negative, and the next quadrant.
        NegateInteger(product);
        KeepLowBits(product, point);
        ++quadrant;
    }
    const Wide fraction = FromInteger(product, -point, upperHalf);
    return {Multiply(fraction, HalfPi), quadrant & 3U};
}

// Rounds to 64 bits, to nearest with ties to even, into the canonical retained form.
Retained64 ToRetained(const Wide& value)
{
    if (IsZero(value))
    {
        return {0, 0, RetainedClass::Finite, value.negative};
    }
    std::uint64_t significand =
        (std::uint64_t{value.mantissa[LimbCount - 1]} << LimbBits) | value.mantissa[LimbCount - 2];
    const std::uint32_t roundingLimb = value.mantissa[LimbCount - 3];
    constexpr std::uint32_t HalfBit = 0x80000000U;
    bool sticky = (roundingLimb & (HalfBit - 1U)) != 0U;
    for (std::size_t index = 0; index + 3 < LimbCount; ++index)
    {
        sticky = sticky || value.mantissa[index] != 0U;
    }
    std::int32_t exponent = value.exponent - RetainedDigits;
    if ((roundingLimb & HalfBit) != 0U && (sticky || (significand & 1U) != 0U))
    {
        ++significand;
        if (significand == 0U)
        {
            significand = std::uint64_t{1} << (RetainedDigits - 1);
            ++exponent;
        }
    }
    return {significand, exponent, RetainedClass::Finite, value.negative};
}

// sin or cos of a finite, nonzero double with |x| < 2^63.
Retained64 RetainedSineOrCosine(double argument, bool cosine)
{
    const BinaryParts parts = DecomposeDouble(argument);
    Reduced reduced{WideMagnitude(parts), 0U};
    if (std::abs(argument) >= DirectSineLimit)
    {
        reduced = ReduceQuarterTurns(parts);
    }
    // cos(x) = sin(x + pi/2); sin(-x) = -sin(x) and cos(-x) = cos(x).
    const std::uint32_t quadrant = reduced.quadrant + (cosine ? 1U : 0U);
    Wide value = (quadrant & 1U) != 0U ? CosineSeries(reduced.angle) : SineSeries(reduced.angle);
    const bool negate = ((quadrant & 2U) != 0U) != (!cosine && parts.negative);
    return ToRetained(negate ? Negate(value) : value);
}

// round(8t) for 0 < t <= 1, from the top 64 bits of t. A different tie-break at the edge does not matter:
// |t - k/8| stays bounded by 1/16 + 2^-60.
std::uint32_t NearestEighths(const Wide& value)
{
    if (value.exponent < -EighthsShift)
    {
        return 0U;
    }
    const std::uint64_t top =
        (std::uint64_t{value.mantissa[LimbCount - 1]} << LimbBits) | value.mantissa[LimbCount - 2];
    const int shift = RetainedDigits - EighthsShift - value.exponent;
    return static_cast<std::uint32_t>(((top >> (shift - 1)) + 1U) >> 1);
}

// atan(t) for 0 < t <= 1: atan(k/8) + atan((t - k/8) / (1 + t·k/8)).
Wide ArctangentUnit(const Wide& value)
{
    const std::uint32_t eighths = NearestEighths(value);
    if (eighths == 0U)
    {
        return ArctangentSeries(value);
    }
    const std::array<std::uint32_t, 1> center{eighths};
    const Wide numerator = Add(value, Negate(FromInteger(center, -EighthsShift, false)));
    Wide product = MultiplySmall(value, eighths);
    product.exponent -= EighthsShift;
    const Wide reduced = Multiply(numerator, Reciprocal(Add(One, product)));
    return Add(ArctangentOfEighths[eighths - 1], ArctangentSeries(reduced));
}

// atan of a non-NaN double; atan(±inf) = ±pi/2 and atan(±0) = ±0.
Retained64 RetainedArctangent(double quotient)
{
    if (quotient == 0.0)
    {
        return {0, 0, RetainedClass::Finite, std::signbit(quotient)};
    }
    if (std::isinf(quotient))
    {
        return ToRetained(quotient < 0.0 ? Negate(HalfPi) : HalfPi);
    }
    const BinaryParts parts = DecomposeDouble(quotient);
    const Wide magnitude = WideMagnitude(parts);
    const bool complement = std::abs(quotient) > 1.0;
    Wide angle = ArctangentUnit(complement ? Reciprocal(magnitude) : magnitude);
    if (complement)
    {
        angle = Add(HalfPi, Negate(angle));
    }
    angle.negative = parts.negative;
    return ToRetained(angle);
}

// mantissa · 2^exponent with mantissa < 2^53: the R53 with an unbounded exponent, before the double.
struct Rounded53
{
    std::uint64_t mantissa;
    std::int64_t exponent;
    bool negative;
};

constexpr int DoubleDigits = std::numeric_limits<double>::digits;
constexpr int DoubleMaxExponent = std::numeric_limits<double>::max_exponent - 1;
constexpr int DoubleQuantumExponent = std::numeric_limits<double>::min_exponent - DoubleDigits;
constexpr int DoubleFractionBits = DoubleDigits - 1;
constexpr std::uint64_t DoubleSignBit = std::uint64_t{1} << 63;
constexpr std::uint64_t DoubleInfinityBits = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity());

// value · 2^-count to the nearest integer, ties to even. count <= 0 shifts left.
std::uint64_t ShiftNearestEven(std::uint64_t value, std::int64_t count)
{
    constexpr int WordBits = 64;
    if (count <= 0)
    {
        return value << static_cast<int>(-count);
    }
    if (count > WordBits)
    {
        return 0U;
    }
    const auto shift = static_cast<int>(count);
    const std::uint64_t kept = shift == WordBits ? 0U : value >> shift;
    const std::uint64_t remainder = shift == WordBits ? value : value & ((std::uint64_t{1} << shift) - 1U);
    const std::uint64_t half = std::uint64_t{1} << (shift - 1);
    const bool up = remainder > half || (remainder == half && (kept & 1U) != 0U);
    return kept + (up ? 1U : 0U);
}

// A single rounding of mantissa · 2^exponent to the double grid, to nearest with ties to even: subnormals with fewer
// bits and overflow to infinity, like convert_to<double>.
double NearestDouble(std::uint64_t mantissa, std::int64_t exponent, bool negative)
{
    const std::uint64_t sign = negative ? DoubleSignBit : 0U;
    if (mantissa == 0U)
    {
        return std::bit_cast<double>(sign);
    }
    const std::int64_t leading = exponent + static_cast<int>(std::bit_width(mantissa)) - 1;
    if (leading > DoubleMaxExponent)
    {
        return std::bit_cast<double>(sign | DoubleInfinityBits);
    }
    std::int64_t quantum = leading - DoubleFractionBits;
    quantum = quantum < DoubleQuantumExponent ? DoubleQuantumExponent : quantum;
    std::uint64_t integer = ShiftNearestEven(mantissa, quantum - exponent);
    if ((integer >> DoubleDigits) != 0U)
    {
        integer >>= 1;
        ++quantum;
    }
    if ((integer >> DoubleFractionBits) == 0U)
    {
        return std::bit_cast<double>(sign | integer);
    }
    const auto biased = static_cast<std::uint64_t>(quantum - DoubleQuantumExponent + 1);
    if (biased >= (DoubleInfinityBits >> DoubleFractionBits))
    {
        return std::bit_cast<double>(sign | DoubleInfinityBits);
    }
    const std::uint64_t fractionMask = (std::uint64_t{1} << DoubleFractionBits) - 1U;
    return std::bit_cast<double>(sign | (biased << DoubleFractionBits) | (integer & fractionMask));
}

// The exact retained64 × f32 product (up to 88 bits) and a single rounding to 53 bits.
Rounded53 RoundedProduct(const Retained64& value, float multiplier)
{
    const BinaryParts factor = DecomposeFloat(multiplier);
    constexpr std::uint64_t LowMask = 0xffffffffU;
    const std::uint64_t low = (value.significand & LowMask) * factor.significand;
    const std::uint64_t high = (value.significand >> LimbBits) * factor.significand;
    const std::uint64_t productLow = low + (high << LimbBits);
    const std::uint64_t productHigh = (high >> LimbBits) + (productLow < low ? 1U : 0U);
    const std::int64_t exponent = std::int64_t{value.exponent} + factor.exponent;
    const int length = productHigh != 0U ? 64 + static_cast<int>(std::bit_width(productHigh))
                                         : static_cast<int>(std::bit_width(productLow));
    if (length <= DoubleDigits)
    {
        return {productLow, exponent, value.negative};
    }
    const int drop = length - DoubleDigits;
    std::uint64_t mantissa = (productLow >> drop) | (productHigh << (64 - drop));
    const std::uint64_t remainder = productLow & ((std::uint64_t{1} << drop) - 1U);
    const std::uint64_t half = std::uint64_t{1} << (drop - 1);
    if (remainder > half || (remainder == half && (mantissa & 1U) != 0U))
    {
        ++mantissa;
    }
    if ((mantissa >> DoubleDigits) != 0U)
    {
        return {mantissa >> 1, exponent + drop + 1, value.negative};
    }
    return {mantissa, exponent + drop, value.negative};
}

// The double only if it represents the R53 without another rounding: no subnormal that loses bits, no overflow.
std::optional<double> ExactDouble(const Rounded53& value)
{
    if (value.mantissa != 0U)
    {
        const auto length = static_cast<int>(std::bit_width(value.mantissa));
        if (value.exponent + length - 1 > DoubleMaxExponent)
        {
            return std::nullopt;
        }
        const std::int64_t lost = DoubleQuantumExponent - value.exponent;
        if (lost >= length || (lost > 0 && (value.mantissa & ((std::uint64_t{1} << lost) - 1U)) != 0U))
        {
            return std::nullopt;
        }
    }
    return NearestDouble(value.mantissa, value.exponent, value.negative);
}

LeafSine EvaluateLeafArgument(double argument)
{
    if (!std::isfinite(argument))
    {
        return {{0, 0, RetainedClass::NaN, false}, false};
    }
    if (std::abs(argument) >= ArgumentLimit)
    {
        const BinaryParts parts = DecomposeDouble(argument);
        const int shift = std::countl_zero(parts.significand);
        return {{parts.significand << shift, parts.exponent - shift, RetainedClass::Finite, parts.negative}, true};
    }
    if (argument == 0.0)
    {
        return {{0, 0, RetainedClass::Finite, std::signbit(argument)}, false};
    }
    return {RetainedSineOrCosine(argument, false), false};
}
} // namespace

// Product of two binary32: 48 significand bits, exact in double. There is no rounding an FMA could contract.
LeafSine FirstLeafSine(float time)
{
    return EvaluateLeafArgument(static_cast<double>(time) * static_cast<double>(FirstFrequency));
}

// With the exact product, the sum is the only R53 rounding; an FMA would give the same value.
LeafSine SecondLeafSine(float time)
{
    const double product = static_cast<double>(time) * static_cast<double>(SecondFrequency);
    return EvaluateLeafArgument(product + static_cast<double>(SecondPhase));
}

SignedConversion TruncateLeafSine(const Retained64& value)
{
    constexpr SignedConversion Invalid{std::numeric_limits<std::int64_t>::min(), false};
    if (value.classification == RetainedClass::NaN || value.classification == RetainedClass::Infinity)
    {
        return Invalid;
    }
    if (value.significand == 0U || value.exponent <= -RetainedDigits)
    {
        return {0, true};
    }
    if (value.exponent < 0)
    {
        const auto magnitude = static_cast<std::int64_t>(value.significand >> -value.exponent);
        return {value.negative ? -magnitude : magnitude, true};
    }
    // |value| = significand · 2^exponent: fits if below 2^63, or equal with a negative sign.
    if (value.exponent > SignedMagnitudeBits)
    {
        return Invalid;
    }
    const std::uint64_t limit = std::uint64_t{1} << (SignedMagnitudeBits - value.exponent);
    if (value.significand < limit)
    {
        const auto magnitude = static_cast<std::int64_t>(value.significand << value.exponent);
        return {value.negative ? -magnitude : magnitude, true};
    }
    if (value.significand == limit && value.negative)
    {
        return {std::numeric_limits<std::int64_t>::min(), true};
    }
    return Invalid;
}

double AtanDegreesProductPC53(double quotient)
{
    if (std::isnan(quotient))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // atan → phi64 → exact product by K24 → R53 → double; the last step rounds again only if the result is subnormal.
    const Rounded53 product = RoundedProduct(RetainedArctangent(quotient), Degrees);
    return NearestDouble(product.mantissa, product.exponent, product.negative);
}
namespace
{
bool ValidParticleInputs(double argument, float multiplier)
{
    return std::isfinite(argument) && std::abs(argument) < ArgumentLimit && std::isfinite(multiplier) &&
           multiplier > 0.0f;
}

std::optional<double> TrigonometricProduct(double argument, float multiplier, bool cosine)
{
    if (!ValidParticleInputs(argument, multiplier))
    {
        return std::nullopt;
    }
    if (argument == 0.0)
    {
        constexpr Retained64 RetainedOne{std::uint64_t{1} << (RetainedDigits - 1), 1 - RetainedDigits,
                                         RetainedClass::Finite, false};
        const Retained64 identity =
            cosine ? RetainedOne : Retained64{0, 0, RetainedClass::Finite, std::signbit(argument)};
        return RetainedProductPC53(identity, multiplier);
    }
    return RetainedProductPC53(RetainedSineOrCosine(argument, cosine), multiplier);
}
} // namespace

std::optional<double> RetainedProductPC53(const Retained64& value, float multiplier)
{
    if (!std::isfinite(multiplier) || multiplier <= 0.0f)
    {
        return std::nullopt;
    }
    if (value.classification == RetainedClass::NaN)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (value.classification == RetainedClass::Infinity)
    {
        return value.negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    if (value.classification != RetainedClass::Finite)
    {
        // A class outside the enum loads as finite and converts without the factor.
        return NearestDouble(value.significand, value.exponent, value.negative);
    }
    // Outside this margin not even an f32 factor (2^-149 .. <2^128) can give an exact double.
    constexpr int ExponentMargin = 2 * std::numeric_limits<double>::max_exponent;
    if (value.significand != 0 && (value.exponent < -ExponentMargin || value.exponent > ExponentMargin))
    {
        return std::nullopt;
    }
    return ExactDouble(RoundedProduct(value, multiplier));
}

std::optional<double> SineProductPC53(double argument, float multiplier)
{
    return TrigonometricProduct(argument, multiplier, false);
}

std::optional<double> CosineProductPC53(double argument, float multiplier)
{
    return TrigonometricProduct(argument, multiplier, true);
}

namespace
{
// The R53 of a normalized Wide: the top 53 bits, to nearest with ties to even.
std::optional<double> Nearest53(const Wide& value, bool negative)
{
    const std::uint64_t high =
        (std::uint64_t{value.mantissa[LimbCount - 1]} << LimbBits) | value.mantissa[LimbCount - 2];
    constexpr int Drop = RetainedDigits - DoubleDigits;
    constexpr std::uint64_t Half = std::uint64_t{1} << (Drop - 1);
    std::uint64_t mantissa = high >> Drop;
    bool sticky = (high & (Half - 1)) != 0;
    for (std::size_t i = 0; i + 2 < LimbCount; ++i)
        sticky = sticky || value.mantissa[i] != 0;
    if ((high & Half) != 0 && (sticky || (mantissa & 1) != 0))
        ++mantissa;
    return ExactDouble({mantissa, value.exponent - DoubleDigits, negative});
}

std::optional<double> PairProduct(double sineArgument, bool firstCosine, double cosineArgument, bool secondCosine)
{
    if (!ValidParticleInputs(sineArgument, 1) || !ValidParticleInputs(cosineArgument, 1))
        return std::nullopt;
    constexpr Retained64 RetainedOne{std::uint64_t{1} << (RetainedDigits - 1), 1 - RetainedDigits,
                                     RetainedClass::Finite, false};
    const Retained64 sine =
        sineArgument == 0
            ? (firstCosine ? RetainedOne : Retained64{0, 0, RetainedClass::Finite, std::signbit(sineArgument)})
            : RetainedSineOrCosine(sineArgument, firstCosine);
    const Retained64 cosine =
        cosineArgument == 0
            ? (secondCosine ? RetainedOne : Retained64{0, 0, RetainedClass::Finite, std::signbit(cosineArgument)})
            : RetainedSineOrCosine(cosineArgument, secondCosine);
    const bool negative = sine.negative != cosine.negative;
    if (sine.significand == 0 || cosine.significand == 0)
        return NearestDouble(0, 0, negative);
    // 128 exact bits fit in Wide; no intermediate R64 of the product.
    const Wide product = Multiply(FromInteger(SplitLimbs(sine.significand), sine.exponent, sine.negative),
                                  FromInteger(SplitLimbs(cosine.significand), cosine.exponent, cosine.negative));
    return Nearest53(product, negative);
}
} // namespace
std::optional<double> SineCosineProductPC53(double sineArgument, double cosineArgument)
{
    return PairProduct(sineArgument, false, cosineArgument, true);
}

std::optional<double> SineSumPC53(double argument, double addend)
{
    if (!ValidParticleInputs(argument, 1) || !std::isfinite(addend))
        return std::nullopt;
    const Retained64 sine = argument == 0 ? Retained64{0, 0, RetainedClass::Finite, std::signbit(argument)}
                                          : RetainedSineOrCosine(argument, false);
    const BinaryParts parts = DecomposeDouble(addend);
    // With |sine| <= 1 and a double addend the exact sum fits in Wide: a single R53, like the FADD.
    const Wide sum = Add(FromInteger(SplitLimbs(sine.significand), sine.exponent, sine.negative),
                         FromInteger(SplitLimbs(parts.significand), parts.exponent, parts.negative));
    if (IsZero(sum))
        return sine.significand == 0 && parts.significand == 0 ? addend + (sine.negative ? -0.0 : 0.0) : 0.0;
    return Nearest53(sum, sum.negative);
}
namespace
{
std::optional<double> DoubleTrigonometricProduct(double argument, double multiplier, bool cosine)
{
    if (!ValidParticleInputs(argument, 1) || !std::isfinite(multiplier))
        return std::nullopt;
    const Retained64 value = argument == 0
                                 ? (cosine ? Retained64{std::uint64_t{1} << 63, -63, RetainedClass::Finite, false}
                                           : Retained64{0, 0, RetainedClass::Finite, std::signbit(argument)})
                                 : RetainedSineOrCosine(argument, cosine);
    const BinaryParts parts = DecomposeDouble(multiplier);
    const bool negative = value.negative != parts.negative;
    if (value.significand == 0 || parts.significand == 0)
        return NearestDouble(0, 0, negative);
    // 64 + 53 exact bits fit in Wide: the product with no intermediate R64, like the x87 FMUL under PC53.
    const Wide product = Multiply(FromInteger(SplitLimbs(value.significand), value.exponent, value.negative),
                                  FromInteger(SplitLimbs(parts.significand), parts.exponent, parts.negative));
    return Nearest53(product, negative);
}
} // namespace
std::optional<double> SineDoubleProductPC53(double argument, double multiplier)
{
    return DoubleTrigonometricProduct(argument, multiplier, false);
}
std::optional<double> CosineDoubleProductPC53(double argument, double multiplier)
{
    return DoubleTrigonometricProduct(argument, multiplier, true);
}

namespace
{
// FSTP DWORD of a finite retained64: a single rounding from 64 bits to the float's 24, to nearest with ties to even.
// A sine never gives float subnormals or overflow, so ldexp and the float conversion are exact.
float NearestFloat(const Retained64& value)
{
    if (value.significand == 0)
        return value.negative ? -0.0f : 0.0f;
    constexpr int FloatDigits = std::numeric_limits<float>::digits;
    const int drop = static_cast<int>(std::bit_width(value.significand)) - FloatDigits;
    const std::uint64_t mantissa = drop > 0 ? ShiftNearestEven(value.significand, drop) : value.significand;
    const int exponent = value.exponent + (drop > 0 ? drop : 0);
    const auto magnitude = static_cast<float>(std::ldexp(static_cast<double>(mantissa), exponent));
    return value.negative ? -magnitude : magnitude;
}

// 0x53642d..0x53642f (and 0x53d260): FSIN and FSTP DWORD, the 64-bit sine straight to float, never through 53 bits.
std::optional<float> SineSpill(double argument)
{
    if (!ValidParticleInputs(argument, 1))
        return std::nullopt;
    if (argument == 0)
        return std::signbit(argument) ? -0.0f : 0.0f;
    return NearestFloat(RetainedSineOrCosine(argument, false));
}

// 0x53d1f9..0x53d231: the x frequency (0.048 · scale) stays on the x87 unstored (the product of two floats fits in 53
// bits); the y and z ones are stored as floats.
std::optional<std::array<float, 4>> DirectionPC53(std::int32_t phase, float rate)
{
    const auto wrapped = [](std::int32_t value, std::uint32_t add)
    { return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) + add); };
    const double x = (static_cast<double>(.048f) * static_cast<double>(rate)) * wrapped(phase, 55555U);
    const double y = static_cast<double>(.0613f * rate) * phase;
    const double z = static_cast<double>(.1113f * rate) * wrapped(phase, 11111U);
    const auto a = PairProduct(x, false, y, true);
    const auto b = PairProduct(x, false, y, false);
    const auto c = PairProduct(x, true, z, true);
    const auto sine = SineSpill(z);
    if (!a || !b || !c || !sine)
        return std::nullopt;
    const float spillB = static_cast<float>(*b), spillSine = *sine;
    const auto signedCos = [](double argument, float factor) -> std::optional<double>
    {
        if (factor == 0)
            return static_cast<double>(factor);
        const auto value = CosineProductPC53(argument, std::abs(factor));
        if (!value)
            return std::nullopt;
        return factor < 0 ? -*value : *value;
    };
    const auto first = signedCos(z, spillB), second = signedCos(x, spillSine);
    if (!first || !second)
        return std::nullopt;
    return std::array<float, 4>{static_cast<float>(*first - *second),
                                static_cast<float>(*c + static_cast<double>(spillSine) * spillB),
                                static_cast<float>(*a), spillSine};
}
} // namespace
std::optional<std::array<float, 3>> FaunaDirectionPC53(std::int32_t phase)
{
    const auto direction = DirectionPC53(phase, 1);
    if (!direction)
        return std::nullopt;
    return std::array<float, 3>{(*direction)[0], (*direction)[1], (*direction)[2]};
}
std::optional<std::array<float, 4>> JointDirectionPC53(std::int32_t phase)
{
    return DirectionPC53(phase, 0.5f);
}
std::optional<std::array<float, 3>> ScaledFaunaDirectionPC53(std::int32_t phase, float scale)
{
    const auto direction = DirectionPC53(phase, scale);
    if (!direction)
        return std::nullopt;
    return std::array<float, 3>{(*direction)[0], (*direction)[1], (*direction)[2]};
}
std::optional<float> SineSpillF32(double argument)
{
    return SineSpill(argument);
}
std::optional<float> CosineSpillF32(double argument)
{
    if (!ValidParticleInputs(argument, 1))
        return std::nullopt;
    if (argument == 0)
        return 1.0f;
    return NearestFloat(RetainedSineOrCosine(argument, true));
}

} // namespace Client::Math::Detail
