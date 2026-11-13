#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// 04 § El gremio, la alianza y la guerra (CL9 hito 8): los paquetes del gremio que 0x4e2b10 reparte a 0x4d74b0,
// 0x4d7a20, 0x4d7b60, 0x4d7c30 y 0x4d7ff0, y los que el dispatcher atiende en línea (0x54, 0x5D y 0x66), sin aplicar
// nada; y los que el cliente manda. El original lee sus campos a desplazamientos fijos sin mirar el largo: acá, un
// paquete que no trae los campos que se leen es TooShort, y una lista que dice más entradas que las que trae, también.
// Los bytes sobrantes se ignoran. 0x55 y 0x5C no tienen caso en 0x4e2b10 (van al default): el cliente no los lee.
namespace Client::Protocol::Guild
{
inline constexpr std::uint8_t JoinRequestOpcode = 0x50;
inline constexpr std::uint8_t JoinResultOpcode = 0x51;
inline constexpr std::uint8_t MemberListOpcode = 0x52;
inline constexpr std::uint8_t LeaveResultOpcode = 0x53;
inline constexpr std::uint8_t MasterQuestionOpcode = 0x54;
inline constexpr std::uint8_t CreateOpcode = 0x55;
inline constexpr std::uint8_t CreateResultOpcode = 0x56;
inline constexpr std::uint8_t ViewportGuildDeleteOpcode = 0x5d;
inline constexpr std::uint8_t GuildInfoOpcode = 0x66;

// Los nombres de gremio: 8 bytes sin terminador (el original los copia con dos DWORD y pone el cero él).
inline constexpr std::size_t GuildNameSize = 8;
// Los nombres de personaje: 10 bytes.
inline constexpr std::size_t CharacterNameSize = 10;
// La marca: 32 bytes, dos nibbles por byte (el alto primero).
inline constexpr std::size_t MarkBytes = 32;
// 0x4d7b60: 80 lugares de 0xe bytes en 0x787a5e8, hasta la tabla de los gremios (0x787aa48).
inline constexpr std::size_t MaxMembers = 80;

using GuildName = std::array<std::byte, GuildNameSize>;
using CharacterName = std::array<std::byte, CharacterNameSize>;
using GuildMark = std::array<std::uint8_t, MarkBytes>;

// 50 (0x4d74b0): la clave de quien pide entrar, pkt[3] · 0x100 + pkt[4] (sin el 0x7f).
struct JoinRequest
{
    std::uint16_t key = 0;
};

// 51 (0x4d7a20), 53 (0x4d7c30) y 56 (0x4d7ff0): el resultado de pkt[3].
struct JoinResult
{
    std::uint8_t result = 0;
};
struct LeaveResult
{
    std::uint8_t result = 0;
};
struct CreateResult
{
    std::uint8_t result = 0;
};

// Una entrada del 52: 0xd bytes desde pkt[0x18].
struct MemberEntry
{
    CharacterName name{};
    std::uint8_t number = 0; // +0xa
    std::uint8_t server = 0; // +0xb: el bit 7 prendido, conectado en el servidor de los 7 de abajo
    std::uint8_t status = 0; // +0xc: el puesto (0x80 el maestro)
};

// 52 (0x4d7b60), un C2: pkt[5] la cuenta, el DWORD de pkt[8] el puntaje, pkt[0xd..0x14] un nombre de gremio y las
// entradas desde pkt[0x18]. pkt[4] (el resultado del GS 0.97) no se lee.
struct MemberList
{
    std::uint8_t count = 0;
    std::int32_t score = 0;
    GuildName name{};
    std::vector<MemberEntry> members;
};

// 54 (0x4e4279): el dispatcher abre el panel 0x40000 sin leer el paquete.
struct MasterQuestion
{
};

// 5D (0x4e42f0): la clave, (pkt[3] & 0x7f) · 0x100 + pkt[4].
struct ViewportGuildDelete
{
    std::uint16_t key = 0;
};

// 66 (0x4e42c7 → 0x447b90): la clave del DWORD de pkt[4], los dos nombres de pkt[9..0x10] y pkt[0x11..0x18] y la
// marca de pkt[0x19..0x38].
struct GuildInfo
{
    std::int32_t key = 0;
    GuildName unionName{};
    GuildName guildName{};
    GuildMark mark{};
};

using GuildPacket = std::variant<JoinRequest, JoinResult, MemberList, LeaveResult, MasterQuestion, CreateResult,
                                 ViewportGuildDelete, GuildInfo>;

// El paquete del gremio que es, por su opcode. 52 es C2; los demás, C1. Otro opcode u otro tipo: WrongPacket.
[[nodiscard]] Core::Expected<GuildPacket, ProtocolError> ReadGuildPacket(const Network::Packet& packet);

// 0x4d6b60 y 0x4d754d..: C1 06 51 [sí] [clave alta] [clave baja], con el XOR32 y en claro. Con 8 (Confirmar) el 1; con
// cualquier otro motivo, y con el diálogo ocupado al llegar el 50, el 0.
[[nodiscard]] Outgoing WriteJoinAnswer(bool accept, std::uint16_t key);

// 0x4d7d95, 0x6599a0(0x10) y 0x655d70: C1 03 52 en claro, el pedido de la lista.
[[nodiscard]] Outgoing WriteMemberListRequest();

// 0x6570b0 y 0x655d70: C1 04 54 [respuesta], con el XOR32 y en claro. Cerrar el panel 0x40000 manda el 0.
[[nodiscard]] Outgoing WriteMasterAnswer(std::uint8_t answer);

// 0x6552b0: C1 2B 55, el nombre del gremio (8 bytes) y la marca (32), con el XOR32 y en claro.
[[nodiscard]] Outgoing WriteCreate(const GuildName& name, const GuildMark& mark);
} // namespace Client::Protocol::Guild
