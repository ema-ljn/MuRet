#pragma once

#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <span>

// Entrar al mundo con el personaje elegido (F3 03, C->S). Su respuesta (0x4c3940: el personaje y
// su mapa) es de la CL7.
// Contrato: Client/docs/research/04-protocolo.md, "La selección de personaje (F3)", y el plan de la
// CL6 (CL6.3, "Contrato").
namespace Client::Protocol::Character
{
// C1 0E F3 03 y los 10 bytes del nombre como llegaron en la lista, con el XOR32 y en claro
// (0x6362b2..0x63638d, en el primer paso de la escena 5).
[[nodiscard]] Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name);
} // namespace Client::Protocol::Character
