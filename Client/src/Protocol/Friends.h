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

// Los amigos (CL9 hito 9; 04, "Los amigos"): lo que llega (C0 en un C2, C1..C4 en un C1; 0x4dc010..0x4dc720) y lo que
// manda la ventana de amigos (0x48ee60, 0x4a3030, 0x4a2be0, 0x49d0b0 y 0x4a3b80). La reconstrucción del GameServer
// 0.97 no tiene amigos: el oráculo es el ASM del cliente (*estático*).
namespace Client::Protocol::Friends
{
inline constexpr std::uint8_t FriendListOpcode = 0xc0;
inline constexpr std::uint8_t FriendAddOpcode = 0xc1;
inline constexpr std::uint8_t FriendRequestOpcode = 0xc2;
inline constexpr std::uint8_t FriendDeleteOpcode = 0xc3;
inline constexpr std::uint8_t FriendStateOpcode = 0xc4;
// Los nombres van en 10 bytes; el cliente los copia con strncpy a un buffer de 11 en cero.
inline constexpr std::size_t FriendNameSize = 10;
// C0: cada amigo, el nombre y el estado.
inline constexpr std::size_t FriendRowSize = 11;

struct FriendRow
{
    std::string name;
    std::uint8_t state = 0;
    friend bool operator==(const FriendRow&, const FriendRow&) = default;
};

// C2 C0 (0x4dc010): pkt[4] las cartas, pkt[5] su tope (0x5767970), pkt[6] cuántos amigos y desde pkt[7] los amigos de
// 11 bytes. Los tres, sin signo.
struct FriendList
{
    std::uint8_t letters = 0;
    std::uint8_t maxLetters = 0;
    std::vector<FriendRow> friends;
};

// C1 C1 (0x4dc3d0): pkt[3] el resultado, pkt[4..13] el nombre y pkt[0xe] el estado.
struct FriendAddResult
{
    std::uint8_t result = 0;
    std::string name;
    std::uint8_t state = 0;
};

// C1 C2 (0x4dc5b0): pkt[3..12], el que pide ser amigo.
struct FriendRequest
{
    std::string name;
};

// C1 C3 (0x4dc690): pkt[3] el resultado y pkt[4..13] el nombre.
struct FriendDeleteResult
{
    std::uint8_t result = 0;
    std::string name;
};

// C1 C4 (0x4dc720): pkt[3..12] el nombre y pkt[0xd] el estado.
struct FriendState
{
    std::string name;
    std::uint8_t state = 0;
};

[[nodiscard]] Core::Expected<FriendList, ProtocolError> ReadFriendList(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendAddResult, ProtocolError> ReadFriendAddResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendRequest, ProtocolError> ReadFriendRequest(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendDeleteResult, ProtocolError> ReadFriendDeleteResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendState, ProtocolError> ReadFriendState(const Network::Packet& packet);

// 0x48ee60, el caso 3 de la fábrica con +0xe8 del administrador en 0 (sin la lista): C1 03 C0 en claro, sin el XOR32.
[[nodiscard]] Outgoing WriteFriendListRequest();
// 0x49d0b0 con el 11 (el nombre de la caja de agregar): C1 0D C1 y el nombre, con el XOR32 desde el byte 3.
[[nodiscard]] Outgoing WriteFriendAdd(std::string_view name);
// 0x4a3b80 con dueño −1 (0x4a3bde y 0x4a3fc8): C1 0E C2, 1 (sí) o 0 (no) y el nombre del que pidió, con el XOR32.
[[nodiscard]] Outgoing WriteFriendAnswer(std::uint8_t answer, std::string_view name);
// 0x49d0b0 con el 12 y 1 (borrar el elegido): C1 0D C3 y el nombre, con el XOR32.
[[nodiscard]] Outgoing WriteFriendDelete(std::string_view name);
// 0x4a2be0 (el 12 con 1) y 0x4a3030 (el botón del estado): C1 04 C4 y 0 (fuera de línea) o 1, con el XOR32.
[[nodiscard]] Outgoing WriteFriendState(std::uint8_t state);
} // namespace Client::Protocol::Friends
