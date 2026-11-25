#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Data\Local\movereq.bmd, los destinos de la ventana /move (Client/docs/research/05-formatos-de-datos.md,
// "movereq.bmd"; en el original, 0x448440 desde 0x449830, que la carga de los datos del juego, 0x61ccb0, llama en
// 0x61ec8f con "Data\Local\movereq.bmd").
//
// 0x448440 lee la cuenta (un int en claro) y, por cada uno, un registro de 0x50 bytes con fread en un bloque de
// operator new, le pasa el XOR3 desde su primer byte (0x449770, FC CF AB con la clave en la pila) y lo agrega al final
// de la lista de 0x723c68 (+8, la cuenta en +0xc). No tiene suma ni mira cuánto leyó. *Verificado* con el del
// laboratorio: 28 destinos, y después de ellos 640 bytes (ocho registros en cero) que no lee.
namespace Client::Data::Text
{
inline constexpr std::size_t MoveCountSize = 4;
inline constexpr std::size_t MoveRecordSize = 0x50;
inline constexpr std::size_t MoveNameSize = 0x20;

struct MoveRequirement
{
    // +0: el número del destino.
    std::int32_t index = 0;
    // +4: el nombre que dibuja la ventana (0x623690) y que dice con el texto 260 (0x4487d7). Hasta su primer '\0',
    // que puede estar en el de +0x24: el bloque sigue.
    std::string name;
    // +0x24, tal cual: el que compara 0x4497c0 (con strcmp, para devolver el zen), que nadie llama.
    std::array<char, MoveNameSize> searchName{};
    // +0x44: el nivel mínimo (con signo: 0x448580 y 0x448fa0 lo comparan con JL y JG).
    std::int32_t level = 0;
    // +0x48: el zen (sin signo en la comparación; con signo en el _itoa del dibujo).
    std::uint32_t zen = 0;
    // +0x4c: la puerta de Gate.bmd, que el cliente no lee (el servidor mueve con /move y el nombre).
    std::int32_t gate = 0;

    friend bool operator==(const MoveRequirement&, const MoveRequirement&) = default;
};

// Los destinos de `file` en su orden. La cuenta sale de los primeros cuatro bytes (los que haya: fread deja en 0 los
// que faltan del int que 0x448440 puso en 0); con 0 o menos, ninguno. FileTooShort si falta algún registro de la
// cuenta (el original los completaría con lo que tuviera el bloque nuevo); InvalidData si un nombre no termina antes
// del final de su registro (strlen seguiría fuera del bloque). Lo que sobra se ignora.
[[nodiscard]] Core::Expected<std::vector<MoveRequirement>, DataError>
LoadMoveRequirements(std::span<const std::byte> file);
} // namespace Client::Data::Text
