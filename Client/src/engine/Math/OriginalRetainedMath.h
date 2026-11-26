#pragma once

#include "EngineApi.h"

#include <array>
#include <cstdint>
#include <optional>

// Private detail of the leaves accepted in ADR0014. Mathematical value, no layout or CPU state:
// (-1)^negative · significand · 2^exponent. No implicit conversion to double.
namespace Client::Math::Detail
{
enum class RetainedClass : std::uint8_t
{
    Finite,
    Infinity,
    NaN
};
struct Retained64
{
    std::uint64_t significand;
    std::int32_t exponent;
    RetainedClass classification;
    bool negative;
};
struct LeafSine
{
    Retained64 value;
    bool argumentRetained;
};
struct SignedConversion
{
    std::int64_t integer;
    bool valid; // A valid INT64_MIN and the masked invalid pattern have the same bits.
};

// They prepare only the retained value. The conversion waits for the caller's store.
[[nodiscard]] ENGINE_API LeafSine FirstLeafSine(float time);
[[nodiscard]] ENGINE_API LeafSine SecondLeafSine(float time);
[[nodiscard]] ENGINE_API SignedConversion TruncateLeafSine(const Retained64& value);

// Quotient already R53; atan113 → phi64 → direct R53 product by K24, no phi → double.
[[nodiscard]] ENGINE_API double AtanDegreesProductPC53(double quotient);
// Limited math profile: sin/cos113 → retained64 → exact product → R53.
// Finite argument with |a| < 2^63; finite positive f32 factor. Does not attest FSIN/FCOS.
[[nodiscard]] ENGINE_API std::optional<double> SineProductPC53(double argument, float multiplier);
[[nodiscard]] ENGINE_API std::optional<double> CosineProductPC53(double argument, float multiplier);
// 445e33: sin/cos113 → two retained64 → exact product → a single R53.
// Same finite domain; avoids rounding each transcendental to double before the product.
[[nodiscard]] ENGINE_API std::optional<double> SineCosineProductPC53(double sineArgument, double cosineArgument);
// 53d1a0 with scale 1: three PC53 directions and F32 spills, no access to the counter.
[[nodiscard]] ENGINE_API std::optional<std::array<float, 3>> FaunaDirectionPC53(std::int32_t phase);
// 5363d9: the same math with halved frequencies; the fourth value is the sinC spill.
[[nodiscard]] ENGINE_API std::optional<std::array<float, 4>> JointDirectionPC53(std::int32_t phase);
// 53d1a0 with another scale (the 0x10f/2, 0x535e66, with 1.4f): the x frequency not stored as float.
[[nodiscard]] ENGINE_API std::optional<std::array<float, 3>> ScaledFaunaDirectionPC53(std::int32_t phase, float scale);
// FSIN then FADD of a double addend (the constant's float): sin64 + addend with a single R53, without rounding the
// sine to double before the sum. Same domain as SineProductPC53.
[[nodiscard]] ENGINE_API std::optional<double> SineSumPC53(double argument, double addend);
// FSIN then FMUL ST1 by a factor the x87 holds as a double (not a float from memory): sin64 times the exact factor
// with a single R53. Same argument domain as SineProductPC53; the factor is finite and of any sign.
[[nodiscard]] ENGINE_API std::optional<double> SineDoubleProductPC53(double argument, double multiplier);
// FCOS by a retained double factor, with the same contract as SineDoubleProductPC53.
[[nodiscard]] ENGINE_API std::optional<double> CosineDoubleProductPC53(double argument, double multiplier);
// FSIN or FCOS and FSTP DWORD (0x52e3a0 and 0x52e390 followed by a float store): the 64-bit value straight to float,
// never through 53 bits. Same domain as SineProductPC53.
[[nodiscard]] ENGINE_API std::optional<float> SineSpillF32(double argument);
[[nodiscard]] ENGINE_API std::optional<float> CosineSpillF32(double argument);
// Canonical retained value; keeps classes and the sign of zero. Error if it does not fit exactly in a double.
[[nodiscard]] ENGINE_API std::optional<double> RetainedProductPC53(const Retained64& value, float multiplier);
} // namespace Client::Math::Detail
