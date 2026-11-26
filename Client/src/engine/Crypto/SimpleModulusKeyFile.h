#pragma once

#include "Core/Expected.h"
#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The SimpleModulus key files: the client's Enc1.dat and Dec2.dat and the server's Enc2.dat and Dec1.dat, 54 bytes
// each. Contract: 0x686240 (Client/docs/research/03-red-y-cifrado.md, "C3/C4", and the research repo's
// 00686240-carga-clave.md). Little-endian: the u16 magic, the u32 declared total size, then three blocks of 4 u32
// (moduli, keys and XORs), each u32 XOR KeyFileXor's for its lane. The second block is the encryption key in one
// file and the decryption key in the other.
namespace Client::Crypto
{
// The magic both callers of 0x686240 pass (0x686070 and 0x6860b0).
inline constexpr std::uint16_t KeyFileMagic = 0x1112;
// A 6-byte header and three 16-byte blocks.
inline constexpr std::size_t KeyFileSize = 54;
// The table at 0x6d84fc, the same in both lab main.exe.
inline constexpr Lanes KeyFileXor{0x3F08A79B, 0xE25CC287, 0x93D27AB9, 0x20DEA7BF};

// InvalidKeyFile with another magic, another declared size or fewer than KeyFileSize bytes; InvalidKey if a modulus
// is unusable (SimpleModulusKey::FromLanes). What follows the 54 bytes is not read, as in the original.
[[nodiscard]] ENGINE_API Core::Expected<EncryptionKey, CryptoError> LoadEncryptionKey(std::span<const std::byte> file);
[[nodiscard]] ENGINE_API Core::Expected<DecryptionKey, CryptoError> LoadDecryptionKey(std::span<const std::byte> file);
} // namespace Client::Crypto
