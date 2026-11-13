#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

// 04 § Las órdenes del cuervo (CL9 hito 4): el A7 y el A8 que llegan, la parte pura de los handlers de 0x4e2b10, y el
// A7 que sale. Lo que hacen con el cuervo es de Scenes (PetOrders). El GameServer 0.97 no los manda ni los lee (08,
// P-C262): el oráculo es el cliente. WrongPacket con otro tipo u opcode y TooShort si faltan campos; el original no
// mira el largo.
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

// A7, C->S: C1 07 A7 [mascota] [orden] [clave H L]. 0x416a20 lo manda cuando el blanco de +0x3b0 o el dueño tienen
// 0x4000 en un ataque (los estados 2, 5 y 6): la mascota es el +0x444 del cuervo, que 0x4167b0 pone en 0 y nadie más
// escribe, la orden 0x78 y la clave 0xffff (0x416b05..0x417095 y 0x417113..0x4176a9). Con el XOR32 desde pkt[3] y por
// send() directo, sin cifrar como C3 (Xor32C1).
struct PetCommand
{
    std::uint8_t pet = 0;
    std::uint8_t command = 0;
    std::uint16_t key = 0;
};

inline constexpr std::uint8_t RavenPet = 0;
// La orden del aviso de 0x416a20 (0x416c9a): la de la skill 0x78, la primera de las del cuervo.
inline constexpr std::uint8_t StopTheAttackCommand = 0x78;
inline constexpr std::uint16_t NoTargetKey = 0xffff;

[[nodiscard]] Core::Expected<PetOrder, ProtocolError> ReadPetOrder(const Network::Packet& packet);
[[nodiscard]] Core::Expected<PetAttack, ProtocolError> ReadPetAttack(const Network::Packet& packet);
[[nodiscard]] Outgoing WritePetCommand(const PetCommand& command);
} // namespace Client::Protocol::Pet
