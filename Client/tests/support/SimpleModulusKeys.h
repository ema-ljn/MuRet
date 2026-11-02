#pragma once

#include "Network/Crypto/SimpleModulus.h"

#include <gtest/gtest.h>

#include <utility>

// Claves del SimpleModulus para los tests: la identidad de mu-client y dos pares sintéticos,
// escritos como literales. Las reales están en los archivos de clave (KeyFiles.h).
//
// Encryption() y Decryption() arman una clave que tiene que servir: si FromLanes la rechaza, el
// test falla y Value() corta el programa con un mensaje. Es un error del literal, no del código.
namespace Client::Tests
{
inline Network::Crypto::EncryptionKey Encryption(const Network::Crypto::Lanes& modulus,
                                                 const Network::Crypto::Lanes& key,
                                                 const Network::Crypto::Lanes& xorKey)
{
    auto built = Network::Crypto::EncryptionKey::FromLanes(modulus, key, xorKey);
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

inline Network::Crypto::DecryptionKey Decryption(const Network::Crypto::Lanes& modulus,
                                                 const Network::Crypto::Lanes& key,
                                                 const Network::Crypto::Lanes& xorKey)
{
    auto built = Network::Crypto::DecryptionKey::FromLanes(modulus, key, xorKey);
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

// Un par de claves: las dos con el mismo módulo y el mismo XOR, y en cada carril la de descifrar
// es la inversa de la de cifrar módulo m (`pow(clave, -1, m)` de Python).
struct KeyPair
{
    Network::Crypto::EncryptionKey encryption;
    Network::Crypto::DecryptionKey decryption;
};

inline KeyPair PairOf(const Network::Crypto::Lanes& modulus, const Network::Crypto::Lanes& encryptionKey,
                      const Network::Crypto::Lanes& decryptionKey, const Network::Crypto::Lanes& xorKey)
{
    return {Encryption(modulus, encryptionKey, xorKey), Decryption(modulus, decryptionKey, xorKey)};
}

// La clave identidad de mu-client (identity_key, en tests/net/test_net_send.c): módulo 65537,
// clave 1 y XOR 0 en los cuatro carriles. Multiplicar por 1 módulo 65537 no cambia un carril, pero
// el encadenamiento con el anterior, la segunda pasada y los 18 bits sí.
inline KeyPair IdentityPair()
{
    return PairOf({65537, 65537, 65537, 65537}, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0});
}

// Dos pares sintéticos, uno con módulos de 17 bits y otro de 18. En los dos, clave de cifrar por
// 0xFFFF y clave de descifrar por (m - 1) entran en 32 bits, como en las claves reales: así ningún
// producto da la vuelta con un bloque bien cifrado, y descifrar deshace cifrar.
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
