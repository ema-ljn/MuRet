// Client::Network::Crypto::EncryptBlock y DecryptBlock: los bloques del SimpleModulus, con claves
// sintéticas escritas acá. Los bloques cifrados que se esperan los dio encrypt_block de
// c3c4_reference.py, en el repo de research, y los descifrados, decrypt_block.

#include "Network/Crypto/SimpleModulus.h"

#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

namespace
{

using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::DecryptBlock;
using Client::Network::Crypto::DecryptedBlock;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::EncryptionKey;
using Client::Network::Crypto::Lanes;
using Client::Network::Crypto::MaxModulus;
using Client::Network::Crypto::PlainBlockSize;
using Client::Tests::SameBytes;

using PlainBlock = std::array<std::byte, PlainBlockSize>;
using CipherBlock = std::array<std::byte, CipherBlockSize>;

// Una clave de descifrar no cifra, ni al revés.
static_assert(!std::is_convertible_v<DecryptionKey, EncryptionKey>);
static_assert(!std::is_convertible_v<EncryptionKey, DecryptionKey>);

template <std::size_t N> std::array<std::byte, N> BytesOf(const std::array<int, N>& values)
{
    std::array<std::byte, N> bytes{};
    std::ranges::transform(values, bytes.begin(), [](int value) { return static_cast<std::byte>(value); });
    return bytes;
}

EncryptionKey Encryption(const Lanes& modulus, const Lanes& key, const Lanes& xorKey)
{
    auto built = EncryptionKey::FromLanes(modulus, key, xorKey);
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

DecryptionKey Decryption(const Lanes& modulus, const Lanes& key, const Lanes& xorKey)
{
    auto built = DecryptionKey::FromLanes(modulus, key, xorKey);
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

// Un par de claves: las dos con el mismo módulo y el mismo XOR, y en cada carril la de descifrar
// es la inversa de la de cifrar módulo m (`pow(clave, -1, m)` de Python).
struct KeyPair
{
    EncryptionKey encryption;
    DecryptionKey decryption;
};

KeyPair PairOf(const Lanes& modulus, const Lanes& encryptionKey, const Lanes& decryptionKey, const Lanes& xorKey)
{
    return {Encryption(modulus, encryptionKey, xorKey), Decryption(modulus, decryptionKey, xorKey)};
}

// La clave identidad de mu-client (identity_key, en tests/net/test_net_send.c): módulo 65537,
// clave 1 y XOR 0 en los cuatro carriles. Cifrar un carril no lo cambia, pero la segunda pasada y
// los 18 bits sí.
KeyPair IdentityPair()
{
    return PairOf({65537, 65537, 65537, 65537}, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0});
}

// Dos pares sintéticos, uno con módulos de 17 bits y otro de 18. En los dos, clave de cifrar por
// 0xFFFF y clave de descifrar por (m - 1) entran en 32 bits, como en las claves reales: así ningún
// producto da la vuelta con un bloque bien cifrado, y descifrar deshace cifrar.
KeyPair Pair17()
{
    return PairOf({0x1A47D, 0x13273, 0x1CEDF, 0x1E0FF}, {0xE5E9, 0x4BB8, 0x4EC1, 0x8DD9},
                  {0x4568, 0x7EEF, 0x5E39, 0x50F2}, {0xA5A5, 0x5A5A, 0x0F0F, 0xF0F0});
}

KeyPair Pair18()
{
    return PairOf({0x24063, 0x3EC45, 0x2E467, 0x3AE38}, {0x6AE1, 0xBDB9, 0xE98F, 0xF5E3},
                  {0x27BB, 0x24B9, 0x1819, 0x235B}, {0x1357, 0x2468, 0x9BDF, 0xACE0});
}

// Que `cipher` descifre en `plain` con `usefulSize` útiles.
void ExpectDecryptsTo(const DecryptionKey& key, const CipherBlock& cipher, const PlainBlock& plain,
                      std::uint8_t usefulSize)
{
    const std::optional<DecryptedBlock> block = DecryptBlock(key, cipher);
    ASSERT_TRUE(block.has_value());
    EXPECT_TRUE(SameBytes(block->bytes, plain));
    EXPECT_EQ(block->usefulSize, usefulSize);
}

// Los bloques de FIX_C3_SERIAL0, FIX_C3_RANDA5 y FIX_C3_SERIALFF, de mu-client: el aviso
// `C1 06 F1 03 06 00`, ya con XOR32, con el serial en lugar del tamaño, 5 bytes útiles y el
// relleno. Lo que va antes de los 11 bytes, `C3 0D`, lo arma el paquete (CL2.3, PR B).
TEST(SimpleModulusTest, EncryptsTheBlocksOfTheMuClientFixturesWithTheIdentityKey)
{
    const struct
    {
        const char* name;
        PlainBlock plain;
        CipherBlock cipher;
    } cases[] = {
        {"FIX_C3_SERIAL0", BytesOf<8>({0x00, 0xF1, 0x7B, 0xC1, 0x73, 0x00, 0x00, 0x00}),
         BytesOf<11>({0x7B, 0xC1, 0x1C, 0xC0, 0x00, 0x00, 0x00, 0x20, 0xC0, 0xF8, 0xC0})},
        {"FIX_C3_RANDA5", BytesOf<8>({0x00, 0xF1, 0x7B, 0xC1, 0x73, 0xA5, 0x00, 0x00}),
         BytesOf<11>({0x7B, 0xC1, 0x1C, 0xE9, 0x40, 0x00, 0x00, 0x22, 0x54, 0x5D, 0x65})},
        {"FIX_C3_SERIALFF", BytesOf<8>({0xFF, 0xF1, 0x7B, 0xC1, 0x73, 0x00, 0x00, 0x00}),
         BytesOf<11>({0x7B, 0xC1, 0x1C, 0xC0, 0x00, 0x00, 0x03, 0xDC, 0xC0, 0x07, 0x3F})},
    };
    const KeyPair identity = IdentityPair();
    constexpr std::uint8_t UsefulSize = 5;

    for (const auto& expected : cases)
    {
        EXPECT_TRUE(SameBytes(EncryptBlock(identity.encryption, expected.plain, UsefulSize), expected.cipher))
            << expected.name;
        ExpectDecryptsTo(identity.decryption, expected.cipher, expected.plain, UsefulSize);
    }
}

// Un solo bit en 1, con la clave identidad. El carril 0 vale 1, y cada uno se encadena con el
// anterior ya cifrado, así que antes de la segunda pasada los cuatro valen 1. La segunda pasada
// combina cada uno con el que lo sigue y los deja en 0, 0, 0 y 1. El carril 3 empieza en el bit 54
// del bloque, y su bit 0 es el último de su primer byte: el bit 61, que en el byte 7 vale 0x04.
// La cola: el checksum es 0xF8 ^ 0x01 = 0xF9, y el largo, 8 ^ 0x3D ^ 0xF9 = 0xCC.
TEST(SimpleModulusTest, EncryptsASingleBitLikeTheReference)
{
    const PlainBlock plain = BytesOf<8>({0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf<11>({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0xCC, 0xF9});
    const KeyPair identity = IdentityPair();

    EXPECT_TRUE(SameBytes(EncryptBlock(identity.encryption, plain, 8), expected));
    ExpectDecryptsTo(identity.decryption, expected, plain, 8);
}

// Los bits 16 y 17 de un carril, que la clave identidad nunca prende y que un ida y vuelta no ve si
// se escriben y se leen en el mismo orden equivocado. Con este bloque, el carril 0 cifrado tiene
// solo el bit 16 y el 1 solo el 17: después de los 16 bits de abajo va el 17 y después el 16, así
// que el byte 2 empieza con 0 y 1 (`0x50`).
TEST(SimpleModulusTest, WritesBit17BeforeBit16)
{
    const KeyPair pair = PairOf({0x3FFFB, 0x3FFFB, 0x3FFFB, 0x3FFFB}, {0x6666, 0x5555, 0x4444, 0x3C3C},
                                {10, 12, 15, 17}, {0x1234, 0x5678, 0x9ABC, 0xDEF0});
    const PlainBlock plain = BytesOf<8>({0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf<11>({0xE2, 0x32, 0x50, 0x6E, 0xA6, 0x98, 0x95, 0x49, 0x10, 0xDD, 0xE8});

    EXPECT_TRUE(SameBytes(EncryptBlock(pair.encryption, plain, 8), expected));
    ExpectDecryptsTo(pair.decryption, expected, plain, 8);
}

// Los dos pares sintéticos, con un bloque lleno y uno con 4 útiles y el relleno en 0.
TEST(SimpleModulusTest, EncryptsLikeTheReferenceWithBothSyntheticPairs)
{
    const PlainBlock full = BytesOf<8>({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const PlainBlock half = BytesOf<8>({0xFE, 0xDC, 0xBA, 0x98, 0x00, 0x00, 0x00, 0x00});
    const struct
    {
        const char* name;
        KeyPair pair;
        CipherBlock full;
        CipherBlock half;
    } cases[] = {
        {"17 bits", Pair17(), BytesOf<11>({0x89, 0xC7, 0x0C, 0x48, 0x42, 0xDB, 0x11, 0xEE, 0xC9, 0xCD, 0xF8}),
         BytesOf<11>({0x6D, 0x9D, 0x2C, 0xA5, 0x85, 0xC3, 0x72, 0x48, 0x01, 0xC1, 0xF8})},
        {"18 bits", Pair18(), BytesOf<11>({0x5E, 0xF2, 0x5D, 0x6D, 0xE2, 0x0C, 0x86, 0x7C, 0x07, 0xCD, 0xF8}),
         BytesOf<11>({0xED, 0xEC, 0x41, 0x06, 0xB0, 0x70, 0xA6, 0xA0, 0x26, 0xC1, 0xF8})},
    };

    for (const auto& expected : cases)
    {
        EXPECT_TRUE(SameBytes(EncryptBlock(expected.pair.encryption, full, 8), expected.full)) << expected.name;
        EXPECT_TRUE(SameBytes(EncryptBlock(expected.pair.encryption, half, 4), expected.half)) << expected.name;
        ExpectDecryptsTo(expected.pair.decryption, expected.full, full, 8);
        ExpectDecryptsTo(expected.pair.decryption, expected.half, half, 4);
    }
}

// La salida directa de std::mt19937 es la misma en todas las bibliotecas; la de
// std::uniform_int_distribution, no.
std::uint32_t Below(std::mt19937& engine, std::uint32_t n)
{
    return static_cast<std::uint32_t>(engine() % n);
}

// De 1 a 64 bytes al azar, partidos en bloques de 8 con el último completado con ceros, como
// encrypt de c3c4_reference.py: cifrar y descifrar los devuelve, con el largo útil de cada bloque.
TEST(SimpleModulusTest, DecryptUndoesEncryptFrom1To64Bytes)
{
    constexpr std::mt19937::result_type Seed = 20261102;
    constexpr std::uint32_t Rounds = 500;
    constexpr std::uint32_t MaxBytes = 64;
    constexpr std::uint32_t ByteValues = 256;
    std::mt19937 engine(Seed);

    for (const KeyPair& pair : {Pair17(), Pair18()})
    {
        for (std::uint32_t round = 0; round < Rounds; ++round)
        {
            const std::size_t size = 1 + Below(engine, MaxBytes);
            std::vector<std::byte> plain((size + PlainBlockSize - 1) / PlainBlockSize * PlainBlockSize);
            for (std::size_t i = 0; i < size; ++i)
            {
                plain[i] = static_cast<std::byte>(Below(engine, ByteValues));
            }

            for (std::size_t offset = 0; offset < size; offset += PlainBlockSize)
            {
                const auto block = std::span(plain).subspan(offset).first<PlainBlockSize>();
                const auto useful = static_cast<std::uint8_t>(std::min(size - offset, PlainBlockSize));

                const std::optional<DecryptedBlock> decrypted =
                    DecryptBlock(pair.decryption, EncryptBlock(pair.encryption, block, useful));

                ASSERT_TRUE(decrypted.has_value()) << "vuelta " << round;
                EXPECT_TRUE(SameBytes(decrypted->bytes, block)) << "vuelta " << round;
                EXPECT_EQ(decrypted->usefulSize, useful) << "vuelta " << round;
            }
        }
    }
}

// Un bit cambiado en un carril o en el checksum hace fallar el checksum.
TEST(SimpleModulusTest, AChangedByteFailsTheChecksum)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf<8>({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);
    ASSERT_TRUE(DecryptBlock(pair.decryption, cipher).has_value());

    for (const std::size_t index : {std::size_t{0}, std::size_t{4}, std::size_t{10}})
    {
        CipherBlock changed = cipher;
        changed[index] ^= std::byte{0x01};

        EXPECT_FALSE(DecryptBlock(pair.decryption, changed).has_value()) << "byte " << index;
    }
}

// El checksum es de los 8 bytes en claro: un cambio en el byte 9, el del largo útil, descifra igual
// y da otro largo. Que no pase de 8 lo mira el paquete.
TEST(SimpleModulusTest, TheChecksumDoesNotCoverTheUsefulSize)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf<8>({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);
    cipher[9] ^= std::byte{0x01};

    ExpectDecryptsTo(pair.decryption, cipher, plain, 9);
}

// Un carril cifrado con sus 18 bits en 1, 0x3FFFF, más grande que su módulo: un servidor bueno no lo
// manda, pero el original lo multiplica igual. Va en el carril 3, que no pasa por la segunda pasada
// y llega a la multiplicación como vino: 0x50F2 * 0x3FFFF pasa de 32 bits, y el producto da la
// vuelta antes del `%`. La cola se armó para que el checksum coincida. Con el producto en 64 bits,
// los bytes 6 y 7 darían otra cosa y el checksum fallaría.
TEST(SimpleModulusTest, DecryptWrapsTheProductLikeTheImul)
{
    const CipherBlock cipher = BytesOf<11>({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0x52, 0x67});
    const PlainBlock expected = BytesOf<8>({0x29, 0x27, 0xF3, 0x8A, 0x7E, 0x97, 0x21, 0x20});

    ExpectDecryptsTo(Pair17().decryption, cipher, expected, 8);
}

// Al cifrar pasa lo mismo con una clave de más de 16 bits: 0xFFFF * 0x3FFF0 pasa de 32 bits. Las
// claves reales no llegan (GoldenSimpleModulusKeyFileTest), pero el original no lo mira.
TEST(SimpleModulusTest, EncryptWrapsTheProductLikeTheImul)
{
    const EncryptionKey key = Encryption({0x3FFFB, 0x3FFFB, 0x3FFFB, 0x3FFFB}, {0x3FFF0, 1, 1, 1}, {0, 0, 0, 0});
    const PlainBlock plain = BytesOf<8>({0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf<11>({0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0xDC, 0xFC, 0xCD, 0xF8});

    EXPECT_TRUE(SameBytes(EncryptBlock(key, plain, 8), expected));
}

// Q-C41: un módulo en 0 haría dividir por cero, y uno de más de 18 bits perdería los bits de arriba
// del carril cifrado. En cualquier carril, y en las dos clases de clave.
TEST(SimpleModulusTest, FromLanesRejectsAModulusThatDoesNotFit)
{
    const Lanes key{1, 1, 1, 1};
    const Lanes xorKey{0, 0, 0, 0};

    for (std::size_t lane = 0; lane < key.size(); ++lane)
    {
        for (const std::uint32_t modulus : {std::uint32_t{0}, MaxModulus + 1})
        {
            Lanes moduli{65537, 65537, 65537, 65537};
            moduli[lane] = modulus;

            const auto encryption = EncryptionKey::FromLanes(moduli, key, xorKey);
            const auto decryption = DecryptionKey::FromLanes(moduli, key, xorKey);

            ASSERT_FALSE(encryption.HasValue()) << "carril " << lane << ", módulo " << modulus;
            ASSERT_FALSE(decryption.HasValue()) << "carril " << lane << ", módulo " << modulus;
            EXPECT_EQ(encryption.Error(), CryptoError::InvalidKey);
            EXPECT_EQ(decryption.Error(), CryptoError::InvalidKey);
        }
    }
    EXPECT_TRUE(EncryptionKey::FromLanes({1, MaxModulus, 1, MaxModulus}, key, xorKey).HasValue());
}

} // namespace
