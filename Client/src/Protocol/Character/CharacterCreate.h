#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Crear un personaje: el pedido (F3 01, C->S) y su respuesta (F3 01, S->C).
// Contrato: Client/docs/research/04-protocolo.md, "La selección de personaje (F3)", y el plan de la
// CL6 (CL6.5, "Contrato").
namespace Client::Protocol::Character
{
// C1 0F F3 01, los 10 bytes del nombre y un byte con la clase de la vista previa por 16 más su piel,
// con el XOR32 y en claro (0x63d990). El nombre va hasta su primer cero y con ceros hasta 10
// (NameUpToItsEnd), como lo copia el original del campo de la ventana. La piel (+0x1d5 de la vista previa)
// nadie la escribe y va en 0.
[[nodiscard]] Outgoing WriteCharacterCreate(std::span<const std::byte, NameSize> name, std::uint8_t characterClass);

// pkt[4] de la respuesta (0x4c2290): 1, creado; 0 abre el aviso 0x36 (el texto 1716) y 2 el 0x37
// (el 396), y los dos vuelven a abrir la ventana de crear; otro no hace nada.
inline constexpr std::uint8_t CharacterCreated = 1;
inline constexpr std::uint8_t CreateRefused = 0;
inline constexpr std::uint8_t CreateRefusedOther = 2;

// Con CharacterCreated, el nuevo: el lugar (pkt[0xf]), el nombre (pkt[5..14]), el nivel (un word en
// pkt[0x10..0x11]) y la clase (pkt[0x12], como el [0xf] de una entrada de la lista:
// CharacterClassOf). Con otro resultado, solo el resultado.
struct CharacterCreateResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
    std::array<std::byte, NameSize> name{};
    std::uint16_t level = 0;
    std::uint8_t classByte = 0;
};

// La respuesta: C1, F3 01 y pkt[4]; con CharacterCreated, el resto hasta pkt[0x12]. TooShort si falta
// algo: 0x4c2290 no mira el largo y leería fuera.
[[nodiscard]] Core::Expected<CharacterCreateResult, ProtocolError>
ReadCharacterCreateResult(const Network::Packet& packet);
} // namespace Client::Protocol::Character
