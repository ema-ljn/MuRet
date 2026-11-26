#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

// The raven's orders (04, "Las órdenes del cuervo"): the A7 and the A8 that arrive, the pure part of the handlers of
// 0x4e2b10, and the A7 that leaves. What they do with the raven is Scenes' (PetOrders). The GameServer 0.97 neither
// sends nor reads them (08, P-C262): the oracle is the client. WrongPacket with another type or opcode and TooShort
// without a field; the original does not look at the length.
namespace Client::Protocol::Pets
{
// 0x4e4c0a: C1 07 A7 [?] [order] [key H L]. It does not read pkt[3].
struct PetOrder
{
    std::uint8_t order = 0;
    std::uint16_t key = 0;
};

// The order that follows a target (0x416534).
inline constexpr std::uint8_t FollowTargetOrder = 3;

// 0x4e4c42: C1 09 A8 [?] [type] [owner H L] [target H L]. It does not read pkt[3].
struct PetAttack
{
    std::uint8_t kind = 0;
    std::uint16_t owner = 0;
    std::uint16_t target = 0;
};

// A7, C->S: C1 07 A7 [pet] [order] [key H L]. 0x416a20 sends it when the target of +0x3b0 or the owner have 0x4000 in
// an attack (states 2, 5 and 6): the pet is the raven's +0x444, which 0x4167b0 sets to 0 and nobody else writes, the
// order 0x78 and the key 0xffff (0x416b05..0x417095 and 0x417113..0x4176a9). With the XOR32 from pkt[3] and through a
// direct send(), not encrypted as a C3 (Xor32C1).
struct PetCommand
{
    std::uint8_t pet = 0;
    std::uint8_t command = 0;
    std::uint16_t key = 0;
};

inline constexpr std::uint8_t RavenPet = 0;
// The order of 0x416a20's report (0x416c9a): skill 0x78's, the first of the raven's.
inline constexpr std::uint8_t StopTheAttackCommand = 0x78;
inline constexpr std::uint16_t NoTargetKey = 0xffff;

[[nodiscard]] Core::Expected<PetOrder, Network::ProtocolError> ReadPetOrder(const Network::Packet& packet);
[[nodiscard]] Core::Expected<PetAttack, Network::ProtocolError> ReadPetAttack(const Network::Packet& packet);
[[nodiscard]] Outgoing WritePetCommand(const PetCommand& command);
} // namespace Client::Protocol::Pets
