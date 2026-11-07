#pragma once

#include <cstddef>
#include <cstdint>

// El opcode y los subcódigos de la selección de personaje, y los tamaños de sus campos.
// Contrato: Client/docs/research/04-protocolo.md, "La selección de personaje (F3)": 0x4e2f93 toma
// el subcódigo de pkt[3] en un C1 y de pkt[4] en un C2, y lo que manda el cliente sale con el XOR32
// desde el byte 3.
namespace Client::Protocol::Character
{
inline constexpr std::uint8_t CharacterOpcode = 0xF3;
inline constexpr std::uint8_t ListSubcode = 0x00;   // pedir la lista, y la lista (0x4c20a0)
inline constexpr std::uint8_t CreateSubcode = 0x01; // crear, y su respuesta (CL6.5)
inline constexpr std::uint8_t DeleteSubcode = 0x02; // borrar, y su respuesta (CL6.5)
inline constexpr std::uint8_t SelectSubcode = 0x03; // entrar al mundo, y su respuesta (0x4c3940, CL7)
// 04-protocolo, "El F3 10, el inventario": la lista de ítems, no la apariencia de selección.
inline constexpr std::uint8_t InventoryListSubcode = 0x10;

// El nombre de un personaje: 10 bytes, sin terminador si los llena.
inline constexpr std::size_t NameSize = 10;
// El equipo de una entrada de la lista, que lee 0x5139f0.
inline constexpr std::size_t EquipmentSize = 17;
// Una entrada de la lista.
inline constexpr std::size_t EntrySize = 0x22;
} // namespace Client::Protocol::Character
