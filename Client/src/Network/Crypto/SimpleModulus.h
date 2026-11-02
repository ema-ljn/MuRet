// SimpleModulus, el cifrado por bloques de los C3/C4: 8 bytes en claro dan 11 cifrados.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "C3/C4", el plan de la CL2 (CL2.3), y
// encrypt_block y decrypt_block de c3c4_reference.py, en el repo de research, que modelan
// 0x685bb0 y 0x685cc0.
//
// Los 8 bytes son cuatro carriles de 16 bits, little-endian. Cifrar un carril es
// `((xor ^ w ^ anterior) * clave) % módulo`, con el anterior ya cifrado. Una segunda pasada, del
// carril 2 al 0, combina cada uno con su XOR y con el que lo sigue, y cada carril se escribe en 18
// bits: sus dos bytes de abajo, el menos significativo primero y cada uno MSB primero, y después el
// bit 17 y el 16. Detrás van 2 bytes: el largo útil y un checksum de los 8 bytes en claro.
//
// No protege nada: las claves están en los archivos del cliente y del servidor (la guía de la
// CL2.0, actividad 5).

#pragma once

#include "Core/Expected.h"
#include "Network/Crypto/CryptoError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Network::Crypto
{

// Los carriles de 16 bits de un bloque.
inline constexpr std::size_t LaneCount = 4;
// Los bytes en claro de un bloque: el último de un paquete se completa con relleno.
inline constexpr std::size_t PlainBlockSize = 8;
// Los bytes cifrados de un bloque: 4 carriles de 18 bits y 2 bytes de cola.
inline constexpr std::size_t CipherBlockSize = 11;
// Los bits de un carril cifrado.
inline constexpr unsigned CipherLaneBits = 18;
// El módulo más grande con el que un carril cifrado entra en sus 18 bits: 0x40000, 2^18.
inline constexpr std::uint32_t MaxModulus = std::uint32_t{1} << CipherLaneBits;
// El valor más grande de un carril en claro, que tiene 16 bits.
inline constexpr std::uint32_t MaxPlainLane = 0xFFFF;

// Un valor por carril: los módulos, las claves o los XOR de una clave.
using Lanes = std::array<std::uint32_t, LaneCount>;

// Para qué sirve la clave de cada carril. Los dos archivos de un par (Enc1.dat del cliente y
// Dec1.dat del servidor, Enc2.dat del servidor y Dec2.dat del cliente) tienen el mismo módulo y el
// mismo XOR, y la clave de uno es la inversa de la del otro módulo m. Son dos tipos para que una
// clave cargada para descifrar no pueda cifrar. Los archivos de los dos tienen el mismo formato, así
// que el tipo lo decide el cargador que se usa: LoadEncryptionKey de un Dec2.dat carga.
enum class KeyRole : std::uint8_t
{
    Encrypt,
    Decrypt,
};

template <KeyRole Role> class SimpleModulusKey
{
public:
    // InvalidKey si en un carril el módulo no sirve (Q-C41). Con uno mayor que MaxModulus, el
    // carril cifrado no entra en sus 18 bits. Y `xor | MaxPlainLane` tiene que ser menor que el
    // módulo: si no, `xor ^ w ^ anterior` puede llegar al módulo y el `%` pierde el carril (con un
    // XOR de 16 bits, cualquier módulo de 17 bits o más sirve). Un módulo en 0 tampoco lo cumple, y
    // el `%` dividiría por cero. Las claves pueden ser cualquier valor, como en el original: que el
    // producto entre en 32 bits depende del par, y si no entra da la vuelta como el IMUL.
    [[nodiscard]] static Core::Expected<SimpleModulusKey, CryptoError> FromLanes(const Lanes& modulus, const Lanes& key,
                                                                                 const Lanes& xorKey)
    {
        for (std::size_t lane = 0; lane < LaneCount; ++lane)
        {
            if (modulus[lane] > MaxModulus || (xorKey[lane] | MaxPlainLane) >= modulus[lane])
            {
                return Core::Unexpected(CryptoError::InvalidKey);
            }
        }
        return SimpleModulusKey(modulus, key, xorKey);
    }

    [[nodiscard]] const Lanes& Modulus() const
    {
        return modulus;
    }
    // La de cifrar en una EncryptionKey y la de descifrar en una DecryptionKey.
    [[nodiscard]] const Lanes& Key() const
    {
        return key;
    }
    [[nodiscard]] const Lanes& XorKey() const
    {
        return xorKey;
    }

private:
    SimpleModulusKey(const Lanes& modulusLanes, const Lanes& keyLanes, const Lanes& xorLanes)
        : modulus(modulusLanes), key(keyLanes), xorKey(xorLanes)
    {
    }

    Lanes modulus;
    Lanes key;
    Lanes xorKey;
};

// Enc1.dat del cliente y Enc2.dat del servidor.
using EncryptionKey = SimpleModulusKey<KeyRole::Encrypt>;
// Dec2.dat del cliente y Dec1.dat del servidor.
using DecryptionKey = SimpleModulusKey<KeyRole::Decrypt>;

// Un bloque. Se cifran siempre los 8 bytes, también el relleno de un bloque incompleto, y
// `usefulSize` dice cuántos valen: de 1 a 8 en un paquete, pero se escribe el que llegue. Los
// productos van en std::uint32_t y dan la vuelta, como el IMUL del original.
[[nodiscard]] std::array<std::byte, CipherBlockSize>
EncryptBlock(const EncryptionKey& key, std::span<const std::byte, PlainBlockSize> plain, std::uint8_t usefulSize);

struct DecryptedBlock
{
    std::array<std::byte, PlainBlockSize> bytes{}; // los 8, relleno incluido
    // Como vino: que esté entre 1 y 8 lo mira quien arma el paquete (CL2.3, PR B).
    std::uint8_t usefulSize = 0;
};

// std::nullopt si el checksum no coincide. El checksum es de los 8 bytes en claro, así que un
// cambio en el byte del largo útil no lo ve.
[[nodiscard]] std::optional<DecryptedBlock> DecryptBlock(const DecryptionKey& key,
                                                         std::span<const std::byte, CipherBlockSize> cipher);

} // namespace Client::Network::Crypto
