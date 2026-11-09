#pragma once

#include "Core/Expected.h"
#include "World/IFishStepServices.h"

#include <span>
#include <system_error>
#include <cstddef>
#include <cstdint>

// Políticas puras usadas por el paso 0x447030. La orquestación conserva las lecturas tardías de mapa,
// tile y del predicado 0x425070 alrededor de estos helpers.
namespace Client::World
{
// Solo el prefijo del mapa actual se procesa; el resto del pool conserva sus campos y flags.
[[nodiscard]] std::size_t FishStepCount(std::int32_t map);

// Guardas de admisión directa: surface es capa1 y attributes el word .att. Mapas8/33/34/54 sólo
// pueden entrar por el fallback posterior y no se admiten aquí.
[[nodiscard]] bool FishDirectSpawnAllowed(std::int32_t map, std::uint8_t surface, std::uint16_t attributes);

// Fronteras puras correspondientes al predicado 0x425070 y a la palabra de agua 0/2.
[[nodiscard]] bool FishSpawnEventGate(std::int32_t map);
[[nodiscard]] bool FishOpenWaterTile(std::uint16_t attributes);

enum class FishMovement : std::uint8_t
{
    Complete,
    Reduced,
};

// 44769a: signed type range b2..bc always uses full movement; other types use timer>0.
[[nodiscard]] FishMovement FishMovementFor(std::int16_t type, std::int32_t timer);
// 4476b1: only b1 and 148 request movement mode1 after selecting complete movement.
[[nodiscard]] std::int32_t FishMovementMode(std::int16_t type);
// 447661: only b9/ba update the retained oscillation fields; no RNG is consumed here.
[[nodiscard]] bool FishNeedsOscillation(std::int16_t type);
// 4478d1: after collision, only b2/148 copy position to the auxiliary triplet.
[[nodiscard]] bool FishCopiesWorldPosition(std::int16_t type);
// 447603 skips model lookup only for signed type -1 (raw word ffff).
[[nodiscard]] bool FishHasModel(std::int16_t type);
// 5f82a0: wrap independiente de los dos bytes bajos, distinto de5f82c0.
[[nodiscard]] std::uint32_t FishTileIndex(std::int32_t x, std::int32_t y);

// Caller447030 completo respecto de servicios requeridos. No conecta Main/escena/renderer.
// Un fallo alcanzado conserva el prefijo; no valida por adelantado servicios de ramas no tomadas.
[[nodiscard]] Core::Expected<void, std::error_code> StepFish(std::span<FishStepView, FishPoolSize> pool,
                                                             const FishStepWorld& world, IFishStepServices& services);
} // namespace Client::World
