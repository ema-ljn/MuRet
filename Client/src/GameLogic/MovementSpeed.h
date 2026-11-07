#pragma once

#include <cstdint>

// 06-render-escenas-y-ui § Caminar, velocidad 0x508940 completa: valor y estado persistente.
namespace Client::GameLogic
{
struct MovementSpeedInput
{
    std::uint16_t type = 0;
    std::uint16_t baseSpeed = 0;
    std::uint16_t item = 0xffff;
    std::uint16_t wings = 0xffff;
    std::uint8_t movementMode = 0;
    std::uint8_t itemVariant = 0;
    std::uint8_t safeZone = 0;
    std::uint32_t flags = 0;
    std::int32_t map = -1;
};

// Una sola instancia entre actores: D0/D4 del original, no estado por actor. Los defaults son
// del DTO; no afirman cómo se inicializan los globales nativos. D4 se compara como signed32.
struct MovementSpeedSharedState
{
    std::uint32_t itemLatch = 0;
    std::uint32_t itemCounterBits = 0;
};

// Run es el único byte mutable del actor y admite 0..255. Inputs estables, memorias distintas y
// estado con un solo dueño durante la llamada. Proyecta el retorno y los tres estados; no replica
// ABI, slot de argumento, status x87, fallos ni aliases arbitrarios del original.
[[nodiscard]] float UpdateMovementSpeed(std::uint8_t& run, MovementSpeedSharedState& shared,
                                        const MovementSpeedInput& input);
} // namespace Client::GameLogic
