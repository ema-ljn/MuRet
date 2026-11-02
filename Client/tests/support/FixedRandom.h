#pragma once

#include <cstdint>
#include <random>

// Números al azar que dan lo mismo en cualquier compilador, para los tests con semilla fija.
//
// El estándar fija los números de std::mt19937 ([rand.predef]: el 10.000 desde la semilla por
// defecto es 4123659995), pero no los de std::uniform_int_distribution, que cambian entre
// bibliotecas: con libstdc++ y con libc++ un test vería otros bytes.
namespace Client::Tests
{
// La semilla de los tests: la fecha en que se escribieron los primeros.
inline constexpr std::mt19937::result_type FixedSeed = 20261102;

// Un número de 0 a n - 1 tomado de la salida del motor (engine() % n). La salida de std::mt19937
// tiene 32 bits, así que entra en un std::uint32_t. El resto no es parejo del todo, y a un test le
// alcanza.
inline std::uint32_t Below(std::mt19937& engine, std::uint32_t n)
{
    return static_cast<std::uint32_t>(engine() % n);
}
} // namespace Client::Tests
