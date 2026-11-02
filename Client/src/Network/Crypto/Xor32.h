// XOR32, el cifrado encadenado del motor de envío: cada byte se combina con un byte de una clave de
// 32 y con el byte anterior ya cifrado.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Envío: el motor C1/C2 y el XOR32", y el
// plan de la CL2 (CL2.2).
//
// En un C1 o un C2, el byte i (desde el que sigue al opcode) hace
// `valor ^= Xor32Key[i & 31] ^ pkt[i - 1]`: el primero se encadena con el opcode, que va en claro.
// Para descifrar se recorre al revés, del último al primero, mientras el anterior sigue cifrado.
//
// No protege nada: la clave está en el binario. Es ofuscación (la guía de la CL2.0, actividad 5).

#pragma once

#include "Network/Packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Network::Crypto
{

// La tabla de 32 bytes del motor (03 § Envío; XOR32_KEY en red_reference.py).
inline constexpr std::array<std::uint8_t, 32> Xor32Key{
    0xE7, 0x6D, 0x3A, 0x89, 0xBC, 0xB2, 0x9F, 0x73, 0x23, 0xA8, 0xFE, 0xB6, 0x49, 0x5D, 0x39, 0x5D,
    0x8A, 0xCB, 0x63, 0x8D, 0xEA, 0x7D, 0x2B, 0x5F, 0xC3, 0xB1, 0xE9, 0x83, 0x29, 0x51, 0xE8, 0x56,
};

// El núcleo, sobre bytes sueltos: desde bytes[first] hasta el final, cada uno con
// Xor32Key[(i + keyShift) & 31] y el anterior ya cifrado. bytes[0] no tiene anterior y nunca
// cambia, así que un `first` de 0 empieza en 1.
//
// Un C1 o un C2 entero es (pkt, HeaderSize + 1, 0). FoxAnti (CL2.4) lo usa sobre lo que va a
// cifrarse en un C3/C4, el serial y el C1 desde su opcode, con (sealed, 2, 1).
void Xor32EncodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift);
void Xor32DecodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift);

// Un C1 o un C2 entero, desde el byte que sigue al opcode. Un C3 o un C4 vuelve igual: lo que
// lleva adentro se cifra antes de envolverlo (CL2.3).
[[nodiscard]] Packet Xor32Encode(Packet packet);
[[nodiscard]] Packet Xor32Decode(Packet packet);

} // namespace Client::Network::Crypto
