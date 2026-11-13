#pragma once

#include "App/IChatRoomLinks.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/FriendChat.h"
#include "Protocol/Letters.h"

#include <cstdint>
#include <string>

// Las cartas y el chat con un amigo del juego (CL9 hito 10; 04 § Las cartas y el chat con un amigo, y 06 § HUD y
// ventanas): los paquetes C5..C8 (0x4dc840..0x4dcd90), el CA y el CB (0x4dce10 y 0x4dd020) y lo que llega por la
// conexión de una sala (0x4e6240), sobre el administrador de las ventanas viejas y los modelos de [0x576351c] y
// [0x5763520].
namespace Client::UI
{
struct ChatRooms;
}

namespace Client::Scenes
{
struct ChatReceiveContext;

// 0x4dc840: 0, 2, 3, 4, 6 y 7, la ventana de escribir (si viene) sin el envío pendiente (+0x778 = 0, Q-C1001) y una
// caja con el texto 1053, 1061, 1064, 1065, 1068 o 423; 1, la ventana se cierra (el 4) y el texto 1046 con el costo
// (1000) al chat.
[[nodiscard]] Core::Expected<void, std::string> ApplyLetterSendResult(const Protocol::Letters::LetterSendResult& result,
                                                                      const ChatReceiveContext& context);

// 0x4dc9e0: con el tipo 0 o 1, la carta al modelo y el orden por la fecha (2); con el 2, el sonido 0x29, la marca de
// carta nueva (0x4a6420), el texto 1062 al chat, la carta como no leída y el orden que estaba (−1); después, la pestaña
// de las cartas (0x4a0960) y, con el tope de 0x5767970 alcanzado, el texto 1073.
[[nodiscard]] Core::Expected<void, std::string> ApplyLetterRow(const Protocol::Letters::LetterRow& row,
                                                               const ChatReceiveContext& context);

// 0x4dcb70: la copia del paquete (0x4a03c0), la carta como leída (+0x134 = 1), la pestaña, la ventana de leer (el 6
// con el texto 1054 y el asunto, en (100, 100) o donde la dejó la anterior), el cuerpo (0x499cb0), la ventana en +0x94
// (0x491720) y el retrato (la clase, el equipo, la acción, el ángulo y el zoom) salvo la carta de "webzen"; el 0xe a la
// ventana.
[[nodiscard]] Core::Expected<void, std::string> ApplyLetterRead(const Protocol::Letters::LetterRead& letter,
                                                                const ChatReceiveContext& context);

// 0x4dcd90: 0, la caja con el texto 1055; 1, la carta fuera del modelo y su copia (0x49ee20 y 0x4a0500). Después, la
// pestaña.
[[nodiscard]] Core::Expected<void, std::string>
ApplyLetterDeleteResult(const Protocol::Letters::LetterDeleteResult& result, const ChatReceiveContext& context);

// 0x4dce10: 0 y 2, el amigo fuera de los pedidos (0x4a6bc0) y la caja 1069 o 1070; 1, según el tipo, el chat que pedí
// (el 1), el que ya estaba con el amigo (sale de su sala) o uno minimizado y al fondo (el 2), y la sala (0x493530).
[[nodiscard]] Core::Expected<void, std::string> ApplyRoomAnswer(const Protocol::FriendChat::RoomAnswer& answer,
                                                                const ChatReceiveContext& context);

// 0x4dd020: el resultado de invitar en el chat de la ventana: 0, el texto 1056; 1, el invitado y el 1057; 3, el 1074.
[[nodiscard]] Core::Expected<void, std::string> ApplyInviteResult(const Protocol::FriendChat::InviteResult& result,
                                                                  const ChatReceiveContext& context);

// 0x4e6240: lo que llega por la conexión de la sala del chat `window` (00, 01, 02, 04 y 0D; App::ChatRoomPacket).
[[nodiscard]] Core::Expected<void, std::string> ApplyRoomPacket(std::int32_t window, const Network::Packet& packet,
                                                                const ChatReceiveContext& context);

// El cierre de la conexión de una sala (App::ChatRoomClosed): abierta, el FD_CLOSE de 0x49e480, el 402 al chat; sin
// abrir, la sala se olvida y su lugar queda tomado (Q-C1005).
[[nodiscard]] Core::Expected<void, std::string> ApplyChatRoomClosed(const App::ChatRoomClosed& closed,
                                                                    const ChatReceiveContext& context);

// Lo que las ventanas dejaron para las salas (UI::ChatRooms::orders), en orden: el C1 04, el C1 03 01 y el cierre, o el
// C1 03 05.
// Sin las conexiones, se descarta.
void FlushTheChatRoomOrders(UI::ChatRooms& rooms, App::IChatRoomLinks* links);
} // namespace Client::Scenes
