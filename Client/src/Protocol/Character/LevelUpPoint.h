#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// El pedido de poner un punto de los de subir de nivel (F3 06, C->S), desde los botones de la ventana del personaje
// (0x5e468e..0x5e4a12, CL8 hito 5): C1 05 F3 06 y la estadística, con el XOR32 desde el byte 3, como lo arma el
// PacketWriter del original en la pila y lo manda con send() sin pasar por la cola (04-protocolo, "F3 06").
namespace Client::Protocol::Character
{
// Las estadísticas de los botones, de arriba hacia abajo: la fuerza, la agilidad, la vitalidad, la energía y, solo
// el Dark Lord (la clase 4), el comando.
inline constexpr std::uint8_t StrengthPoint = 0;
inline constexpr std::uint8_t AgilityPoint = 1;
inline constexpr std::uint8_t VitalityPoint = 2;
inline constexpr std::uint8_t EnergyPoint = 3;
inline constexpr std::uint8_t CommandPoint = 4;

[[nodiscard]] Outgoing WriteLevelUpPoint(std::uint8_t stat);
} // namespace Client::Protocol::Character
