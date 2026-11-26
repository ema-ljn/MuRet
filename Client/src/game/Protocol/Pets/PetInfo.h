#pragma once

#include "Core/Expected.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

namespace Client::Network
{
class Packet;
}

// The request for a pet's information (A9, C->S): which pet, in which place and in which slot (04, "A9, C->S: pedir la
// información de una mascota", Q-C16). What each value is comes from the reply's handler (0x421db0) and from OpenMU:
// *inferred*, high.
//
// main.exe sends it to the ConnectServer, which does not use it: the login scene connects and right away gives its
// character equipment with the dark raven, and 0x415be0 asks for it through whichever socket is open, the CS's. MuRet
// sends it too, on opening the CS (the ServerSession), because it is what the original's CS sees first. The one sent
// in the game, the dark horse's from the pets UI (0x424230), is CL8's.
namespace Client::Protocol::Pets
{
inline constexpr std::uint8_t DarkRavenPet = 0; // item 13, 5
inline constexpr std::uint8_t DarkHorsePet = 1; // item 13, 4
inline constexpr std::uint8_t InventoryPlace = 0;
inline constexpr std::uint8_t LeftHandSlot = 1; // where the raven goes
inline constexpr std::uint8_t PetSlot = 8;      // where the horse goes

struct PetInfoRequest
{
    std::uint8_t pet;
    std::uint8_t place;
    std::uint8_t slot;
};

// What 0x415be0 sends on opening the ConnectServer: the dark raven in the left hand of the login scene's character.
// Always the same, whatever the map or the character.
inline constexpr PetInfoRequest LoginScenePetInfoRequest{DarkRavenPet, InventoryPlace, LeftHandSlot};

// A9: C1 06 A9, the pet, the place and the slot, with the XOR32 and in the clear, as it goes to the ConnectServer
// (Xor32C1).
[[nodiscard]] Outgoing WritePetInfoRequest(const PetInfoRequest& request);

// What 0x4e4ca1..0x4e4cb8 pass to 0x421db0: pkt[3] to pkt[6] and the DWORD at pkt[8], little-endian. Nobody reads
// pkt[7].
struct PetInfoReply
{
    std::uint8_t pet = 0;         // pkt[3]: 0 the raven, 1 the horse
    std::uint8_t kind = 0;        // pkt[4]: 0, 0xfe and 0xff do more with the hero's pet (06, "El A9")
    std::uint8_t slot = 0;        // pkt[5]: 1 the raven and 8 the horse
    std::uint8_t level = 0;       // pkt[6]
    std::uint32_t experience = 0; // pkt[8]
};

// The A9 that arrives, a C1; the original looks at neither the encryption nor the length: MuRet gives TooShort
// without the 12 bytes.
[[nodiscard]] Core::Expected<PetInfoReply, Network::ProtocolError> ReadPetInfoReply(const Network::Packet& packet);
} // namespace Client::Protocol::Pets
