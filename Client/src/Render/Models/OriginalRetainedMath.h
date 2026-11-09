#pragma once

#include <cstdint>

// Detalle privado de las hojas aceptadas en ADR0014. Valor matemático, sin layout ni estado
// de CPU: (-1)^negative · significand · 2^exponent. No hay conversión implícita a double.
namespace Client::Render::Models::Detail
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
    bool valid; // INT64_MIN válido y el patrón inválido enmascarado tienen los mismos bits.
};

// Preparan sólo el valor retenido. La conversión queda pendiente hasta el store del caller.
[[nodiscard]] LeafSine FirstLeafSine(float time);
[[nodiscard]] LeafSine SecondLeafSine(float time);
[[nodiscard]] SignedConversion TruncateLeafSine(const Retained64& value);

// Cociente ya R53; atan113 → phi64 → producto R53 directo por K24, sin phi → double.
[[nodiscard]] double AtanDegreesProductPC53(double quotient);
} // namespace Client::Render::Models::Detail
