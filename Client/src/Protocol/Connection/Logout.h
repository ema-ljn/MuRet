#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

// La salida del juego con el GameServer: la respuesta F1 02, que el GS manda para cerrar el juego,
// volver a la selección o volver a la lista de servidores, y lo que el cliente manda al seguirla.
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión" (F1 02), y Q-C106, Q-C79
// y Q-C211 a Q-C215 en 07-quirks-y-bugs.md.
namespace Client::Protocol::Connection
{
// El resultado que cierra el juego: 0x4c2710 hace SendMessageA(WM_DESTROY) (0x4c391b), en
// cualquier escena y llegue cifrado o no. Con 1 va a la selección y con 2 a la lista (Q-C79); con
// otro no hace nada. El GS los manda según su CloseType (GCCloseMsgSend, en el research del
// servidor).
inline constexpr std::uint8_t LogoutQuitsTheGame = 0;
// El resultado que vuelve a la selección: cifrado, 0x4c2710 limpia el juego, pasa a la escena 4 y
// pide la lista (0x4c3626..0x4c390f); sin cifrar solo avisa con el F1 03 00 F1 (06, "La vuelta a la
// selección").
inline constexpr std::uint8_t LogoutReturnsToSelection = 1;
// El que vuelve a la lista de servidores: lo pide el 1 de la ventana de Esc (0x647e28) y el GS lo
// contesta con el mismo (Q-C79).
inline constexpr std::uint8_t LogoutReturnsToServerList = 2;

// La respuesta F1 02: C1 05 F1 02 y el resultado. El GS la manda como C3, y llega acá ya
// descifrada como C1 (Q-C10). WrongPacket si no es un C1 F1 02, y TooShort si no trae el
// resultado: el original lee pkt[4] igual, lo que haya en su buffer.
[[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> ReadLogoutResult(const Network::Packet& packet);

// F1 02: C1 05 F1 02 y el tipo, con el XOR32 y para cifrar (0x647f60: lo arma con la clave de 0x648025 y
// lo manda como C3). El GS contesta con la respuesta F1 02 del mismo tipo (CL8, hito 8).
[[nodiscard]] Outgoing WriteLogoutRequest(std::uint8_t kind);
// F1 03: C1 06 F1 03 00 F1, con el XOR32 y para cifrar. Lo manda el F1 02 con 1 o con 2 que llega sin
// cifrar, en vez de cambiar de escena (0x4c2f64..0x4c35dd con 1, 0x4c2787..0x4c2ef2 con 2; estático):
// la misma forma que el aviso del F3 03 en claro, con el F1 del paquete en el último byte.
[[nodiscard]] Outgoing WriteClearLogoutReport();

// C1 03 31, en claro y sin XOR32: no tiene bytes después del opcode. Lo manda 0x419900 (0x419a0e), que
// la vuelta a la selección llama desde 0x4c2480 (0x4c2546), y otra vez 0x4499b0 (0x449ad1) desde
// 0x657670, con la caja de 0x723c80 vacía (Q-C214). Qué hace con él el GameServer no está leído: el
// nombre sale de lo que hace el cliente al mandarlo, que cierra lo suyo (+0x21383 de [0x710148] en 0, y
// 0x724580 y 0x724584 en 0; inferido).
[[nodiscard]] Outgoing WriteWindowClose();
} // namespace Client::Protocol::Connection
