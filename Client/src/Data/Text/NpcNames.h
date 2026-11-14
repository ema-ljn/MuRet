#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Data\Local\NpcName(%s).txt, los nombres de los monstruos y los NPC (en el original, 0x54e7f0 con el tokenizador
// 0x54e8d0, que la carga de los datos del juego, 0x61ccb0, llama en 0x61ed05 con "Data\Local\NpcName(%s).txt" y el
// idioma de [0x5763350]). Los deja en 0x7681eb8: 512 registros de 0x36 bytes, el tipo (un word) y el nombre.
//
// El tokenizador lee de a un byte con getc (0x6a0641) y saltea los blancos (isspace) y los comentarios "//" hasta
// el '\n'. Según el primer byte: '"' es una cadena hasta la otra comilla (sin escapes), en 0x7681db0; un dígito, '.'
// o '-' es un número (los dígitos, '.' y '-' que siguen, con atof a un float en 0x76baac4; el byte que lo termina se
// pierde); una letra es un nombre (letras, dígitos, '.' y '_', y el que lo termina vuelve con ungetc), también en
// 0x7681db0; '#', ',', ';', '{' y '}' son ellos mismos, y cualquier otro, el 0x3c. El fin del archivo, y el byte 0xff
// (getc lo compara en un char), es el 2.
//
// 0x54e7f0: mientras el token no sea el 2 ni el nombre "end", un registro más con el tipo en el ftol del último número
// leído (sea cual sea el token), y dos tokens más: el nombre es la cadena que dejó el segundo (strcpy; "" si fue otra
// cosa). Las comillas, el número y el nombre del archivo del laboratorio: " 84 1 \"King Orc\"".
namespace Client::Data::Text
{
// Los registros de 0x7681eb8..0x7688ab8.
inline constexpr std::size_t NpcNameCount = 512;
// 0x36 bytes menos el word del tipo.
inline constexpr std::size_t NpcNameBytes = 0x34;
// 0x7681db0..0x7681eb8: la cadena del token, que sigue en la tabla.
inline constexpr std::size_t NpcNameTokenBytes = 0x108;
// [ebp − 0x64]: el número del token, en la pila de 0x54e8d0.
inline constexpr std::size_t NpcNameNumberBytes = 0x64;

struct NpcNameRecord
{
    // +0: el tipo del monstruo o del NPC.
    std::uint16_t type = 0;
    // +2: el nombre con su '\0' y, después, lo que había (0: la tabla está en la .bss).
    std::array<char, NpcNameBytes> name{};

    friend bool operator==(const NpcNameRecord&, const NpcNameRecord&) = default;
};

// Los registros que escribe 0x54e7f0 con `file`, en su orden. Un nombre que no entra en sus 0x34 bytes con su '\0'
// sigue, como el strcpy del original, en el tipo y el nombre de los registros que siguen, que el registro siguiente
// pisa con lo suyo; los que solo toca ese nombre después del último leído también se devuelven. InvalidData donde el
// original escribiría fuera de la tabla o no terminaría: un nombre que pasaría de 0x7688ab8, más de 512 registros
// (después viene 0x7688ab8), una cadena o un nombre de 0x108 bytes o más, un número de 0x64 o más, un
// comentario que llega al final del archivo sin '\n' (0x54e925 no sale nunca), un byte de 0x80 a 0xfe donde el
// tokenizador le pregunta a isspace, isdigit o isalnum (con el char con signo, fuera de la tabla de la CRT) y un ftol
// fuera de int64. El último número empieza en 0 (la .bss; *inferido*: en la versión de release ningún otro cargador
// del tokenizador corre antes). Los bytes de 0 a 0x7f se clasifican como en la "C".
[[nodiscard]] Core::Expected<std::vector<NpcNameRecord>, DataError> LoadNpcNames(std::span<const std::byte> file);
} // namespace Client::Data::Text
