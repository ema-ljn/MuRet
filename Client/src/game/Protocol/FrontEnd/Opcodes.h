#pragma once

#include <cstddef>

// The sizes of the character select fields (04-protocolo.md, "La selección de personaje (F3)"). The F3 subcodes
// are in Protocol/Opcodes.h: 0x4e2f93 reads the subcode from pkt[3] in a C1 and from pkt[4] in a C2.
namespace Client::Protocol::Character
{
// El nombre de un personaje: 10 bytes, sin terminador si los llena.
inline constexpr std::size_t NameSize = 10;
// El equipo de una entrada de la lista, que lee 0x5139f0.
inline constexpr std::size_t EquipmentSize = 17;
// Una entrada de la lista.
inline constexpr std::size_t EntrySize = 0x22;
} // namespace Client::Protocol::Character
