#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h" // IWYU pragma: export
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <span>

// Entering the world with the chosen character, F3 03: the request and its reply (04, "La selección de personaje
// (F3)").
namespace Client::Protocol::FrontEnd
{
// 0x6362b2..0x63638d, in scene 5's first step: C1 0E F3 03 and the name as the list brought it, with the XOR32 and in
// clear.
[[nodiscard]] Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name);

// Up to pkt[0x33], little endian, past the padding at 0x26..0x27 (04, "La entrada al mundo", and Q-C144). It does not
// tell whether the reply came encrypted.
[[nodiscard]] Core::Expected<CharacterSelectResult, Network::ProtocolError>
ReadCharacterSelectResult(const Network::Packet& packet);
} // namespace Client::Protocol::FrontEnd
