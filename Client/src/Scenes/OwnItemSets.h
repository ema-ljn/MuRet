#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Las opciones de los conjuntos del registro propio: 0x40e1b0, que 0x550d10 llama primero (0x550d24),
// con 0x40cfa0, 0x40d010 y 0x40de10, todos thiscall sobre [0x6eed3c]; 0x40da20 es el código de 0x40d690
// (Scenes::AddItemSetOptionValues, en ItemSetOptionList.h). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Los stats del registro propio", y 07, Q-C184 y
// Q-C187. Lo que el original lee o escribe fuera de sus tablas y listas es un error explícito, después
// de lo que hace antes.
namespace Client::Scenes
{
struct ActorState;
struct ItemSetOptionList;
struct ItemSetState;
struct OwnCharacterRecord;
struct OwnStatsContext;

// Una entrada de la lista local de 0x40e1b0, de a 3 bytes: el conjunto, cuántas piezas suyas se
// contaron y la clase de pieza de la última (el byte +0x1c & 3 menos 1).
struct ItemSetPiece
{
    std::uint8_t setId = 0;
    std::uint8_t count = 0;
    std::uint8_t kind = 0;
};

// El buffer de 0x40e1b0 es de 30 bytes (0x40e1bb..0x40e1d4), pero 0x40cfa0 y 0x40d010 recorren solo
// los cuatro primeros conjuntos (hasta el offset 9, 0x40cfe0 y 0x40d467).
inline constexpr std::size_t ItemSetPieceSlots = 4;
using ItemSetPieces = std::array<ItemSetPiece, ItemSetPieceSlots>;

// 0x40cfa0: con el byte +0x1c & 3 del ítem en 1, 2 o 3, el byte 0, 1 o 2 del registro `type` de
// ItemSetType.bmd es su conjunto; con 0 o 0xff, ninguno. Lo suma al conjunto igual o al primer lugar
// libre de los cuatro; sin lugar, nada. `type` es el tipo del ítem, que quien llama ya miró que no sea
// negativo. Con 8.192 o más, el original lee fuera de la tabla: error (Q-C187).
[[nodiscard]] Core::Expected<void, std::string> AddItemSetPiece(ItemSetPieces& pieces,
                                                                const Data::Tables::ItemSetTypeTable& types,
                                                                std::size_t type, std::uint8_t flags1c);

// 0x40d010: con cada conjunto de dos piezas o más, el nombre, las opciones de ItemSetOption.bmd que da
// la cantidad de piezas y, si está completo, las del conjunto completo; las opciones de 0x18 en
// adelante, solo si el conjunto marca la clase del héroe publicado. Las deja en la lista de `state`,
// primero las de la clase de pieza 0, y escribe el +0x358 del héroe.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemSetOptions(ItemSetState& state, const ItemSetPieces& pieces,
                                                                    const Data::Tables::ItemSetOptionTable& options,
                                                                    ActorState& publishedHero);

// 0x550d29..0x550d3f: las 16 marcas de +0x937c en 0.
void ClearItemSetOptionMarks(ItemSetOptionList& list);

// Las copias de los stats del registro que arma 0x40e1b0 (0x40e429..0x40e453) para mirar los requisitos
// de cada ítem en la segunda pasada.
struct OwnStatCopies
{
    std::uint16_t strength = 0; // +0x18
    std::uint16_t agility = 0;  // +0x1a
    std::uint16_t vitality = 0; // +0x1c
    std::uint16_t energy = 0;   // +0x1e
};

// 0x40de10: lo de 0x550d10 en sus doce lugares (0x40dc50, con 5 · valor) y 0x40da20 con 0x8c a 0x8f,
// pero sobre las copias: el tipo 1 a la fuerza, el 2 a la agilidad, el 3 a la energía y el 4 a la
// vitalidad. Abre y cierra el registro en cada lugar.
[[nodiscard]] Core::Expected<void, std::string>
AddOwnBonusesToStatCopies(OwnStatCopies& copies, const OwnCharacterRecord& own, const OwnStatsContext& context);

// 0x40e1b0: dos pasadas por los lugares del equipo salvo las alas y la mascota, que cuentan las piezas
// de cada conjunto (0x40cfa0) y aplican sus opciones (0x40d010). Entre las dos, 0x40de10 sobre las
// copias de los stats; la segunda pasada salta los ítems cuyos requisitos no dan esas copias. Abre y
// cierra el registro en cada lugar y una vez en el medio. Sin las tablas, error antes de tocar nada.
[[nodiscard]] Core::Expected<void, std::string> RecalculateOwnItemSets(const OwnCharacterRecord& own,
                                                                       const OwnStatsContext& context);
} // namespace Client::Scenes
