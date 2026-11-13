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

// 04 § El gremio, la alianza y la guerra (CL9 hito 8): los paquetes que 0x4e2b10 reparte a 0x4d74b0, 0x4d7a20,
// 0x4d7b60, 0x4d7c30, 0x4d7ff0, 0x4d8150, 0x4d8260, 0x4d8870, 0x4d9260, 0x4d9810, 0x6565e0, 0x4d9940, 0x4d9e50 y
// 0x4d9b80, y los que el dispatcher atiende en línea (0x54, 0x5D, 0x61, 0x64, 0x66 y 0x67), sin aplicar nada; y los
// que el cliente manda. El
// original lee sus campos a desplazamientos fijos sin mirar el largo: acá, un paquete que no trae los campos que se
// leen es TooShort, y una lista que dice más entradas que las que trae, también. Los bytes sobrantes se ignoran. 0x55 y
// 0x5C no tienen caso en 0x4e2b10 (van al default): el cliente no los lee.
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
inline constexpr std::uint8_t WarRequestResultOpcode = 0x60;
inline constexpr std::uint8_t WarRequestOpcode = 0x61;
inline constexpr std::uint8_t WarStartOpcode = 0x62;
inline constexpr std::uint8_t WarEndOpcode = 0x63;
inline constexpr std::uint8_t WarScoreOpcode = 0x64;
inline constexpr std::uint8_t ViewportGuildOpcode = 0x65;
inline constexpr std::uint8_t GuildInfoOpcode = 0x66;
inline constexpr std::uint8_t ViewportUnionOpcode = 0x67;
inline constexpr std::uint8_t AssignResultOpcode = 0xe1;
inline constexpr std::uint8_t RelationshipRequestOpcode = 0xe5;
inline constexpr std::uint8_t RelationshipResultOpcode = 0xe6;
inline constexpr std::uint8_t UnionListOpcode = 0xe9;
inline constexpr std::uint8_t UnionListRefreshOpcode = 0xeb;

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

// 51 (0x4d7a20), 53 (0x4d7c30), 56 (0x4d7ff0) y 60 (0x4d8150): el resultado de pkt[3].
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
struct WarRequestResult
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

// 61 (0x4e4382): el nombre del gremio de pkt[3..10] y el tipo de pkt[0xb].
struct WarRequest
{
    GuildName name{};
    std::uint8_t type = 0;
};

// 62 (0x4d8260): el nombre de pkt[3..10], el tipo de pkt[0xb] y el equipo de pkt[0xc].
struct WarStart
{
    GuildName name{};
    std::uint8_t type = 0;
    std::uint8_t team = 0;
};

// 63 (0x4d8870): el resultado de pkt[3]. El nombre de pkt[4..11] no se lee.
struct WarEnd
{
    std::uint8_t result = 0;
};

// 64 (0x4e4400): los dos puntajes de pkt[3] y pkt[4]. pkt[5] no se lee.
struct WarScore
{
    std::uint8_t ours = 0;
    std::uint8_t theirs = 0;
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

// Una entrada del 65: 0xc bytes desde pkt[5] (0x4d9294..0x4d92e9, 0x4d9799). Los tres de después de la clave del
// personaje no se leen.
struct ViewportGuildEntry
{
    // +0: el DWORD de la clave del gremio (0x4d929f).
    std::int32_t guildKey = 0;
    // +4, +5 y +6: lo que va a +0x368 (el puesto), +0x369 y +0x370 (la relación) del personaje.
    std::uint8_t status = 0;
    std::uint8_t byte369 = 0;
    std::uint8_t relation = 0;
    // +7 y +8: la clave del personaje, (+7 & 0x7f) · 0x100 + (+8).
    std::uint16_t key = 0;
};

// 65 (0x4e42af → 0x4d9260), un C2: la cuenta de pkt[4] y sus entradas. *Inferido* el C2: el original lee la cuenta de
// pkt[4] y la primera entrada de pkt[5]; el GameServer 0.97 no lo manda, y el ASM es el oráculo (04).
struct ViewportGuild
{
    std::vector<ViewportGuildEntry> entries;
};

// Una entrada del 67: 0x14 bytes desde pkt[5] (0x4e44ba..0x4e4509). +2, +3 y +0x11..+0x13 no se leen.
struct ViewportUnionEntry
{
    // +0 y +1: la clave del personaje, CONCAT11 sin el 0x7f del 65.
    std::uint16_t key = 0;
    // +4: el DWORD de la clave del gremio.
    std::int32_t guildKey = 0;
    // +8: lo que va a +0x370 del personaje.
    std::uint8_t relation = 0;
    // +9..+0x10: el nombre de la alianza, dos DWORD.
    GuildName unionName{};
};

// 67 (0x4e44aa, en línea), un C2 como el 65: la cuenta de pkt[4] y sus entradas.
struct ViewportUnion
{
    std::vector<ViewportUnionEntry> entries;
};

// E1 (0x4d9810): el tipo de pkt[3] y el resultado de pkt[4].
struct AssignResult
{
    std::uint8_t type = 0;
    std::uint8_t result = 0;
};

// E5 (0x4e4444 → 0x6565e0): pkt[3..6], el tipo de relación, el pedido y la clave de quien pide (alto y bajo).
struct RelationshipRequest
{
    std::uint8_t relation = 0;
    std::uint8_t request = 0;
    std::uint8_t keyHigh = 0;
    std::uint8_t keyLow = 0;
};

// E6 (0x4d9940): el tipo de relación de pkt[3], el pedido de pkt[4], el resultado de pkt[5] y la clave de pkt[6..7].
struct RelationshipResult
{
    std::uint8_t relation = 0;
    std::uint8_t request = 0;
    std::uint8_t result = 0;
    std::uint16_t key = 0;
};

// Una entrada del E9: 0x29 bytes desde pkt[8]: un byte, la marca y el nombre.
struct UnionEntry
{
    std::uint8_t byte0 = 0;
    GuildMark mark{};
    GuildName name{};
};

// E9 (0x4d9e50), un C2: la cuenta de pkt[4], el resultado de pkt[5] y las entradas.
struct UnionList
{
    std::uint8_t count = 0;
    std::uint8_t result = 0;
    std::vector<UnionEntry> entries;
};

// EB (0x4e4486): pkt[3] y, si es 1, pkt[4] (0x4d9b80).
struct UnionListRefresh
{
    std::uint8_t kind = 0;
    std::uint8_t result = 0;
};

using GuildPacket =
    std::variant<JoinRequest, JoinResult, MemberList, LeaveResult, MasterQuestion, CreateResult, ViewportGuildDelete,
                 WarRequestResult, WarRequest, WarStart, WarEnd, WarScore, GuildInfo, AssignResult, RelationshipRequest,
                 RelationshipResult, UnionList, UnionListRefresh, ViewportGuild, ViewportUnion>;

// El paquete del gremio que es, por su opcode. 52, 65, 67 y E9 son C2; los demás, C1. Otro opcode u otro tipo:
// WrongPacket.
[[nodiscard]] Core::Expected<GuildPacket, ProtocolError> ReadGuildPacket(const Network::Packet& packet);

// 0x4d6b60 y 0x4d754d..: C1 06 51 [sí] [clave alta] [clave baja], con el XOR32 y en claro. Con 8 (Confirmar) el 1; con
// cualquier otro motivo, y con el diálogo ocupado al llegar el 50, el 0.
[[nodiscard]] Outgoing WriteJoinAnswer(bool accept, std::uint16_t key);

// 0x4d7d95, 0x6599a0(0x10) y 0x655d70: C1 03 52 en claro, el pedido de la lista.
[[nodiscard]] Outgoing WriteMemberListRequest();

// 0x6570b0 y 0x655d70: C1 04 54 [respuesta], con el XOR32 y en claro. Cerrar el panel 0x40000 manda el 0.
[[nodiscard]] Outgoing WriteMasterAnswer(std::uint8_t answer);

// 0x6552b0: C1 2C 55, un byte en 0 (0x6553bf: el tipo del gremio de las versiones de después, *inferido*), el nombre
// del gremio (8 bytes) y la marca (32), con el XOR32 y en claro. El GS 0.97 lee el nombre en pkt[3] (C1 2B, Q-C1287).
[[nodiscard]] Outgoing WriteCreate(const GuildName& name, const GuildMark& mark);

// 0x6565e0 y 0x653c00: C1 08 E6 [relación] [pedido] [resultado] [clave alta] [clave baja], con el XOR32 y en claro.
[[nodiscard]] Outgoing WriteRelationshipAnswer(std::uint8_t relation, std::uint8_t request, std::uint8_t result,
                                               std::uint8_t keyHigh, std::uint8_t keyLow);

// 0x4d9b80 y 0x651840: C1 03 E9 en claro, el pedido de la lista de la alianza.
[[nodiscard]] Outgoing WriteUnionListRequest();

// 0x4d930f..0x4d9690: C1 08 66 [0] [clave del gremio, DWORD], con el XOR32 y en claro: el 65 pide el 66 de un gremio
// que la tabla no tiene. El 0 es el relleno hasta el DWORD, como el pkt[3] del 66.
[[nodiscard]] Outgoing WriteGuildInfoRequest(std::int32_t guildKey);
} // namespace Client::Protocol::Guild
