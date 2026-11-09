#pragma once

#include "Data/Tables/ItemTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

// Estado de ítem de 0x54bf40 y campos previos de 0x5bc740. No es un layout C ni un decoder wire.
namespace Client::World
{
// Máximo propio, con metadata estable y frontera 0x4225d0 válida: nueve opciones.
inline constexpr std::size_t MaxItemOptions = 9;
inline constexpr std::size_t ItemOptionValueOffset = 8; // D+0x33 frente a D+0x2b
inline constexpr std::size_t ItemOptionStorageSize = ItemOptionValueOffset + MaxItemOptions;

struct ItemState
{
    std::uint16_t id = 0;
    std::uint32_t scalar1Low = 0; // D+4: copia zero-extended de B1.low8
    std::uint8_t itemClass = 0;
    // Nombres neutros: el contrato acredita offsets y cálculos, no nombres de estadísticas.
    std::uint8_t byte0a = 0;
    std::uint16_t word0c = 0;
    std::uint16_t word0e = 0;
    std::uint8_t byte10 = 0;
    std::uint16_t word12 = 0;
    std::uint16_t word14 = 0;
    std::uint8_t byte16 = 0;
    std::uint8_t byte17 = 0;
    std::uint16_t word18 = 0;
    std::uint8_t durability = 0; // D+1a, puesto por 0x5bc740, no por 0x54bf40
    std::uint8_t flags1b = 0;
    std::uint8_t flags1c = 0;
    std::uint16_t word1e = 0;
    std::uint16_t word20 = 0;
    std::uint16_t word22 = 0;
    std::uint16_t word24 = 0;
    std::uint16_t word26 = 0;
    std::uint16_t word28 = 0;
    std::uint8_t optionCount = 0;
    // Una sola región D+2b..3b: tag8 pisa valor0. Los valores tag-only conservan su historia.
    std::array<std::uint8_t, ItemOptionStorageSize> optionBytes{};
    std::uint32_t occupied = 0;  // D+3c, tampoco lo toca el inicializador
    std::uint8_t gridColumn = 0; // D+42/43, sólo productores de grillas
    std::uint8_t gridRow = 0;

    // Precondición: index < MaxItemOptions; no implica index < optionCount.
    // Un índice inválido corta con diagnóstico, también en Release.
    [[nodiscard]] std::uint8_t OptionTag(std::uint8_t index) const;
    [[nodiscard]] std::uint8_t OptionValue(std::uint8_t index) const;
};

struct ItemInitializationScalars
{
    std::uint32_t b1 = 0;
    std::uint32_t b3 = 0;
    std::uint32_t b4 = 0;
};

// Única frontera opaca, síncrona en el punto 0x54cc5c. No se invoca para otros IDs.
using Item4225d0 = std::function<void(ItemState&)>;

// Comportamiento propio completo de 0x54bf40. ID ya puesto, metadata del ID con vida estable y
// sin alias con el destino; no impone guard de sentinel ni rollback que el original no tiene.
// B3 completo importa para ID180b/L>6. contextWord es el word de S+e, releído sólo si lo pide la
// familia tardía. Puede referenciar D+e: se observa su valor posterior a los cálculos propios,
// no un snapshot de entrada. No se inventan upper24 de direcciones de la máquina actual.
// Para ID1a04, apply4225d0 debe existir y producir un estado válido de su contrato: ID preservado,
// count0/1->1/2 y bookkeeping ordinario sin alias con metadata. La rama transitiva queda abierta.
// Una función vacía en esa rama viola la precondición y lanza; no es un éxito silencioso.
void InitializeItemState(ItemState& item, std::span<const std::byte, Data::Tables::ItemRecordSize> metadata,
                         ItemInitializationScalars scalars, const std::uint16_t& contextWord,
                         const Item4225d0& apply4225d0);
} // namespace Client::World
