#pragma once

#include "Core/Expected.h"
#include "Fauna/IFishStepServices.h"

#include <span>
#include <system_error>
#include <cstddef>
#include <cstdint>

// Pure policies used by step 0x447030. The orchestration keeps the late reads of map, tile and predicate 0x425070
// around these helpers.
namespace Client::Fauna
{
// Only the current map's prefix is processed; the rest of the pool keeps its fields and flags.
[[nodiscard]] std::size_t FishStepCount(std::int32_t map);

// Direct admission guards: surface is layer 1 and attributes the .att word. Maps 8/33/34/54 can only enter through
// the later fallback and are not admitted here.
[[nodiscard]] bool FishDirectSpawnAllowed(std::int32_t map, std::uint8_t surface, std::uint16_t attributes);

// Pure boundaries matching predicate 0x425070 and the water word 0/2.
[[nodiscard]] bool FishSpawnEventGate(std::int32_t map);
[[nodiscard]] bool FishOpenWaterTile(std::uint16_t attributes);

enum class FishMovement : std::uint8_t
{
    Complete,
    Reduced,
};

// 44769a: signed model range b2..bc always uses full movement; other models use timer>0.
[[nodiscard]] FishMovement FishMovementFor(std::int16_t model, std::int32_t timer);
// 4476b1: only b1 and 148 request movement mode1 after selecting complete movement.
[[nodiscard]] std::int32_t FishMovementMode(std::int16_t model);
// 447661: only b9/ba update the retained oscillation fields; no RNG is consumed here.
[[nodiscard]] bool FishNeedsOscillation(std::int16_t model);
// 4478d1: after collision, only b2/148 copy position to the auxiliary triplet.
[[nodiscard]] bool FishCopiesWorldPosition(std::int16_t model);
// 447603 skips model lookup only for signed model -1 (raw word ffff).
[[nodiscard]] bool FishHasModel(std::int16_t model);
// 447472 and 4474ba: the lower half of Atlans draws one of Fish04..07, the upper half Fish08 or Fish09.
[[nodiscard]] std::int16_t AtlansFishModel(bool lower, std::int32_t draw);
// 0x5f82a0: wrap independent of the two low bytes, unlike 0x5f82c0.
[[nodiscard]] std::uint32_t FishTileIndex(std::int32_t x, std::int32_t y);

// Caller 0x447030, complete with respect to the required services. Does not connect Main/scene/renderer. A reached
// failure keeps the prefix; services of branches not taken are not validated in advance.
[[nodiscard]] Core::Expected<void, std::error_code> StepFish(std::span<FishStepView, FishPoolSize> pool,
                                                             const FishStepWorld& world, IFishStepServices& services);
} // namespace Client::Fauna
