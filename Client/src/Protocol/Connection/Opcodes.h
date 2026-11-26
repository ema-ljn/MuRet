#pragma once

#include "Network/Packet.h"

// The ConnectServer's head codes and the subcodes of its list (04-protocolo.md, "El flujo de conexión"). The
// GameServer's are in Protocol/Opcodes.h. dispatcher_cases.py checks both.
namespace Client::Protocol::Connection
{
// The GameServer's 00 is the chat: the original tells them apart by the scene (0x4e33e4), MuRet by a Dispatcher per
// server (CL4.3).
inline constexpr Network::Opcode ConnectHelloOpcode{0x00};
inline constexpr Network::Opcode ServerListOpcode{0xf4};
inline constexpr Network::Subcode ServerListRequestSubcode{0x02};
inline constexpr Network::Subcode ServerSelectSubcode{0x03};
inline constexpr Network::Subcode ServerBusySubcode{0x05};
inline constexpr Network::Subcode ServerListSubcode{0x06}; // as FoxAnti leaves it
} // namespace Client::Protocol::Connection
