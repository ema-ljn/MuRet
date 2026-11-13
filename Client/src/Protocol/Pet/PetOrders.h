#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

// 04 § Las órdenes del cuervo (CL9 hito 4): el A7 y el A8 que llegan, la parte pura de los handlers de 0x4e2b10. Lo
// que hacen con el cuervo es de Scenes (PetOrders). El GameServer 0.97 no los manda (08, P-C262): el oráculo es el
// cliente. WrongPacket con otro tipo u opcode y TooShort si faltan campos; el original no mira el largo.
namespace Client::Protocol::Pet
{
inline constexpr std::uint8_t PetOrderOpcode = 0xA7;
inline constexpr std::uint8_t PetAttackOpcode = 0xA8;

// 0x4e4c0a: C1 07 A7 [?] [orden] [clave H L]. pkt[3] no lo lee.
struct PetOrder
{
    std::uint8_t order = 0;
    std::uint16_t key = 0;
};

// La orden que sigue a un blanco (0x416534).
inline constexpr std::uint8_t FollowTargetOrder = 3;

// 0x4e4c42: C1 09 A8 [?] [tipo] [dueño H L] [blanco H L]. pkt[3] no lo lee.
struct PetAttack
{
    std::uint8_t kind = 0;
    std::uint16_t owner = 0;
    std::uint16_t target = 0;
};

[[nodiscard]] Core::Expected<PetOrder, ProtocolError> ReadPetOrder(const Network::Packet& packet);
[[nodiscard]] Core::Expected<PetAttack, ProtocolError> ReadPetAttack(const Network::Packet& packet);
} // namespace Client::Protocol::Pet
