#pragma once

#include <cstdint>

// Los opcodes y subcódigos de la conexión, del ConnectServer al login en el GameServer.
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión". El subcódigo va justo
// después del opcode: pkt[3] en un C1 y pkt[4] en un C2.
namespace Client::Protocol::Connection
{
// 00: el saludo del ConnectServer, C1 04 00 01, con un handler para el opcode entero. En el
// GameServer el mismo opcode es el chat: el original los separa por la escena (0x4e33e4), y MuRet
// con un Dispatcher por servidor (CL4.3).
inline constexpr std::uint8_t ConnectHelloOpcode = 0x00;

// F4: la lista de servidores, con el ConnectServer.
inline constexpr std::uint8_t ServerListOpcode = 0xF4;
inline constexpr std::uint8_t ServerListRequestSubcode = 0x02; // pedir la lista
inline constexpr std::uint8_t ServerSelectSubcode = 0x03;      // elegir uno, y la respuesta
inline constexpr std::uint8_t ServerBusySubcode = 0x05;        // la respuesta si está ocupado
inline constexpr std::uint8_t ServerListSubcode = 0x06;        // la lista, como la deja FoxAnti

// F1: la entrada al GameServer.
inline constexpr std::uint8_t JoinOpcode = 0xF1;
inline constexpr std::uint8_t HelloSubcode = 0x00;        // el saludo del GameServer
inline constexpr std::uint8_t LoginSubcode = 0x01;        // el login y su respuesta
inline constexpr std::uint8_t LogoutSubcode = 0x02;       // la salida: el pedido del cliente y la respuesta
inline constexpr std::uint8_t DecryptErrorSubcode = 0x03; // el aviso de un C3/C4 que no se descifró

// 0E: el keepalive, sin subcódigo.
inline constexpr std::uint8_t KeepaliveOpcode = 0x0E;
} // namespace Client::Protocol::Connection
