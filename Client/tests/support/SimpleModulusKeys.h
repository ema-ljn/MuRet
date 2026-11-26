#pragma once

#include "Crypto/SimpleModulus.h"

#include <gtest/gtest.h>

#include <utility>

// SimpleModulus keys for the tests: mu-client's identity key and two synthetic pairs, written as literals. The real
// ones are in the key files (KeyFiles.h). Encryption() and Decryption() build a key that must be valid: if FromLanes
// rejects it, the test fails and Value() aborts the program with a message. It is an error of the literal, not of
// the code.
namespace Client::Tests
{
inline Crypto::EncryptionKey Encryption(const Crypto::Lanes& modulus, const Crypto::Lanes& key,
                                        const Crypto::Lanes& xorKey)
{
    auto built = Crypto::EncryptionKey::FromLanes(modulus, key, xorKey);
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

inline Crypto::DecryptionKey Decryption(const Crypto::Lanes& modulus, const Crypto::Lanes& key,
                                        const Crypto::Lanes& xorKey)
{
    auto built = Crypto::DecryptionKey::FromLanes(modulus, key, xorKey);
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

// A key pair: both with the same modulus and the same XOR, and in each lane the decryption key is the inverse of the
// encryption key modulo m (Python's `pow(clave, -1, m)`).
struct KeyPair
{
    Crypto::EncryptionKey encryption;
    Crypto::DecryptionKey decryption;
};

inline KeyPair PairOf(const Crypto::Lanes& modulus, const Crypto::Lanes& encryptionKey,
                      const Crypto::Lanes& decryptionKey, const Crypto::Lanes& xorKey)
{
    return {Encryption(modulus, encryptionKey, xorKey), Decryption(modulus, decryptionKey, xorKey)};
}

// mu-client's identity key (identity_key, in tests/net/test_net_send.c): modulus 65537, key 1 and XOR 0 in the four
// lanes. Multiplying by 1 modulo 65537 does not change a lane, but the chaining with the previous one, the second pass
// and the 18 bits do.
inline KeyPair IdentityPair()
{
    return PairOf({65537, 65537, 65537, 65537}, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0});
}

// Two synthetic pairs, one with 17-bit moduli and the other with 18. In both, the encryption key times 0xFFFF and the
// decryption key times (m - 1) fit in 32 bits, as in the real keys: so no product wraps with a well-encrypted block,
// and decrypting undoes encrypting.
inline KeyPair Pair17()
{
    return PairOf({0x1A47D, 0x13273, 0x1CEDF, 0x1E0FF}, {0xE5E9, 0x4BB8, 0x4EC1, 0x8DD9},
                  {0x4568, 0x7EEF, 0x5E39, 0x50F2}, {0xA5A5, 0x5A5A, 0x0F0F, 0xF0F0});
}

inline KeyPair Pair18()
{
    return PairOf({0x24063, 0x3EC45, 0x2E467, 0x3AE38}, {0x6AE1, 0xBDB9, 0xE98F, 0xF5E3},
                  {0x27BB, 0x24B9, 0x1819, 0x235B}, {0x1357, 0x2468, 0x9BDF, 0xACE0});
}
} // namespace Client::Tests
