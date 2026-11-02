#include "ModularInverse.h"

#include <cstdint>

namespace Labs
{

namespace
{

// El módulo más chico con aritmética que tenga sentido: módulo 1 todo es 0.
constexpr std::uint32_t MinModulus = 2;

} // namespace

// Euclides extendido. Lleva dos restos (r) y, para cada uno, el coeficiente de a (t) con
// r ≡ t × a (mod m). Empieza con m ≡ 0 × a y a ≡ 1 × a; cada paso resta q veces el segundo al
// primero, así que la relación se mantiene. Cuando el resto llega al mcd: si es 1, su t es el
// inverso.
//
// Los t van alternando de signo y en valor absoluto no pasan de m, así que entran en un
// std::int64_t con m de hasta 32 bits. Los restos no son negativos y no pasan de m: entran en un
// std::uint32_t, pero se llevan en std::int64_t para no mezclar signos en q × r.
std::optional<std::uint32_t> ModularInverse(std::uint32_t a, std::uint32_t m)
{
    if (m < MinModulus)
    {
        return std::nullopt;
    }
    std::int64_t previousRest = m;
    std::int64_t rest = a % m;
    std::int64_t previousCoefficient = 0;
    std::int64_t coefficient = 1;
    while (rest != 0)
    {
        const std::int64_t quotient = previousRest / rest;
        const std::int64_t nextRest = previousRest - quotient * rest;
        const std::int64_t nextCoefficient = previousCoefficient - quotient * coefficient;
        previousRest = rest;
        rest = nextRest;
        previousCoefficient = coefficient;
        coefficient = nextCoefficient;
    }
    if (previousRest != 1)
    {
        return std::nullopt;
    }
    // previousCoefficient está entre -m y m: sumarle m si es negativo lo deja en 0..m-1.
    if (previousCoefficient < 0)
    {
        previousCoefficient += m;
    }
    return static_cast<std::uint32_t>(previousCoefficient);
}

} // namespace Labs
