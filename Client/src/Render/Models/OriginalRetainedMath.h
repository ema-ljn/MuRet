#pragma once

#include <array>
#include <cstdint>
#include <optional>

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
// Perfil matemático limitado: sin/cos113 → retenido64 → producto exacto → R53.
// Argumento finito con |a| < 2^63; factor f32 finito positivo. No acredita FSIN/FCOS.
[[nodiscard]] std::optional<double> SineProductPC53(double argument, float multiplier);
[[nodiscard]] std::optional<double> CosineProductPC53(double argument, float multiplier);
// 445e33: sin/cos113 → dos retenidos64 → producto exacto → un único R53.
// Mismo dominio finito; evita redondear cada trascendente a double antes del producto.
[[nodiscard]] std::optional<double> SineCosineProductPC53(double sineArgument, double cosineArgument);
// 53d1a0 con escala1: tres direcciones PC53 y spills F32, sin acceso al contador.
[[nodiscard]] std::optional<std::array<float, 3>> FaunaDirectionPC53(std::int32_t phase);
// 5363d9: las mismas cuentas con frecuencias a la mitad; el cuarto valor es el spill sinC.
[[nodiscard]] std::optional<std::array<float, 4>> JointDirectionPC53(std::int32_t phase);
// FSIN y después FADD de un sumando double (el float de la constante): sin64 + sumando con un solo R53,
// sin redondear el seno a double antes de la suma. Mismo dominio que SineProductPC53.
[[nodiscard]] std::optional<double> SineSumPC53(double argument, double addend);
// Valor canónico retenido; conserva clases y signo de cero. Error si no cabe exactamente en double.
[[nodiscard]] std::optional<double> RetainedProductPC53(const Retained64& value, float multiplier);
} // namespace Client::Render::Models::Detail
