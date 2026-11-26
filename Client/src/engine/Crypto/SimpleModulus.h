#pragma once

#include "Core/Expected.h"
#include "Crypto/CryptoError.h"
#include "EngineApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// SimpleModulus, the C3/C4 block cipher: 8 plain bytes become 11. Contract: Client/docs/research/03-red-y-cifrado.md,
// "C3/C4", and the research repo's c3c4_reference.py (encrypt_block and decrypt_block), which models 0x685bb0 and
// 0x685cc0. It protects nothing: the keys are in the client's and server's files.
namespace Client::Crypto
{
inline constexpr std::size_t LaneCount = 4;
// The last block of a packet is padded.
inline constexpr std::size_t PlainBlockSize = 8;
// 4 lanes of 18 bits and a 2-byte tail.
inline constexpr std::size_t CipherBlockSize = 11;
inline constexpr unsigned CipherLaneBits = 18;
// The largest modulus whose encrypted lanes fit their 18 bits: 0x40000, 2^18.
inline constexpr std::uint32_t MaxModulus = std::uint32_t{1} << CipherLaneBits;
inline constexpr std::uint32_t MaxPlainLane = 0xFFFF;

// The moduli, the keys or a key's XORs.
using Lanes = std::array<std::uint32_t, LaneCount>;

// The two files of a pair (the client's Enc1.dat and the server's Dec1.dat; the server's Enc2.dat and the client's
// Dec2.dat) share modulus and XOR, and one's key is the other's inverse modulo m. Two types so a key loaded to decrypt
// cannot encrypt. Both files share the format, so the loader used decides the type: LoadEncryptionKey of a Dec2.dat
// loads.
enum class KeyRole : std::uint8_t
{
    Encrypt,
    Decrypt,
};

template <KeyRole Role> class SimpleModulusKey
{
public:
    // InvalidKey if a lane's modulus is unusable (Q-C41). Over MaxModulus, the encrypted lane does not fit its 18 bits.
    // `xor | MaxPlainLane` must be below the modulus, or `xor ^ w ^ previous` can reach it and the `%` loses the lane
    // (with a 16-bit XOR any modulus of 17 bits or more works). A modulus of 0 fails that too, and `%` would divide by
    // zero. Keys may be any value, as in the original: whether the product fits 32 bits depends on the pair, and if not
    // it wraps like the IMUL.
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
        return m_modulus;
    }
    // The encryption key in an EncryptionKey and the decryption key in a DecryptionKey.
    [[nodiscard]] const Lanes& Key() const
    {
        return m_key;
    }
    [[nodiscard]] const Lanes& XorKey() const
    {
        return m_xorKey;
    }

private:
    SimpleModulusKey(const Lanes& modulus, const Lanes& key, const Lanes& xorKey)
        : m_modulus(modulus), m_key(key), m_xorKey(xorKey)
    {
    }

    Lanes m_modulus;
    Lanes m_key;
    Lanes m_xorKey;
};

// The client's Enc1.dat and the server's Enc2.dat.
using EncryptionKey = SimpleModulusKey<KeyRole::Encrypt>;
// The client's Dec2.dat and the server's Dec1.dat.
using DecryptionKey = SimpleModulusKey<KeyRole::Decrypt>;

// All 8 bytes are always encrypted, a partial block's padding too, and `usefulSize` says how many count: 1 to 8 in a
// packet, but whatever comes is written. Products are std::uint32_t and wrap, like the original's IMUL.
[[nodiscard]] ENGINE_API std::array<std::byte, CipherBlockSize>
EncryptBlock(const EncryptionKey& key, std::span<const std::byte, PlainBlockSize> plain, std::uint8_t usefulSize);

struct DecryptedBlock
{
    std::array<std::byte, PlainBlockSize> bytes{}; // all 8, padding included
    // As it came: whoever builds the packet checks it is between 1 and 8.
    std::uint8_t usefulSize = 0;
};

// std::nullopt if the checksum does not match. The checksum covers the 8 plain bytes, so a change in the useful-size
// byte goes unseen.
[[nodiscard]] ENGINE_API std::optional<DecryptedBlock> DecryptBlock(const DecryptionKey& key,
                                                                    std::span<const std::byte, CipherBlockSize> cipher);
} // namespace Client::Crypto
