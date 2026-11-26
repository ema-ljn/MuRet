#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// The jewel mix (04, "Las ventanas de los NPC", "La mezcla de joyas"): the BC the jewel mix's dialogs send through
// 0x4029e0, bundling (BC 00, 0x402500) or undoing a bundle (BC 01, 0x401f30). Both build the C1 one byte at a time
// with the XOR32 of each new byte (the 32-byte key from 0xe7 0x6d 0x3a, from pkt[3]) and send it with send() in the
// clear, without the protection table. The GameServer 0.97 neither handles nor sends the BC: the ASM is the oracle,
// of the reply too (0x4e4547), which arrives as Protocol::Events::JewelMixResult.
namespace Client::Protocol::ChaosMix
{
// 0x402500: C1 06 BC 00 with the jewel (0x6c62e0) and the bundle (0x6c62e1 / 10 − 1: 0, 1 or 2 for 10, 20 or 30).
[[nodiscard]] Outgoing WriteJewelMix(std::uint8_t kind, std::uint8_t bundle);

// 0x401f30: C1 07 BC 01 with the jewel (0x6c62e0), the bundle's level (0x6c62dc) and its inventory place (0x6c62d8,
// which 0x401f5a already turned from cell to place by adding 12).
[[nodiscard]] Outgoing WriteJewelUnmix(std::uint8_t kind, std::uint8_t level, std::uint8_t slot);
} // namespace Client::Protocol::ChaosMix
