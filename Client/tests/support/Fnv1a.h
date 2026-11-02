// El FNV-1a de 64 bits, un hash chico para comparar muchos bytes con un valor guardado: el de los
// píxeles que da la 6b de cada OZJ del laboratorio (Client/tests/vectors/jpeg/lab-6b.txt). No es
// criptográfico, y no hace falta: lo que se busca es ver si un decodificador da otros bytes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Tests
{

// La base y el primo de 64 bits, de la página de los autores (Fowler, Noll y Vo):
// http://www.isthe.com/chongo/tech/comp/fnv/
inline constexpr std::uint64_t Fnv1aOffsetBasis = 0xcbf29ce484222325;
inline constexpr std::uint64_t Fnv1aPrime = 0x100000001b3;

// El FNV-1a de `bytes`: por cada byte, primero el xor y después el producto, módulo 2^64 (un
// uint64_t da la vuelta sin comportamiento indefinido).
constexpr std::uint64_t Fnv1a64(std::span<const std::byte> bytes)
{
    std::uint64_t hash = Fnv1aOffsetBasis;
    for (const std::byte value : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint64_t>(value)) * Fnv1aPrime;
    }
    return hash;
}

} // namespace Client::Tests
