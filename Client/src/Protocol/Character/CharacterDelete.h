#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Borrar un personaje: el pedido (F3 02, C->S) y su respuesta (F3 02, S->C).
// Contrato: Client/docs/research/04-protocolo.md, "La selección de personaje (F3)", y el plan de la
// CL6 (CL6.5, "Contrato").
namespace Client::Protocol::Character
{
// Los bytes del campo de la ventana de borrar que van en el pedido: en otras versiones, un número
// personal de la cuenta. Se tratan como la clave: nunca en el log.
inline constexpr std::size_t DeleteCodeSize = 10;

// C1 18 F3 02, los 10 bytes del nombre del elegido y los 10 del campo de la ventana, como vienen,
// con el XOR32 y en claro (0x643740). El nombre va hasta su primer cero y con ceros hasta 10
// (NameUpToItsEnd), como el original.
[[nodiscard]] Outgoing WriteCharacterDelete(std::span<const std::byte, NameSize> name,
                                            std::span<const std::byte, DeleteCodeSize> code);

// pkt[4] de la respuesta, que 0x4e2b10 lee en línea y solo abre un aviso: 1, borrado (el 0x60, que
// borra al elegido); 0, el 0x92 (el texto 1654); 3, el 0x62 (el 439); otro, el 0x89 (el 401).
inline constexpr std::uint8_t CharacterDeleted = 1;

// La respuesta: C1, F3 02 y pkt[4]. TooShort sin pkt[4].
[[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> ReadCharacterDeleteResult(const Network::Packet& packet);
} // namespace Client::Protocol::Character
