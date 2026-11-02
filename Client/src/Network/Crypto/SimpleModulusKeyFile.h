#pragma once

#include "Core/Expected.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Los archivos de clave del SimpleModulus: Enc1.dat y Dec2.dat del cliente, y Enc2.dat y Dec1.dat
// del servidor, de 54 bytes cada uno.
// Contrato: 0x686240 (Client/docs/research/03-red-y-cifrado.md, "C3/C4", y
// 00686240-carga-clave.md en el repo de research) y el plan de la CL2 (CL2.3).
//
//   offset 0  magic, u16: KeyFileMagic
//   offset 2  tamaño total que declara, u32: KeyFileSize
//   offset 6  tres bloques de 4 u32: los módulos, las claves y los XOR
//
// Todo little-endian, y cada u32 de un bloque va con XOR de KeyFileXor, el del mismo carril. Los de
// cifrar y los de descifrar tienen el mismo formato: el segundo bloque es la clave de cifrar en uno y
// la de descifrar en el otro.
namespace Client::Network::Crypto
{
// El magic que pasan los dos que llaman a 0x686240 (0x686070 y 0x6860b0).
inline constexpr std::uint16_t KeyFileMagic = 0x1112;
// 6 bytes de cabecera y tres bloques de 16.
inline constexpr std::size_t KeyFileSize = 54;
// La tabla de 0x6d84fc, la misma en los dos main.exe del laboratorio.
inline constexpr Lanes KeyFileXor{0x3F08A79B, 0xE25CC287, 0x93D27AB9, 0x20DEA7BF};

// InvalidKeyFile con otro magic, otro tamaño declarado o menos de KeyFileSize bytes; InvalidKey si
// un módulo no sirve (SimpleModulusKey::FromLanes). Lo que sigue a los 54 bytes no se lee, como en
// el original.
[[nodiscard]] Core::Expected<EncryptionKey, CryptoError> LoadEncryptionKey(std::span<const std::byte> file);
[[nodiscard]] Core::Expected<DecryptionKey, CryptoError> LoadDecryptionKey(std::span<const std::byte> file);
} // namespace Client::Network::Crypto
