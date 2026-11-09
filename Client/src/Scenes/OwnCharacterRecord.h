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

    // 0x4c3940, fila 7: el tipo de los doce lugares en −1, porque el equipo lo trae el F3 10.
    // Frontera: 06 da solo el tipo. Si el original escribe algo más en cada lugar no está leído, y
    // MuRet no lo inventa: el resto de cada ItemState queda como estaba.
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
    std::uint16_t bp = 0;             // +0x38
    std::uint16_t maxBp = 0;          // +0x3a
    // +0x3c: los bits del ajuste que 0x4bee60 aplica a las velocidades del keepalive (04, el 0E).
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
    std::uint16_t levelUpPoints = 0; // +0x68

    std::array<World::ItemState, OwnEquipmentSlotCount> equipment{}; // +0xb68

    // [0x7681eb4] + 0xecc: desde el login, el mismo bloque que [0x7681eb0] (0x4bafc9).
    std::uint32_t zen = 0;
};
} // namespace Client::Scenes
