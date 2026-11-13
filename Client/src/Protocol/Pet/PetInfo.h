#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

namespace Client::Network
{
class Packet;
}

// El pedido de la información de una mascota (A9, C->S): qué mascota, en qué lugar y en qué
// casilla. Contrato: Client/docs/research/04-protocolo.md, "A9, C->S: pedir la información de una
// mascota" (Q-C16, de la CL4.R), y el plan de la CL4 (CL4.3). Qué es cada valor sale del handler
// de la respuesta (0x421db0) y de OpenMU: *inferido* alto.
//
// main.exe lo manda al ConnectServer, que no lo usa: la escena de login conecta y enseguida le
// pone a su personaje un equipo con el cuervo oscuro, y 0x415be0 lo pide por el socket que esté
// abierto, que es el del CS. MuRet lo manda igual, al abrir el CS (la ServerSession), porque es lo
// que el CS del original ve primero. El que sale ya en el juego, el del caballo oscuro desde la UI
// de las mascotas (0x424230), es de la CL8.
namespace Client::Protocol::Pet
{
inline constexpr std::uint8_t PetInfoOpcode = 0xA9;

// La mascota.
inline constexpr std::uint8_t DarkRavenPet = 0; // el cuervo oscuro, el ítem 13, 5
inline constexpr std::uint8_t DarkHorsePet = 1; // el caballo oscuro, el ítem 13, 4
// El lugar.
inline constexpr std::uint8_t InventoryPlace = 0;
// La casilla del inventario.
inline constexpr std::uint8_t LeftHandSlot = 1; // donde va el cuervo
inline constexpr std::uint8_t PetSlot = 8;      // donde va el caballo

struct PetInfoRequest
{
    std::uint8_t pet;
    std::uint8_t place;
    std::uint8_t slot;
};

// El que manda 0x415be0 al abrir el ConnectServer: el cuervo oscuro de la mano izquierda del
// personaje de la escena de login. Siempre el mismo, sea cual sea el mapa o el personaje.
inline constexpr PetInfoRequest LoginScenePetInfoRequest{DarkRavenPet, InventoryPlace, LeftHandSlot};

// A9: C1 06 A9, la mascota, el lugar y la casilla, con el XOR32 y en claro, como sale al
// ConnectServer (Xor32C1).
[[nodiscard]] Outgoing WritePetInfoRequest(const PetInfoRequest& request);

// Lo que pasan 0x4e4ca1..0x4e4cb8 a 0x421db0 (CL9 hito 4): pkt[3] a pkt[6] y el DWORD de pkt[8], en little endian.
// El byte de pkt[7] no lo lee nadie.
struct PetInfoReply
{
    std::uint8_t pet = 0;         // pkt[3]: 0 el cuervo, 1 el caballo
    std::uint8_t kind = 0;        // pkt[4]: 0, 0xfe y 0xff hacen algo más con la mascota del héroe (06, "El A9")
    std::uint8_t slot = 0;        // pkt[5]: la casilla, 1 el cuervo y 8 el caballo
    std::uint8_t level = 0;       // pkt[6]
    std::uint32_t experience = 0; // pkt[8]
};

// El A9 que llega, C1 y sin mirar el cifrado ni el largo en el original: MuRet da TooShort sin los 12 bytes.
[[nodiscard]] Core::Expected<PetInfoReply, ProtocolError> ReadPetInfoReply(const Network::Packet& packet);
} // namespace Client::Protocol::Pet
