#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

// Changing maps (04, "Los cambios de mapa"): the warp and the revival. Network hands over an incoming C3 already
// decrypted as a C1; the scene decides when to send.
namespace Client::Protocol::Movement
{
struct Teleport
{
    std::uint8_t gate;
    std::uint8_t map;
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t direction;
};

struct Regen
{
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t map;
    std::uint8_t direction;
    std::uint16_t life;
    std::uint16_t mana;
    std::uint16_t bp;
    std::uint32_t experience;
    std::uint32_t zen;
};

// The raw direction, not turned into a yaw.
[[nodiscard]] Core::Expected<Teleport, Network::ProtocolError> ReadTeleport(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Regen, Network::ProtocolError> ReadRegen(const Network::Packet& packet);

// 0x599ef0: C1 06 1C gate 00 00, with the XOR32 and Delivery::Encrypted (C3).
[[nodiscard]] Outgoing WriteGateRequest(std::uint8_t gate);

// 0x4c6e10: C1 04 F3 12, with the XOR32 and Delivery::Clear.
[[nodiscard]] Outgoing WriteTeleportDone();

// 0x4c6e32: unencrypted, C1 06 F1 03 00 1C, with the XOR32 and delivered as a C3.
[[nodiscard]] Outgoing WriteRejectedTeleport();

// 04, "El clima y Crywolf", 0x4c421a/0x4c785d: C1 04 BD 00, with the XOR32 and Delivery::Clear.
[[nodiscard]] Outgoing WriteCrywolfState();

// 06, "Los objetos de Castle Siege", 0x42a7d3..0x42a99e (0x42a760, on loading map 30): C1 04 B9 02, with the XOR32 and
// Delivery::Clear (sent directly, with the GameServer's socket open).
[[nodiscard]] Outgoing WriteCastleSiegeState();
} // namespace Client::Protocol::Movement
