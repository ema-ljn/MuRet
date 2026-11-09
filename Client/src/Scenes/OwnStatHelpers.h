#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "World/ItemState.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los ocho helpers que 0x550d10 llama en 0x551287..0x5512ba, en ese orden, todos thiscall sobre el
// registro propio, antes de su cola. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los ocho
// helpers de los stats propios", y 08, P-C38.
//
// Cada uno es una función, con su dirección. Escriben en el registro los WORD +0x4c a +0x66 y +0xef4
// (no los +0xee2..+0xef2 de la cola). Donde el original lee algo que MuRet no tiene, devuelven un error
// en ese punto, con lo que escribieron antes, como el original hasta ahí: Item.bmd sin cargar o un tipo
// fuera de la tabla, la lista de [0x6eed3c] sin armar (0x40e1b0) o un ítem con más de nueve opciones.
// Suponen el registro abierto, como desde 0x512a30.
namespace Client::Scenes
{
struct ItemSetOptionList;
struct OwnCharacterRecord;

// El ítem de 0x7879f80 y lo que decide si 0x54efe0, 0x54f9b0 y 0x54fe10 lo leen en lugar de un lugar
// del equipo: con [0x7879fbc] > 0, con signo (jle), y [0x78ab9b8] == 0x78a9bb0, el lugar [0x78a6758],
// sin signo (ja con 11), si está en la lista de ese helper. Qué son esos globales no está acreditado.
// Por defecto no reemplaza nada.
struct OwnEquipmentOverride
{
    std::int32_t dword7879fbc = 0;
    bool window78ab9b8Is78a9bb0 = false;
    std::uint32_t slot78a6758 = 0;
    World::ItemState item7879f80{};
};

// Lo que deja 0x505820 (06, "El equipo propio en el personaje", paso 9; sin portar) y lee 0x550990.
struct Scan505820
{
    std::int32_t dword743a178 = 0;
    std::uint8_t byte743a17c = 0;
};

// 0x54ee00 (cdecl, devuelve un float): con max = byte +0x2d de Item.bmd, más 1 por cada uno de los
// primeros 4 niveles ((scalar1 >> 3) & 15) y 2 por cada uno de los otros, más 20 con flags1c % 4 en 1 o
// 2 o, si no, 15 con flags1b & 0x3f, y r = 1 − durabilidad / max: 0.5 si r > 0.8f, 0.3f si r > 0.7f,
// 0.2f si r > 0.5f y 0 si no. La división y la resta, en doble (el perfil PC53 de ADR 0012). Con max
// en 0 la x87, con las excepciones enmascaradas, da infinito o NaN, y ninguna comparación pasa: 0.
[[nodiscard]] float ItemDurabilityFactor(std::uint8_t durability, std::uint8_t baseDurability, std::uint32_t scalar1,
                                         std::uint8_t flags1b, std::uint8_t flags1c);

// 0x54efe0 (0x551289): +0x50, +0x52, +0x54 y +0x56 por la clase, la fuerza, la agilidad y la energía
// (cada una con su WORD de +0x2a, +0x2c o +0x30), las alas, las manos, el lugar 9 y los anillos, la
// lista de [0x6eed3c] (0x40d690 y 0x40d7f0) y, con flechas o virotes de nivel, un último ajuste.
// Reemplaza los lugares 0, 1, 9, 10 y 11. `items` es *[0x7758550] y `setOptions` la lista de
// [0x6eed3c]: nullptr si no están, y el error sale donde el original los lee.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWords50To56(OwnCharacterRecord& own,
                                                                     const OwnEquipmentOverride& held,
                                                                     const Data::Tables::ItemTable* items,
                                                                     ItemSetOptionList* setOptions);

// 0x54f9b0 (0x551290): +0x5a y +0x5c por la energía (más +0x30), las alas, la mano derecha, el lugar
// 9, los anillos y la lista (0x40d690 sobre +0xef4, que no pone en 0 antes, y 0x40d7f0). Reemplaza los
// lugares 0, 9, 10 y 11.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWords5aAnd5c(OwnCharacterRecord& own,
                                                                      const OwnEquipmentOverride& held,
                                                                      const Data::Tables::ItemTable* items,
                                                                      ItemSetOptionList* setOptions);

// 0x54fd40 (0x551297): +0x4e por la clase, el nivel, la fuerza, la agilidad y, en la clase 4, el
// mando (+0x20 más +0x36), y después 0x40d690.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWord4e(OwnCharacterRecord& own, ItemSetOptionList* setOptions);

// 0x54fe10 (0x55129e): las velocidades del keepalive, +0x4c y +0x58, por la clase y la agilidad, las
// armas, los anillos 0x1a14 (con el bit 0x04 de +0x3c, que prende o apaga abriendo y cerrando el
// registro: Render::Models::LogicalAccess sobre `ownRecordAccesses`, con 3.848 rand() si el cierre deja
// la cuenta en 0), los guantes, los bits 0x01 y 0x08 de +0x3c y las opciones 0x61. Reemplaza los
// lugares 0, 1, 5, 10 y 11.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnAttackAndMagicSpeeds(OwnCharacterRecord& own,
                                                                              const OwnEquipmentOverride& held,
                                                                              std::uint8_t& ownRecordAccesses,
                                                                              Render::Models::CrtRandom& random);

// 0x5507e0 (0x5512a5): +0x60 por la clase y la agilidad, el lugar 1 y las opciones 0x5a de los lugares
// 2 a 6, 11 y 10.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWord60(OwnCharacterRecord& own,
                                                                const Data::Tables::ItemTable* items);

// 0x550990 (0x5512ac): +0x62 por la clase y la agilidad, los lugares 1 a 7, lo de 0x505820, la lista
// (0x40d690, 0x40d7f0 dos veces y, con un tipo de 0xc00 a 0xdff en el lugar 1, 0x40d730) y las opciones
// 0x96 del lugar 8 y de Cape of Lord. Con un lugar con durabilidad y el tipo −1 lee fuera de Item.bmd,
// como Q-C184: error.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWord62(OwnCharacterRecord& own,
                                                                const Data::Tables::ItemTable* items,
                                                                const Scan505820& scan, ItemSetOptionList* setOptions);

// 0x550c30 (0x5512b3): +0x64 es el D+0x14 del último de los lugares 2 a 7 con durabilidad (0 si su
// tipo es −1; 0x54e6b0). Si ninguno tiene, +0x64 queda como estaba.
void UpdateOwnWord64(OwnCharacterRecord& own);

// 0x550c60 (0x5512ba): +0x66 es el D+0x18 del lugar 6 con durabilidad, más el del lugar 7 con
// durabilidad (0x54e6d0). Sin durabilidad en el 6 no lo pone: el del 7 se suma a lo que había.
void UpdateOwnWord66(OwnCharacterRecord& own);
} // namespace Client::Scenes
