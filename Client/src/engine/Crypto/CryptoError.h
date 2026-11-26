#pragma once

#include "EngineApi.h"

#include <cstdint>
#include <string_view>

// Contract: Client/docs/research/03-red-y-cifrado.md, "C3/C4". InvalidKey fixes an original bug: Q-C41 in
// 07-quirks-y-bugs.md. Of the decryption ones, ChecksumMismatch is where the original checks too (0x685b30 returns
// -1); NotWholeBlocks, InvalidBlock and TooShort are where it does not (03, "C3/C4").
namespace Client::Crypto
{
enum class CryptoError : std::uint8_t
{
    InvalidKeyFile,   // another magic, another declared size, or fewer bytes than it declares
    InvalidKey,       // a modulus of 0 or over MaxModulus, or an XOR that reaches the modulus (Q-C41)
    WrongPacketType,  // a C3/C4 to encrypt, a C1/C2 to decrypt, or a Packet without its bytes
    ChecksumMismatch, // a block's checksum does not match its 8 decrypted bytes
    InvalidBlock,     // a block claims 0 or more than 8 useful bytes, or fewer when it is not the last
    NotWholeBlocks,   // the encrypted size is not a multiple of 11
    TooShort,         // the decrypted bytes lack the serial and the opcode
    TooLarge,         // encrypted, the packet would exceed MaxPacketSize
};

// For the log. Never shows a value of the key or the packet.
[[nodiscard]] ENGINE_API std::string_view CryptoErrorText(CryptoError error);
} // namespace Client::Crypto
