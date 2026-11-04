#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

// La salida del juego con el GameServer: la respuesta F1 02, que el GS manda para cerrar el juego,
// volver a la selección o volver a la lista de servidores.
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión" (F1 02), y Q-C106 y
// Q-C79 en 07-quirks-y-bugs.md.
namespace Client::Protocol::Connection
{
// El resultado que cierra el juego: 0x4c2710 hace SendMessageA(WM_DESTROY) (0x4c391b), en
// cualquier escena y llegue cifrado o no. Con 1 va a la selección y con 2 a la lista (Q-C79); con
// otro no hace nada. El GS los manda según su CloseType (GCCloseMsgSend, en el research del
// servidor).
inline constexpr std::uint8_t LogoutQuitsTheGame = 0;

// La respuesta F1 02: C1 05 F1 02 y el resultado. El GS la manda como C3, y llega acá ya
// descifrada como C1 (Q-C10). WrongPacket si no es un C1 F1 02, y TooShort si no trae el
// resultado: el original lee pkt[4] igual, lo que haya en su buffer.
[[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> ReadLogoutResult(const Network::Packet& packet);
} // namespace Client::Protocol::Connection
