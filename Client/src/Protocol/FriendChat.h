#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// El chat con un amigo (CL9 hito 10; 04, "Las cartas y el chat con un amigo"). Con el GameServer: el pedido de una
// sala (CA) y la invitación (CB), que van y vuelven (0x4dce10 y 0x4dd020). Con el servidor del chat, una conexión
// aparte por sala al puerto 55980 (0x49de50): lo que llega (00, 01, 02, 04 y 0D, que 0x4e1d50 despacha con la ventana
// a 0x4dd1b0..0x4dd540) y lo que manda la ventana del chat (la entrada, 0x493530; el mensaje, 0x4944c0; la salida,
// 0x493a70). La reconstrucción del GameServer 0.97 no tiene nada de esto: el oráculo es el ASM del cliente
// (*estático*). Los handlers del servidor del chat leen el paquete por su lugar desde pkt[0], sea C1 o C2.
namespace Client::Protocol::FriendChat
{
inline constexpr std::uint8_t RoomRequestOpcode = 0xca;
inline constexpr std::uint8_t InviteOpcode = 0xcb;
// Los del servidor del chat.
inline constexpr std::uint8_t JoinOpcode = 0x00;
inline constexpr std::uint8_t MemberOpcode = 0x01;
inline constexpr std::uint8_t MembersOpcode = 0x02;
inline constexpr std::uint8_t MessageOpcode = 0x04;
inline constexpr std::uint8_t NoticeOpcode = 0x0d;
// El que manda el timer 1002 a cada sala (0x4a6550).
inline constexpr std::uint8_t PingOpcode = 0x05;
// El puerto del servidor del chat (0x49df17, PUSH 0xdaac).
inline constexpr std::uint16_t ChatPort = 55980;
inline constexpr std::size_t ChatNameSize = 10;
// CA: la IP en 15 bytes.
inline constexpr std::size_t ChatAddressSize = 15;
// 04: el tope del mensaje (0x4dd3fc, con más no hace nada) y el del EDIT del chat (0x493220).
inline constexpr std::uint8_t MessageLimit = 100;
// 02: cada miembro, el número y el nombre.
inline constexpr std::size_t MemberRowSize = 11;
// 0D: 0x4dd540 corta el texto en pkt[0x68].
inline constexpr std::size_t NoticeEnd = 0x68;

// C1 CA (0x4dce10): pkt[3] la IP del servidor del chat (15), pkt[0x12] la sala (u16), pkt[0x14] el ticket (u32),
// pkt[0x18] el tipo (0, la pedí yo; 1, me invitan o me llaman; 2, otra), pkt[0x19] el nombre (10) y pkt[0x23] el
// resultado (0, no está; 1, bien; 2, fuera de línea).
struct RoomAnswer
{
    std::string address;
    std::uint16_t room = 0;
    std::uint32_t ticket = 0;
    std::uint8_t type = 0;
    std::string name;
    std::uint8_t result = 0;
};

// C1 CB (0x4dd020): pkt[3] el resultado y pkt[4] la ventana del chat (u32).
struct InviteResult
{
    std::uint8_t result = 0;
    std::uint32_t window = 0;
};

// 00 (0x4dd1b0): pkt[3] el resultado de entrar a la sala.
struct JoinResult
{
    std::uint8_t result = 0;
};

// 01 (0x4dd1e0): pkt[3] 0 entra o 1 sale, pkt[4] su número y pkt[5] su nombre (strncpy de 10: hasta el primer '\0').
struct Member
{
    std::uint8_t kind = 0;
    std::uint8_t index = 0;
    std::string name;
};

// 02 (0x4dd350): pkt[6] cuántos y desde pkt[8] los miembros de 11 bytes (el número y el nombre, con strncpy de 10).
struct MemberRow
{
    std::uint8_t index = 0;
    std::string name;
    friend bool operator==(const MemberRow&, const MemberRow&) = default;
};
struct Members
{
    std::vector<MemberRow> members;
};

// 04 (0x4dd3d0): pkt[3] el número del que habla, pkt[4] el largo y desde pkt[5] el texto con el XOR de 3 bytes
// (0x6c82e4). Con un largo de más de 100, el handler no hace nada y el texto queda vacío.
struct Message
{
    std::uint8_t index = 0;
    std::uint8_t length = 0;
    std::string text;
};

// 0D (0x4dd540): el texto desde pkt[5] hasta el primer '\0', cortado en pkt[0x68].
struct Notice
{
    std::string text;
};

[[nodiscard]] Core::Expected<RoomAnswer, ProtocolError> ReadRoomAnswer(const Network::Packet& packet);
[[nodiscard]] Core::Expected<InviteResult, ProtocolError> ReadInviteResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<JoinResult, ProtocolError> ReadJoinResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Member, ProtocolError> ReadMember(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Members, ProtocolError> ReadMembers(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Message, ProtocolError> ReadMessage(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Notice, ProtocolError> ReadNotice(const Network::Packet& packet);
// 0x4e6240 y sus handlers leen cada paquete por su lugar sin mirar su largo: con uno más corto que lo que leen (un
// nombre o un texto sin su '\0' adentro, o menos bytes que los del largo del 04), el original sigue en el búfer de
// recepción (Q-C1011). TooShort con esos; con uno que se lee entero o con otro opcode, nada. Los textos que terminan
// adentro del paquete se leen aunque sea más corto que el lugar fijo (el 0D de largo 5 + texto + 1).
[[nodiscard]] Core::Expected<void, ProtocolError> CheckRoomPacket(const Network::Packet& packet);

// 0x4c1690 y 0x4dd3d0: cada byte con FC CF AB (0x6c82e4) según su lugar % 3. Es su propia inversa.
void FlipChatText(std::vector<std::byte>& bytes);

// 0x49d0b0 con el 3 (el chat con el amigo elegido): C1 0D CA y el nombre, con el XOR32.
[[nodiscard]] Outgoing WriteRoomRequest(std::string_view name);
// 0x4944c0 con el 10 y 2: C1 14 CB, el nombre del invitado, un 0, la sala (u16) y la ventana (u32), con el XOR32.
[[nodiscard]] Outgoing WriteInvite(std::string_view name, std::uint16_t room, std::uint32_t window);
// 0x493530, por la conexión de la sala: C1 10 00, un 0, la sala (u16) y el ticket (_itoa en base 10 a 10 bytes en cero,
// con el XOR de 3 bytes), con el XOR32.
[[nodiscard]] Outgoing WriteJoin(std::uint16_t room, std::uint32_t ticket);
// 0x4944c0 con el 7: C1 04, un 0, el largo y el texto con el XOR de 3 bytes, con el XOR32.
[[nodiscard]] Outgoing WriteMessage(std::string_view text);
// 0x493a70, al soltar la ventana: C1 03 01, que sale como está (no tiene bytes después del opcode).
[[nodiscard]] Outgoing WriteLeave();
// 0x4a6550, cada 15 s (el timer 1002 de 0x48e470): C1 03 05, que sale como está.
[[nodiscard]] Outgoing WritePing();
} // namespace Client::Protocol::FriendChat
