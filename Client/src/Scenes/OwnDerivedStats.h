#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
struct ItemState;
}

// Los stats del registro propio: 0x550d10, que 0x512a30 llama en 0x512ef5 (thiscall sobre
// [0x7681eb4]). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los stats del registro
// propio", y 07, Q-C184 y Q-C185.
//
// Lo primero que hace es 0x40e1b0, que MuRet no tiene: RecalculateOwnStats corta ahí. Los pasos que
// siguen y no dependen de lo que falta están portados aparte, cada uno con su dirección, para cuando
// exista lo de antes; los otros (0x40da20 y los ocho helpers de 0x551287..0x5512ba) siguen sin leer.
namespace Client::Scenes
{
struct OwnCharacterRecord;

// 0x550d10 sobre `own`. Corta en su primera llamada, 0x40e1b0 (0x550d24): las opciones de los
// conjuntos de [0x6eed3c], con las tablas de ItemSetType.bmd e ItemSetOption.bmd, que MuRet no carga.
// No toca el registro ni saca rand(): el original no hace nada antes.
[[nodiscard]] Core::Expected<void, std::string> RecalculateOwnStats(OwnCharacterRecord& own);

// 0x54ef00 (0x550d44): los WORD +0x2a, +0x2c, +0x2e, +0x30, +0x36, +0x32 y +0x34 en 0.
void ClearOwnItemStatWords(OwnCharacterRecord& own);

// Lo que 0x40dc50 da de un ítem.
struct ItemStatBonus
{
    // El byte +0x44 del registro de Item.bmd de su tipo, lo que devuelve.
    std::uint8_t kind = 0;
    // (D+0x1c >> 2) & 3, el WORD que escribe en su segundo argumento.
    std::uint16_t value = 0;
};

// 0x40dc50. Indexa Item.bmd con el tipo con signo: con uno fuera de la tabla (−1 en un lugar vacío),
// el original lee memoria que no es de la tabla; MuRet devuelve un error (Q-C184).
[[nodiscard]] Core::Expected<ItemStatBonus, std::string> ItemStatBonusOf(const World::ItemState& item,
                                                                         const Data::Tables::ItemTable& items);

// 0x550d49..0x550db1: por cada uno de los doce lugares con la durabilidad distinta de 0, 0x40dc50, y
// con el tipo 1, 2, 3 o 4, 5 · valor a +0x2a, +0x2c, +0x30 o +0x2e (16 bits, envuelve). `items` es
// *[0x7758550], nullptr si no se cargó. Si corta en un lugar, lo de los anteriores queda sumado.
[[nodiscard]] Core::Expected<void, std::string> AddOwnItemStatBonuses(OwnCharacterRecord& own,
                                                                      const Data::Tables::ItemTable* items);

// 0x550e17..0x551284: con Cape of Lord (0x1a1e, el 13 30 de Item.bmd) en el lugar de las alas, abre el
// registro, suma a +0x36 los valores de sus opciones 0x90 (0x54e540, Scenes::AddOwnItemOptionValues) y
// lo cierra. El cierre es el de Render::Models::LogicalAccess sobre `ownRecordAccesses`, con los 3.848
// rand() si deja la cuenta en 0. Con otro tipo no abre ni toca nada. Con más de nueve opciones el
// original lee el lugar siguiente, que MuRet no proyecta: error, con el acceso cerrado.
[[nodiscard]] Core::Expected<void, std::string>
AddCapeOfLordOptions(OwnCharacterRecord& own, std::uint8_t& ownRecordAccesses, Render::Models::CrtRandom& random);

// 0x5512bf..0x5513b1, la cola: restas de 16 bits que envuelven, un rand() en un rango con IDIV, dos
// topes de 100 y dos rand() % 100 que deciden +0xf02 y +0xf03. Con +0xee2 = +0xee4 + 1 el IDIV divide
// por 0 (Q-C185): error, con +0xef8, +0xefa y el primer rand() ya hechos, como en el original.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnDerivedStatsTail(OwnCharacterRecord& own,
                                                                          Render::Models::CrtRandom& random);
} // namespace Client::Scenes
