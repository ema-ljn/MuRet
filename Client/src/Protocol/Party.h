#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// 04 § La party: del 40 al 44 en los dos sentidos (08 P-C92 en la CL8 hito 6, el resto en la CL9 hito 7). Los lectores
// son los campos que leen los casos del dispatcher 0x4e2b10 (0x4e3f41..0x4e41ba) y 0x4d67e0; los escritores, lo que
// arman 0x6599a0(4) (42), 0x406050 y 0x56c440 (40), 0x623e70 (41) y 0x5e3c80 (43).
namespace Client::Protocol::Party
{
inline constexpr std::uint8_t RequestOpcode = 0x40;
inline constexpr std::uint8_t RequestResultOpcode = 0x41;
inline constexpr std::uint8_t PartyListOpcode = 0x42;
inline constexpr std::uint8_t LeaveOpcode = 0x43;
inline constexpr std::uint8_t LifeOpcode = 0x44;
// 0x4d67e0: cada miembro ocupa 0x18 bytes desde pkt[5].
inline constexpr std::size_t MemberWireSize = 0x18;

using Name = std::array<std::byte, Character::NameSize>;

// 0x659aa6..0x659b94: C1 03 42 armado en la pila y mandado con send si el socket de 0x6c82a0 (+0xc) no es −1. No
// pasa por el XOR32 ni por el cifrado del C3: va en claro.
[[nodiscard]] Outgoing WritePartyListRequest();

// El 40 del GS (0x4e3f41): C1 05 40 y la clave del que invita en big-endian (pkt[3] << 8 | pkt[4]).
struct PartyRequest
{
    std::uint16_t key = 0;
};

[[nodiscard]] Core::Expected<PartyRequest, ProtocolError> ReadPartyRequest(const Network::Packet& packet);

// El 41 del GS (0x4e3f6f): C1 04 41 y el resultado (de 0 a 5 un texto; más, ninguno).
struct PartyRequestResult
{
    std::uint8_t result = 0;
};

[[nodiscard]] Core::Expected<PartyRequestResult, ProtocolError> ReadPartyRequestResult(const Network::Packet& packet);

// Un miembro del 42, 0x18 bytes: el nombre (10), el número (+0xa), el mapa (+0xb), la celda (+0xc y +0xd), dos bytes
// que 0x4d67e0 no copia, la vida (+0x10) y la vida máxima (+0x14), los dos DWORD en little-endian.
struct PartyMember
{
    Name name{};
    std::uint8_t number = 0;
    std::uint8_t map = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint32_t life = 0;
    std::uint32_t maxLife = 0;
};

// El 42 del GS (0x4d67e0): C1 ?? 42, el resultado en pkt[3] (que nadie lee), la cuenta en pkt[4] y la cuenta de
// miembros desde pkt[5]. 0x4d67e0 no le pone tope: el lector los lee todos, y quien los aplica corta pasados los cinco
// lugares de 0x77f3cd0.
struct PartyList
{
    std::uint8_t result = 0;
    std::vector<PartyMember> members;
};

[[nodiscard]] Core::Expected<PartyList, ProtocolError> ReadPartyList(const Network::Packet& packet);

// El 43 del GS (0x4e409b): C1 03 43, sin campos.
struct PartyLeft
{
};

[[nodiscard]] Core::Expected<PartyLeft, ProtocolError> ReadPartyLeft(const Network::Packet& packet);

// El 44 del GS (0x4e4174): C1 ?? 44, la cuenta en pkt[3] y un byte por miembro desde pkt[4]: el lugar en el
// nibble alto y la vida en décimos en el bajo.
struct PartyLife
{
    std::vector<std::uint8_t> entries;
};

[[nodiscard]] Core::Expected<PartyLife, ProtocolError> ReadPartyLife(const Network::Packet& packet);

// El 40 del cliente (0x406050 desde 0x40739b; 0x56c440 con /party): C1 05 40 y la clave del otro en big-endian, con el
// XOR32 y en C3.
[[nodiscard]] Outgoing WritePartyRequest(std::uint16_t key);

// El 41 del cliente (el diálogo 0x77 de 0x623e70: sí 0x62bb1d, no 0x62cd52): C1 06 41, la respuesta (1 sí, 0 no) y
// la clave de 0x78aba4c en big-endian, con el XOR32 y en C3.
[[nodiscard]] Outgoing WritePartyRequestResult(std::uint8_t accepted, std::uint16_t key);

// El 43 del cliente (0x5e3c80, 0x5e3ded..0x5e407b): C1 04 43 y el número del miembro (+0xb de su lugar en 0x77f3cd0),
// con el XOR32 y en claro.
[[nodiscard]] Outgoing WritePartyLeave(std::uint8_t number);
} // namespace Client::Protocol::Party
