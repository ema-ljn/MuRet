#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/CharacterSelectResult.h" // IWYU pragma: export
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <span>

// Entrar al mundo con el personaje elegido (F3 03, C->S) y leer su respuesta (S->C).
// Contrato: Client/docs/research/04-protocolo.md, "La selección de personaje (F3)", y el plan de la
// CL6 (CL6.3, "Contrato").
namespace Client::Protocol::Character
{
// C1 0E F3 03 y los 10 bytes del nombre como llegaron en la lista, con el XOR32 y en claro
// (0x6362b2..0x63638d, en el primer paso de la escena 5).
[[nodiscard]] Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name);

// Recibe el C1 rearmado de Network, sin acreditar que haya llegado cifrado. Lee hasta pkt[0x33]
// en little-endian e ignora el padding 0x26..0x27 y los bytes sobrantes (04, "La entrada al mundo",
// y 07, Q-C144). TooShort si faltan campos; WrongPacket si cambia tipo, opcode o subcódigo.
[[nodiscard]] Core::Expected<CharacterSelectResult, ProtocolError>
ReadCharacterSelectResult(const Network::Packet& packet);
} // namespace Client::Protocol::Character
