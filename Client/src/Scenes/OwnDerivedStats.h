#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/OwnStatHelpers.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Data::Tables
{
struct ItemSetTables;
}

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
// propio", y 07, Q-C184, Q-C185 y Q-C187.
//
// RecalculateOwnStats corre en orden todo lo leído: 0x40e1b0 (Scenes/OwnItemSets.h), la limpieza de
// [0x6eed3c] + 0x937c, 0x54ef00, los doce lugares, 0x40da20 cinco veces, la rama de Cape of Lord, los
// ocho helpers de 0x551287..0x5512ba (Scenes/OwnStatHelpers.h) y la cola (UpdateOwnDerivedStatsTail).
namespace Client::Scenes
{
struct ActorState;
struct ItemSetOptionList;
struct ItemSetState;
struct OwnCharacterRecord;

// Lo que 0x550d10, y 0x40e1b0 desde ella, leen y escriben fuera del registro propio. Quien llama lo
// presta durante la llamada.
struct OwnStatsContext
{
    // Las tablas de [0x6eed3c] que carga 0x40c600 (ItemSetType.bmd e ItemSetOption.bmd) y lo que
    // 0x40e1b0, 0x40d010 y 0x40da20 escriben en [0x6eed3c] y dura de una llamada a otra
    // (WorldState::ItemSets). Con alguno en nullptr, 0x40e1b0 corta con error antes de tocar nada.
    const Data::Tables::ItemSetTables* itemSetTables = nullptr;
    ItemSetState* itemSets = nullptr;
    // *[0x7758550], Item.bmd: 0x40dc50, desde 0x40de10 y desde los doce lugares de 0x550d10. nullptr
    // si no se cargó: corta con error en el primer lugar que la lee.
    const Data::Tables::ItemTable* items = nullptr;
    // DAT_0743a170, el héroe publicado: 0x40d010 lee su clase (+0x1d4) y escribe su +0x358.
    ActorState& publishedHero;
    // La cuenta de accesos del registro propio (Render::Models::LogicalAccess): 0x40e1b0 y 0x40de10 lo
    // abren y lo cierran en cada lugar, y la rama de Cape of Lord una vez. Cada cierre que la deja en
    // 0 saca 3.848 rand() de `random`.
    std::uint8_t& ownRecordAccesses;
    Render::Models::CrtRandom& random;
    // El ítem de 0x7879f80 y sus condiciones, para 0x54efe0, 0x54f9b0 y 0x54fe10. Por defecto no
    // reemplaza nada: [0x7879fbc] está en la .bss (0 al arrancar) y solo lo escriben handlers del
    // protocolo que MuRet no tiene (0x4d1c89 y 0x4d29cb; estático).
    OwnEquipmentOverride held{};
    // [0x743a178] y [0x743a17c], que lee 0x550990: lo que dejó el último 0x505820 (WorldRuntime::fullSetScan).
    // Desde 0x512a30 con el héroe publicado, el de esa misma llamada; si no, el del último dibujo de los
    // personajes (0x50ba30, en 0x50c91e). nullopt si MuRet no sabe qué dejó: 0x550990 corta con error.
    std::optional<Scan505820> scan505820 = std::nullopt;
};

// 0x550d10 sobre `own`, entera: 0x40e1b0, la limpieza, 0x54ef00, los doce lugares, 0x40da20 cinco veces,
// Cape of Lord, los ocho helpers de 0x551287..0x5512ba (Scenes/OwnStatHelpers.h) en su orden y la cola
// (UpdateOwnDerivedStatsTail). Un error de un paso corta ahí, con lo de los pasos anteriores hecho y los
// accesos cerrados. Sin `scan505820` corta en 0x550990, con los cinco helpers de antes hechos.
[[nodiscard]] Core::Expected<void, std::string> RecalculateOwnStats(OwnCharacterRecord& own,
                                                                    const OwnStatsContext& context);

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

// 0x550dc2..0x550e12: 0x40da20 (el código de 0x40d690) sobre `list` con +0x2a y 0x8c, +0x2c y
// 0x8d, +0x30 y 0x8e, +0x2e y 0x8f, y +0x36 y 0x90, en ese orden. Si corta en uno, lo de los
// anteriores queda sumado.
[[nodiscard]] Core::Expected<void, std::string> AddOwnItemSetOptionValues(OwnCharacterRecord& own,
                                                                          ItemSetOptionList& list);

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
