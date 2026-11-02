// Misión de la CL2.0 (docs/learning/ola-cl2.0-flujos-y-bloques.md, actividad 8): el inverso modular,
// con Euclides extendido.
//
// Las claves de SimpleModulus vienen de a pares: la de cifrado del cliente es el inverso de la de
// descifrado del servidor, módulo el mismo número (Client/docs/research/03-red-y-cifrado.md,
// "C3/C4"). Multiplicar por una y después por la otra deja el carril como estaba.

#pragma once

#include <cstdint>
#include <optional>

namespace Labs
{

// El inverso de a módulo m: el x de 0 a m - 1 con (a * x) mod m = 1. a puede ser mayor que m (se
// toma su resto). Si a y m tienen un divisor común mayor que 1 no hay inverso, y tampoco con m
// menor que 2: módulo 1 todo es 0, y módulo 0 no hay aritmética. En esos casos, std::nullopt.
[[nodiscard]] std::optional<std::uint32_t> ModularInverse(std::uint32_t a, std::uint32_t m);

} // namespace Labs
