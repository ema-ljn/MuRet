#pragma once

#include "EngineApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// XOR32, the send engine's chained cipher (Client/docs/research/03-red-y-cifrado.md, "Envío: el motor C1/C2 y el
// XOR32"). In a C1 or C2, byte i (from the one after the opcode) does `value ^= Xor32Key[i & 31] ^ pkt[i - 1]`: the
// first chains with the opcode, which is plain. Decrypting walks backwards while the previous byte is still
// encrypted. It protects nothing: the key is in the binary.
namespace Client::Crypto
{
// The engine's 32-byte table (03 § Envío; XOR32_KEY in red_reference.py).
inline constexpr std::array<std::uint8_t, 32> Xor32Key{
    0xE7, 0x6D, 0x3A, 0x89, 0xBC, 0xB2, 0x9F, 0x73, 0x23, 0xA8, 0xFE, 0xB6, 0x49, 0x5D, 0x39, 0x5D,
    0x8A, 0xCB, 0x63, 0x8D, 0xEA, 0x7D, 0x2B, 0x5F, 0xC3, 0xB1, 0xE9, 0x83, 0x29, 0x51, 0xE8, 0x56,
};

// From bytes[first] to the end, each with Xor32Key[(i + keyShift) & 31] and the previous one already encrypted.
// bytes[0] has no previous and never changes, so a `first` of 0 starts at 1. A whole C1 or C2 is
// (pkt, HeaderSize + 1, 0). FoxAnti applies it to what will be encrypted in a C3/C4, the serial and the C1 from its
// opcode, with (sealed, 2, 1) as the original. There it undoes and redoes with the same key, so the shift cancels: any
// other gives the same result, even if what is in between is not the plain C1 (in a C2 sent as C4 the natural shift
// would be 2).
ENGINE_API void Xor32EncodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift);
ENGINE_API void Xor32DecodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift);
} // namespace Client::Crypto
