#pragma once

#include <cstdint>

// Entrada de la decisión de gates (06 § Los cambios de mapa). Los defaults inicializan el DTO;
// no representan un snapshot del original. El caller debe completar todos los campos del paso.
namespace Client::GameLogic
{
struct GateCheckState
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t map = 0;
    std::uint16_t level = 0;
    std::uint8_t characterClass = 0;
    // Tipos de ítem de +0xdac/+0xd64, no modelos de CharacterAppearance; -1 es vacío.
    std::int16_t mount = -1;
    std::int16_t wings = -1;
    // WORD de 0x07879f80: la montura alternativa que solo consulta la restricción de Atlans.
    std::int16_t alternateMount = -1;
    // DWORD signed de 0x07879fbc > 0 (TEST/JLE en 0x59a22f..0x59a231), no solo distinto de cero.
    bool alternateMountActive = false;
    // Ya decrementado por MoveHero si era positivo; GateCheck exige exactamente cero.
    std::int32_t warpCounter = 0;
    // Resta unsigned de GetTickCount y el último 1C: conserva el wrap de 32 bits.
    std::uint32_t elapsedMs = 0;
    bool heroGateBlocked = false; // +0x399 distinto de cero.
    bool requestBlocked = false;  // 0x576794c distinto de cero.
    bool warpPending = false;     // 0x77f3bb0 distinto de cero.
};
} // namespace Client::GameLogic
