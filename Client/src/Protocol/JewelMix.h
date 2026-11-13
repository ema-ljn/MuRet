#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// 04 § Las ventanas de los NPC, "La mezcla de joyas" (CL9 hito 6): el BC que mandan los diálogos de la mezcla de
// joyas por 0x4029e0, juntar (BC 00, 0x402500) o deshacer un paquete (BC 01, 0x401f30). Los dos arman el C1 de a un
// byte con el XOR32 de cada byte nuevo (la clave de 32 bytes desde 0xe7 0x6d 0x3a, desde pkt[3]) y lo mandan con
// send() en claro, sin la tabla de protección. El GameServer 0.97 no atiende el BC: el ASM es el oráculo.
namespace Client::Protocol::JewelMix
{
inline constexpr std::uint8_t JewelMixOpcode = 0xbc;
inline constexpr std::uint8_t MixSubcode = 0x00;
inline constexpr std::uint8_t UnmixSubcode = 0x01;

// 0x402500: C1 06 BC 00 con la joya (0x6c62e0) y el paquete (0x6c62e1 / 10 − 1: 0, 1 o 2 para 10, 20 o 30).
[[nodiscard]] Outgoing WriteJewelMix(std::uint8_t kind, std::uint8_t bundle);

// 0x401f30: C1 07 BC 01 con la joya (0x6c62e0), el nivel del paquete (0x6c62dc) y su lugar del inventario
// (0x6c62d8, que 0x401f5a ya pasó de casilla a lugar sumando 12).
[[nodiscard]] Outgoing WriteJewelUnmix(std::uint8_t kind, std::uint8_t level, std::uint8_t slot);
} // namespace Client::Protocol::JewelMix
