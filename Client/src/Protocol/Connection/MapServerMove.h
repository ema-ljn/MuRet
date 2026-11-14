#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// El cambio de servidor del mapa (CL9 hito 13; 04 § Castle Siege, la batalla): el B1 00 con que el GameServer manda
// al cliente a otro (0x4e5056 → 0x412b30) y el B1 01 con que el cliente se presenta en el nuevo cuando saluda
// (0x4c1e20 → 0x412bb0). El GameServer 0.97 del laboratorio no tiene ninguno de los dos: cada byte sale del ASM del
// cliente, *estático*.
namespace Client::Protocol::Connection
{
inline constexpr std::uint8_t MapServerOpcode = 0xB1;
// 0x4e506a: el dispatcher solo atiende el 00 (el subcódigo, pkt[3] en un C1 y pkt[4] en los demás).
inline constexpr std::uint8_t MapServerMoveSubcode = 0x00;
// 0x412c5f: el que arma 0x412bb0.
inline constexpr std::uint8_t MapServerAuthSubcode = 0x01;
// 0x4e5073..0x4e5080: los 0x24 bytes desde pkt+4 que 0x412aa0 copia a +0x18 del objeto de 0x6eed7c: el host en
// +0x18 (pkt+4), el puerto en +0x28 (pkt+0x14) y los cuatro dwords de +0x2c..+0x3b (pkt+0x18..0x27) que 0x412bb0
// devuelve tal cual. +0x2a y +0x2b (pkt+0x16 y 0x17) no los lee nadie.
inline constexpr std::size_t MapServerMoveBytes = 0x24;
inline constexpr std::size_t MapServerCodesSize = 0x10;
using MapServerCodes = std::array<std::byte, MapServerCodesSize>;

struct MapServerMove
{
    // +0x18: lo que 0x4c1340 toma como texto, hasta el primer 0. Un host de 16 bytes sin 0 sigue en el puerto y los
    // códigos, que son del mismo objeto; sin ningún 0 en los 0x24 bytes, la lectura sale del objeto (Q-C1402) y
    // `hostEnds` queda en false.
    std::string host;
    bool hostEnds = true;
    // +0x28: MOV AX, word: el byte de abajo primero.
    std::uint16_t port = 0;
    MapServerCodes codes{};
};

// B1 00 (0x4e5056): WrongPacket con otro opcode u otro subcódigo, TooShort sin los 0x24 bytes desde pkt+4. Lee a
// desplazamientos fijos desde el primer byte, también en un C2 (donde pkt[4] es a la vez el subcódigo y el primer
// byte del host).
[[nodiscard]] Core::Expected<MapServerMove, ProtocolError> ReadMapServerMove(const Network::Packet& packet);

// Lo que pone 0x412bb0 en el B1 01. No tiene operator<<: la cuenta no se imprime nunca.
struct MapServerAuth
{
    // 0x576792c, la cuenta del login.
    std::string_view account;
    // +0xc del objeto de 0x6eed7c: el nombre del elegido, que 0x4129f0 guarda al entrar al mundo (0x634906).
    std::string_view character;
    MapServerCodes codes{};
    std::uint32_t tick = 0; // el GetTickCount de 0x412bb0
};

// B1 01 (0x412bb0): C1 45 B1 01, la cuenta en 12 bytes (strcpy sobre 12 en 0 y el XOR3 sobre los 12, 0x4c1690(…,
// 0xc)), el nombre en 12 (strcpy sobre 12 en 0, sin el XOR3, cortado en 10 por 0x412c1e), los 16 bytes de los
// códigos, el tick (u32 de abajo primero), la versión (0x6c8284[i] − i − 1, "10209") y el serial (0x6c828c); con el
// XOR32 y para cifrar como C3. FieldTooLong con una cuenta de más de 10 bytes: el strcpy del original pisaría lo que
// sigue en su pila (la del login no pasa de 10).
[[nodiscard]] Core::Expected<Outgoing, ProtocolError> WriteMapServerAuth(const MapServerAuth& auth);
} // namespace Client::Protocol::Connection
