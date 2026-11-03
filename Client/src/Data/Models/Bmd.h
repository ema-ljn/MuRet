#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Models/Model.h"

#include <cstddef>
#include <cstdint>
#include <span>

// El .bmd de un modelo, como lo lee 0x4f13d0 (Client/docs/research/05-formatos-de-datos.md, "Modelos
// .bmd"). Las tablas .bmd (Item, Skill, Gate) son otro formato con la misma extensión: no empiezan
// con "BMD".
//
//   offset   qué es
//   0        "BMD", que el original no mira (Q-C86)
//   3        la versión
//   4        con 0x0C, el tamaño del bloque cifrado (u32) y el bloque, cifrado con K16 desde +8;
//            con cualquier otra, el contenido en claro
//
// El contenido, sin relleno entre una parte y otra y todo little-endian:
//
//   nombre[32], mallas, huesos y acciones (int16 cada uno)
//   por malla:  vértices, normales, UV, triángulos y textura (int16 cada uno), los vértices (16 B),
//               las normales (20 B), las UV (8 B), los triángulos (64 B) y el nombre de la
//               textura[32]
//   por acción: las keys (int16), lock_positions (u8) y, con lock, una posición (12 B) por key
//   por hueso:  dummy (u8); si es 0, nombre[32], el padre (int16) y por cada acción sus keys
//               posiciones (12 B) y después sus keys rotaciones (12 B)
namespace Client::Data::Models
{
// El byte de la versión y la que va cifrada (0x4f13d0).
inline constexpr std::size_t BmdVersionOffset = 3;
inline constexpr std::uint8_t EncryptedBmdVersion = 0x0C;
// En claro, el contenido empieza después de la versión. Cifrado, después del tamaño.
inline constexpr std::size_t BmdPlainOffset = 4;
inline constexpr std::size_t BmdEncryptedSizeOffset = 4;
inline constexpr std::size_t BmdEncryptedOffset = BmdEncryptedSizeOffset + sizeof(std::uint32_t);

// Los largos de cada parte, los que recorre el script del research en los 1.962 modelos sin bytes de
// sobra (05).
inline constexpr std::size_t BmdNameSize = 32;
inline constexpr std::size_t BmdVertexSize = 16;   // hueso, relleno y la posición
inline constexpr std::size_t BmdNormalSize = 20;   // hueso, relleno, la normal, el vértice y relleno
inline constexpr std::size_t BmdTexCoordSize = 8;  // u y v
inline constexpr std::size_t BmdTriangleSize = 64; // de los que 0x4f13d0 copia 36
inline constexpr std::size_t BmdVectorSize = 3 * sizeof(float);
static_assert(BmdVertexSize == 2 * sizeof(std::int16_t) + BmdVectorSize, "el vértice de 05");
static_assert(BmdNormalSize == 4 * sizeof(std::int16_t) + BmdVectorSize, "la normal de 05");

// Dónde está cada cosa dentro de un triángulo: el tipo, y los juegos de cuatro índices de vértices,
// normales y UV (05).
inline constexpr std::size_t BmdTriangleVerticesOffset = 2;
inline constexpr std::size_t BmdTriangleNormalsOffset = 10;
inline constexpr std::size_t BmdTriangleTexCoordsOffset = 18;

// Bytes → Model, como 0x4f13d0. A propósito, también como él:
//   - no mira el "BMD" (Q-C86) y lee en claro cualquier versión que no sea 0x0C;
//   - con 0x0C, descifra con K16 el bloque de +8, desde su primer byte;
//   - lo que sobra después del último hueso no se lee (Model::trailingBytes).
// Distinto del original, que no mira los tamaños y sigue con la memoria que haya después del buffer:
//   - si una parte no entra en lo que queda, o el tamaño cifrado de +4 pasa del archivo,
//     DataError::FileTooShort;
//   - si una cantidad (de mallas, huesos, acciones, de cada parte de una malla o de keys) es
//     negativa, DataError::InvalidData: el original la usaría para pedir memoria.
[[nodiscard]] Core::Expected<Model, DataError> LoadBmd(std::span<const std::byte> file);
} // namespace Client::Data::Models
