#pragma once

#include "Core/Expected.h"
#include "Protocol/Character/Opcodes.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Core
{
class Log;
}

namespace Client::Protocol::Character
{
struct CharacterSelectResult;
}

// El registro del personaje propio, [0x7681eb0]: lo que la respuesta del F3 03 escribe en él y el
// vaciado de su equipo. Es una proyección tipada, no el layout de 0xf08 bytes; cada campo dice su
// offset. Contrato: Client/docs/research/04-protocolo.md, "La entrada al mundo" (la tabla de la
// respuesta y Q-C144), y 06-render-escenas-y-ui.md, "La lógica" de la selección (la identidad) y
// "La entrada al mundo" (las filas 5 y 7 de 0x4c3940).
//
// Arranca en cero, como el bloque que WinMain llena de ceros (04, 0x4bafbc..0x4bafc2). El original
// lo guarda cifrado en la tabla de protección (06, "La tabla de protección"): abrirlo y cerrarlo,
// con sus rand(), es de quien llama, no de este registro.
namespace Client::Scenes
{
// 06, fila 7 de 0x4c3940: doce lugares desde +0xb68, de a 0x48, y el tipo en −1 al vaciarlos.
inline constexpr std::size_t OwnEquipmentSlotCount = 12;
inline constexpr std::uint16_t EmptyOwnItemId = 0xffff;

struct OwnCharacterRecord
{
    // Los stats de la respuesta del F3 03 (0x4c3940), según la tabla de 04; el PK y el CtlCode van
    // al héroe, no acá. No toca la identidad ni el equipo: 0x512a30 viste al héroe después con el
    // equipo todavía viejo (06, fila 5).
    // Q-C144: el original copia a +0x48 y +0x4a los WORD de pkt[0x34..0x37], fuera de los 0x34
    // bytes del paquete; MuRet los deja en 0 y lo avisa en `log`.
    void ApplyStats(const Protocol::Character::CharacterSelectResult& reply, Core::Log& log);

    // 0x4c3940, fila 7 (0x4c47c8..0x4c47da): en los doce lugares, el tipo en −1, el dword de +4
    // (scalar1Low) y el byte de +0x1b (flags1b) en 0, porque el equipo lo trae el F3 10. El resto de
    // cada ItemState queda como estaba, como en el original.
    void ClearEquipmentForEntry();

    // Frontera explícita del F3 10 (0x4c55a0). 04 ("El F3 10") dice que hace una inicialización
    // común, también con cantidad 0, y 06 ("La tabla de protección") que cierra este registro una
    // vez, pero no qué escribe en su equipo: sigue pendiente en L3/P. Devuelve siempre el error y
    // no toca el registro, para que nadie lo tome por un vaciado hecho.
    [[nodiscard]] Core::Expected<void, std::string> ClearEquipmentForInventory();

    // 06, "La lógica": lo copia la selección (0x6342c0) y la respuesta no lo cambia.
    std::array<std::byte, Protocol::Character::NameSize + 1> name{}; // +0, hasta la clase
    std::uint8_t characterClass = 0;                                 // +0xb
    std::uint8_t skin = 0;                                           // +0xc
    std::uint16_t level = 0;                                         // +0xe

    // 04, la tabla de la respuesta del F3 03: adónde va cada campo.
    std::uint32_t experience = 0;     // +0x10
    std::uint32_t nextExperience = 0; // +0x14
    std::uint16_t strength = 0;       // +0x18
    std::uint16_t agility = 0;        // +0x1a
    std::uint16_t vitality = 0;       // +0x1c
    std::uint16_t energy = 0;         // +0x1e
    std::uint16_t leadership = 0;     // +0x20, de pkt[0x32], el relleno del struct (Q-C144)
    std::uint16_t life = 0;           // +0x22
    std::uint16_t mana = 0;           // +0x24
    std::uint16_t maxLife = 0;        // +0x26
    std::uint16_t maxMana = 0;        // +0x28
    // +0x2a a +0x36: 0x550d10 los pone en 0 (0x54ef00) y les suma lo de los ítems (06, "Los stats del
    // registro propio"); qué son no está acreditado.
    std::uint16_t word2a = 0;
    std::uint16_t word2c = 0;
    std::uint16_t word2e = 0;
    std::uint16_t word30 = 0;
    std::uint16_t word32 = 0;
    std::uint16_t word34 = 0;
    std::uint16_t word36 = 0;
    std::uint16_t bp = 0;    // +0x38
    std::uint16_t maxBp = 0; // +0x3a
    // +0x3c: los bits del ajuste que 0x4bee60 aplica a las velocidades del keepalive (04, el 0E).
    // 0x54fe10 suma 20 a las velocidades con el 0x01 o el 0x08, y prende o apaga el 0x04.
    std::uint8_t speedAdjustBits = 0;
    // +0x3e, +0x40 y +0x42: la respuesta los pone en 0; qué son no está acreditado.
    std::uint16_t word3e = 0;
    std::uint16_t word40 = 0;
    std::uint16_t word42 = 0;
    std::uint16_t addedPoints = 0;    // +0x44
    std::uint16_t maxAddedPoints = 0; // +0x46
    // +0x48 y +0x4a: los de Q-C144; qué son no está acreditado.
    std::uint16_t word48 = 0;
    std::uint16_t word4a = 0;
    // +0x4c a +0x66: los escriben los ocho helpers de 0x550d10 (06, "Los ocho helpers de los stats
    // propios"). +0x4c y +0x58 son las velocidades que manda el keepalive (04, el 0E); qué son los otros
    // no está acreditado. La cola de 0x550d10 lee +0x4e, +0x60 y +0x62.
    std::uint16_t attackSpeed = 0; // +0x4c
    std::uint16_t word4e = 0;
    std::uint16_t word50 = 0;
    std::uint16_t word52 = 0;
    std::uint16_t word54 = 0;
    std::uint16_t word56 = 0;
    std::uint16_t magicSpeed = 0; // +0x58
    std::uint16_t word5a = 0;
    std::uint16_t word5c = 0;
    std::uint16_t word60 = 0;
    std::uint16_t word62 = 0;
    std::uint16_t word64 = 0;
    std::uint16_t word66 = 0;
    std::uint16_t levelUpPoints = 0; // +0x68

    std::array<World::ItemState, OwnEquipmentSlotCount> equipment{}; // +0xb68

    // [0x7681eb4] + 0xecc: desde el login, el mismo bloque que [0x7681eb0] (0x4bafc9).
    std::uint32_t zen = 0;

    // +0xee2 a +0xf03, la cola de 0x550d10 (0x5512bf..0x5513b1): lee los primeros siete y escribe los
    // otros. Los ocho helpers de antes no escriben esos siete; quién, sin leer (08, P-C38). Qué son no
    // está acreditado.
    std::uint16_t wordee2 = 0;
    std::uint16_t wordee4 = 0;
    std::uint16_t wordee6 = 0;
    std::uint16_t wordeea = 0;
    std::uint16_t wordeec = 0;
    std::uint16_t wordef0 = 0;
    std::uint16_t wordef2 = 0;
    // +0xef4: 0x54f9b0 le suma 0x40d690 sin ponerlo antes en 0, lo suma a +0x5a y +0x5c, y lo deja con
    // lo de 0x40d7f0.
    std::uint16_t wordef4 = 0;
    std::uint16_t wordef8 = 0;
    std::uint16_t wordefa = 0;
    std::uint16_t wordefc = 0;
    std::uint16_t wordefe = 0;
    std::uint16_t wordf00 = 0;
    std::uint8_t bytef02 = 0;
    std::uint8_t bytef03 = 0;
};
} // namespace Client::Scenes
