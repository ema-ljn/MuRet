#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// La lista de personajes de la cuenta: pedirla (F3 00, C->S) y leerla (F3 00, S->C).
// Contrato: Client/docs/research/04-protocolo.md, "La selección de personaje (F3)", y el plan de la
// CL6 (CL6.3, "Contrato").
namespace Client::Protocol::Character
{
// C1 04 F3 00, sin cuerpo, con el XOR32 y en claro (0x634e0e, en el mismo paso en que 0x634c60 pasa
// a la escena 4).
[[nodiscard]] Outgoing WriteCharacterListRequest();

// Una entrada de 0x22 bytes, como la lee 0x4c20a0. El [0xb] no se lee.
struct CharacterEntry
{
    std::uint8_t slot = 0;                            // [0], de 0 a 4 (MaxCharacterSlot)
    std::array<std::byte, NameSize> name{};           // [1..10], sin terminador si lo llena
    std::uint16_t level = 0;                          // [0xc..0xd], un word (inferido: el nivel)
    std::uint8_t flags = 0;                           // [0xe], a +0x1d8: con 0x1, bloqueado
    std::uint8_t classByte = 0;                       // [0xf]: CharacterClassOf
    std::array<std::byte, EquipmentSize> equipment{}; // [0x10..0x20], a 0x5139f0
    std::uint8_t guildStatus = 0;                     // [0x21], a +0x368 (inferido: el gremio)
    // No vino en la lista: lo armó la respuesta de crear (0x4c2290 con 0x517270), sin equipo
    // (Q-C103). `equipment` no cuenta.
    bool withoutEquipment = false;
};

// El lugar más alto de la selección: cinco, del 0 al 4 (0x4c20a0).
inline constexpr std::uint8_t MaxCharacterSlot = 4;

// La bandera de las banderas de una entrada que bloquea al personaje: con ella, entrar muestra el
// aviso 0x84 (0x6342c0).
inline constexpr std::uint8_t LockedCharacterFlag = 0x01;

[[nodiscard]] constexpr bool IsLocked(const CharacterEntry& entry)
{
    return (entry.flags & LockedCharacterFlag) != 0;
}

struct CharacterList
{
    std::uint8_t creatableClasses = 0;      // pkt[4], a 0x622b20: lo lee la ventana de crear (inferido)
    std::uint8_t notice = 0;                // pkt[5]: los dos números del texto 1240
    std::vector<CharacterEntry> characters; // en el orden del paquete
};

// La lista: C1, F3 00, pkt[4] y pkt[5], la cantidad en pkt[6] y una entrada de 0x22 bytes por
// personaje desde pkt[7]. TooShort si la cantidad dice más entradas que las que entran en el
// paquete: 0x4c20a0 no mira el largo y leería fuera (diferencia nombrada). Lo que sobra detrás de
// la última no se lee. Una entrada con un lugar de más de MaxCharacterSlot se descarta: el original
// la deja sin posición y no se puede elegir (Q-C101).
[[nodiscard]] Core::Expected<CharacterList, ProtocolError> ReadCharacterList(const Network::Packet& packet);

// La clase de una entrada, de su [0xf]: ((b & 0x10) | (b >> 4)) >> 1, como se la pasa 0x4c20a0 a
// 0x517270. Quedan la clase en los bits 0 a 2 (los 5 a 7 del byte) y la segunda clase en el 3 (el
// 4 del byte); los bits 0 a 3 del byte no cuentan.
[[nodiscard]] constexpr std::uint8_t CharacterClassOf(std::uint8_t classByte)
{
    return static_cast<std::uint8_t>(((classByte & 0x10) | (classByte >> 4)) >> 1);
}
} // namespace Client::Protocol::Character
