#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// El keepalive (0E) que el cliente manda al GameServer al entrar y después cada 20 segundos.
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión", y el plan de la CL2
// (CL2.5).
//
// Lleva el tick del cliente y los WORD de personaje+0x4C y +0x58, que son la velocidad de ataque y
// la de magia: el GameServer original lee los de +8 y +10 del paquete y los compara con las suyas
// (0x415291 y 0x4152ac, en CGLiveClient del research del servidor) y, si no pasan, anota
// "velocidad de ataque editada" (el log de 0x41532b); si compara con != o con un < con o sin signo
// no está decidido. El GS del repack tiene esa comparación anulada: 0x41529b y 0x4152b6 saltan
// siempre al else (repack_patches.tsv). Antes de mandarlas, el original les resta 20 o 25 según
// dos bits del personaje (0x4bee60 y el WM_TIMER de 0x4af4e0).
namespace Client::Protocol::Connection
{
// Los bits de personaje+0x3C que cambian las velocidades del keepalive. Qué son, el research no lo
// nombró todavía.
inline constexpr std::uint8_t KeepaliveAdjust20 = 0x01; // resta 20; gana si están los dos
inline constexpr std::uint8_t KeepaliveAdjust25 = 0x08; // resta 25

// La velocidad que va en el keepalive: menos 20 con KeepaliveAdjust20 en `characterFlags`, si no,
// menos 25 con KeepaliveAdjust25, y si no, igual. Da la vuelta en 16 bits: 3 menos 25 es 0xFFEA.
[[nodiscard]] std::uint16_t KeepaliveSpeed(std::uint16_t speed, std::uint8_t characterFlags);

struct Keepalive
{
    std::uint32_t tick;        // el GetTickCount del cliente
    std::uint16_t attackSpeed; // ya con KeepaliveSpeed
    std::uint16_t magicSpeed;  // ya con KeepaliveSpeed
};

// 0E: C1 0C 0E 00, el tick (u32 little-endian) y las dos velocidades (u16 little-endian); con el
// XOR32 y para cifrar. El 00 alinea el tick a 4 bytes en el struct del GameServer.
[[nodiscard]] Outgoing WriteKeepalive(const Keepalive& keepalive);
} // namespace Client::Protocol::Connection
