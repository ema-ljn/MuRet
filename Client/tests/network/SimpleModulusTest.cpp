// Client::Network::Crypto::EncryptBlock y DecryptBlock: los bloques del SimpleModulus, con claves
// sintéticas escritas acá. Los bloques cifrados que se esperan los dio encrypt_block de
// c3c4_reference.py, en el repo de research, y los descifrados, decrypt_block.

#include "Network/Crypto/SimpleModulus.h"

#include "support/FixedRandom.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

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
using Client::Tests::Below;
using Client::Tests::Encryption;
using Client::Tests::FixedSeed;
using Client::Tests::IdentityPair;
using Client::Tests::KeyPair;
using Client::Tests::Pair17;
using Client::Tests::Pair18;
using Client::Tests::PairOf;
using Client::Tests::SameBytes;

using PlainBlock = std::array<std::byte, PlainBlockSize>;
using CipherBlock = std::array<std::byte, CipherBlockSize>;

// Una clave de descifrar no cifra, ni al revés.
static_assert(!std::is_convertible_v<DecryptionKey, EncryptionKey>);
static_assert(!std::is_convertible_v<EncryptionKey, DecryptionKey>);

// El límite de Q-C41, escrito como número: 2^18.
static_assert(MaxModulus == 0x40000);

// Bytes escritos como en Bytes(), en un std::array de tantos como se escriben: con uno de menos, el
// bloque no es un PlainBlock ni un CipherBlock y no compila.
template <std::size_t N> std::array<std::byte, N> BytesOf(const int (&values)[N])
{
    std::array<std::byte, N> bytes{};
    std::ranges::transform(values, bytes.begin(), [](int value) { return static_cast<std::byte>(value); });
    return bytes;
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
        {"FIX_C3_SERIAL0", BytesOf({0x00, 0xF1, 0x7B, 0xC1, 0x73, 0x00, 0x00, 0x00}),
         BytesOf({0x7B, 0xC1, 0x1C, 0xC0, 0x00, 0x00, 0x00, 0x20, 0xC0, 0xF8, 0xC0})},
        {"FIX_C3_RANDA5", BytesOf({0x00, 0xF1, 0x7B, 0xC1, 0x73, 0xA5, 0x00, 0x00}),
         BytesOf({0x7B, 0xC1, 0x1C, 0xE9, 0x40, 0x00, 0x00, 0x22, 0x54, 0x5D, 0x65})},
        {"FIX_C3_SERIALFF", BytesOf({0xFF, 0xF1, 0x7B, 0xC1, 0x73, 0x00, 0x00, 0x00}),
         BytesOf({0x7B, 0xC1, 0x1C, 0xC0, 0x00, 0x00, 0x03, 0xDC, 0xC0, 0x07, 0x3F})},
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
    const PlainBlock plain = BytesOf({0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0xCC, 0xF9});
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
    const PlainBlock plain = BytesOf({0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf({0xE2, 0x32, 0x50, 0x6E, 0xA6, 0x98, 0x95, 0x49, 0x10, 0xDD, 0xE8});

    EXPECT_TRUE(SameBytes(EncryptBlock(pair.encryption, plain, 8), expected));
    ExpectDecryptsTo(pair.decryption, expected, plain, 8);
}

// Los dos pares sintéticos, con un bloque lleno y uno con 4 útiles y el relleno en 0.
TEST(SimpleModulusTest, EncryptsLikeTheReferenceWithBothSyntheticPairs)
{
    const PlainBlock full = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const PlainBlock half = BytesOf({0xFE, 0xDC, 0xBA, 0x98, 0x00, 0x00, 0x00, 0x00});
    const struct
    {
        const char* name;
        KeyPair pair;
        CipherBlock full;
        CipherBlock half;
    } cases[] = {
        {"17 bits", Pair17(), BytesOf({0x89, 0xC7, 0x0C, 0x48, 0x42, 0xDB, 0x11, 0xEE, 0xC9, 0xCD, 0xF8}),
         BytesOf({0x6D, 0x9D, 0x2C, 0xA5, 0x85, 0xC3, 0x72, 0x48, 0x01, 0xC1, 0xF8})},
        {"18 bits", Pair18(), BytesOf({0x5E, 0xF2, 0x5D, 0x6D, 0xE2, 0x0C, 0x86, 0x7C, 0x07, 0xCD, 0xF8}),
         BytesOf({0xED, 0xEC, 0x41, 0x06, 0xB0, 0x70, 0xA6, 0xA0, 0x26, 0xC1, 0xF8})},
    };

    for (const auto& expected : cases)
    {
        EXPECT_TRUE(SameBytes(EncryptBlock(expected.pair.encryption, full, 8), expected.full)) << expected.name;
        EXPECT_TRUE(SameBytes(EncryptBlock(expected.pair.encryption, half, 4), expected.half)) << expected.name;
        ExpectDecryptsTo(expected.pair.decryption, expected.full, full, 8);
        ExpectDecryptsTo(expected.pair.decryption, expected.half, half, 4);
    }
}

// De 1 a 64 bytes al azar, partidos en bloques de 8 con el último completado con ceros, como
// encrypt de c3c4_reference.py: cifrar y descifrar los devuelve, con el largo útil de cada bloque.
TEST(SimpleModulusTest, DecryptUndoesEncryptFrom1To64Bytes)
{
    constexpr std::uint32_t Rounds = 500;     // vueltas por par
    constexpr std::uint32_t MaxBytes = 64;    // ocho bloques
    constexpr std::uint32_t ByteValues = 256; // los valores de un byte
    std::mt19937 engine(FixedSeed);

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

// El checksum es un byte: 0xF8 con XOR de los 8 bytes en claro. Un cambio en el byte 10, el del
// checksum, siempre se ve, porque el claro no cambia y el byte sí.
TEST(SimpleModulusTest, AChangedChecksumByteAlwaysFails)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);
    ASSERT_TRUE(DecryptBlock(pair.decryption, cipher).has_value());
    constexpr std::size_t ChecksumByte = CipherBlockSize - 1; // el último del bloque
    constexpr unsigned BitsPerByte = 8;                       // se cambia cada uno, de a uno

    for (unsigned bit = 0; bit < BitsPerByte; ++bit)
    {
        CipherBlock changed = cipher;
        changed[ChecksumByte] ^= std::byte{0x01} << bit;

        EXPECT_FALSE(DecryptBlock(pair.decryption, changed).has_value()) << "bit " << bit;
    }
}

// Un cambio en los bytes de los carriles da otro claro, y su XOR casi siempre es otro: con este
// bloque, los bytes 0 y 4 cambiados fallan. En general pasa uno de cada 256, más o menos (el
// siguiente test).
TEST(SimpleModulusTest, AChangedLaneByteFailsTheChecksumWithThisBlock)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);

    for (const std::size_t index : {std::size_t{0}, std::size_t{4}})
    {
        CipherBlock changed = cipher;
        changed[index] ^= std::byte{0x01};

        EXPECT_FALSE(DecryptBlock(pair.decryption, changed).has_value()) << "byte " << index;
    }
}

// Uno que pasa: el bloque 08 00 00 00 00 00 00 00 con Pair17, y el bit 4 del byte 3 cambiado. Se
// descifra en otro claro con el mismo XOR, y el checksum no lo ve. El original tampoco.
TEST(SimpleModulusTest, SomeChangedLaneBytesPassTheChecksum)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock cipher = BytesOf({0x67, 0x73, 0x20, 0xF9, 0x0C, 0xF1, 0xA7, 0xB1, 0x14, 0xC5, 0xF0});
    ASSERT_TRUE(SameBytes(EncryptBlock(pair.encryption, plain, 8), cipher));
    CipherBlock changed = cipher;
    changed[3] ^= std::byte{0x10};

    ExpectDecryptsTo(pair.decryption, changed, BytesOf({0xF8, 0x98, 0x2B, 0x03, 0x00, 0x40, 0x00, 0x00}), 8);
}

// El checksum es de los 8 bytes en claro: un cambio en el byte 9, el del largo útil, descifra igual
// y da otro largo. Que no pase de 8 lo mira el paquete.
TEST(SimpleModulusTest, TheChecksumDoesNotCoverTheUsefulSize)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);
    cipher[9] ^= std::byte{0x01};

    ExpectDecryptsTo(pair.decryption, cipher, plain, 9);
}

// El largo útil se escribe y se lee entero, también uno que un paquete no usa: 0, 9, 0x18 (que con
// 4 bits sería 8) y 0xFF. Que esté entre 1 y 8 lo mira el paquete (CL2.3, PR B).
TEST(SimpleModulusTest, KeepsAnyUsefulSize)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});

    for (const std::uint8_t useful : {std::uint8_t{0}, std::uint8_t{9}, std::uint8_t{0x18}, std::uint8_t{0xFF}})
    {
        ExpectDecryptsTo(pair.decryption, EncryptBlock(pair.encryption, plain, useful), plain, useful);
    }
}

// Un carril cifrado con sus 18 bits en 1, 0x3FFFF, más grande que su módulo: un servidor bueno no lo
// manda, pero el original lo multiplica igual. Va en el carril 3, que no pasa por la segunda pasada
// y llega a la multiplicación como vino: 0x50F2 * 0x3FFFF pasa de 32 bits, y el producto da la
// vuelta antes del `%`. La cola se armó para que el checksum coincida. Con el producto en 64 bits,
// los bytes 6 y 7 darían otra cosa y el checksum fallaría.
TEST(SimpleModulusTest, DecryptWrapsTheProductLikeTheImul)
{
    const CipherBlock cipher = BytesOf({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0x52, 0x67});
    const PlainBlock expected = BytesOf({0x29, 0x27, 0xF3, 0x8A, 0x7E, 0x97, 0x21, 0x20});

    ExpectDecryptsTo(Pair17().decryption, cipher, expected, 8);
}

// Al cifrar pasa lo mismo con una clave de más de 16 bits: 0xFFFF * 0x3FFF0 pasa de 32 bits. Las
// claves reales no llegan (GoldenSimpleModulusKeyFileTest), pero el original no lo mira.
TEST(SimpleModulusTest, EncryptWrapsTheProductLikeTheImul)
{
    const EncryptionKey key = Encryption({0x3FFFB, 0x3FFFB, 0x3FFFB, 0x3FFFB}, {0x3FFF0, 1, 1, 1}, {0, 0, 0, 0});
    const PlainBlock plain = BytesOf({0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf({0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0xDC, 0xFC, 0xCD, 0xF8});

    EXPECT_TRUE(SameBytes(EncryptBlock(key, plain, 8), expected));
}

// Q-C41: un módulo en 0 haría dividir por cero; uno de 16 bits, como 0xFFFF, no alcanza para un
// carril en claro, que llega a 0xFFFF; y uno mayor que 0x40000 no entra en los 18 bits del carril
// cifrado. En cualquier carril, y en las dos clases de clave.
TEST(SimpleModulusTest, FromLanesRejectsAModulusThatDoesNotFit)
{
    const Lanes key{1, 1, 1, 1};
    const Lanes xorKey{0, 0, 0, 0};

    for (std::size_t lane = 0; lane < key.size(); ++lane)
    {
        for (const std::uint32_t modulus : {std::uint32_t{0}, std::uint32_t{0xFFFF}, std::uint32_t{0x40001}})
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
}

// Q-C41 también con el XOR: `xor ^ w ^ anterior` llega a `xor | 0xFFFF`, y si eso no es menor que
// el módulo, el `%` pierde el carril. Con los módulos de 17 bits de Pair17, cualquier XOR con el
// bit 16 prendido; con el de 18 bits del carril 0 de Pair18, 0x24063, 0x20000.
TEST(SimpleModulusTest, FromLanesRejectsAXorThatCanReachTheModulus)
{
    const Lanes pair17Moduli{0x1A47D, 0x13273, 0x1CEDF, 0x1E0FF};
    const Lanes key{1, 1, 1, 1};

    for (std::size_t lane = 0; lane < key.size(); ++lane)
    {
        Lanes xorKey{0, 0, 0, 0};
        xorKey[lane] = 0x10000;

        EXPECT_FALSE(EncryptionKey::FromLanes(pair17Moduli, key, xorKey).HasValue()) << "carril " << lane;
        EXPECT_FALSE(DecryptionKey::FromLanes(pair17Moduli, key, xorKey).HasValue()) << "carril " << lane;
    }
    EXPECT_FALSE(EncryptionKey::FromLanes({0x24063, 65537, 65537, 65537}, key, {0x20000, 0, 0, 0}).HasValue());
}

// Los bordes que sirven: el módulo más chico para un XOR de 16 bits, 0x10000; el más grande,
// 0x40000, con el XOR más grande que le entra, 0x3FFFF; y un XOR de 17 bits con un módulo de 18.
TEST(SimpleModulusTest, FromLanesAcceptsTheEdges)
{
    const Lanes moduli{0x10000, 0x40000, 0x24063, 0x1A47D};
    const Lanes key{1, 1, 1, 1};
    const Lanes xorKey{0xFFFF, 0x3FFFF, 0x1FFFF, 0xFFFF};

    EXPECT_TRUE(EncryptionKey::FromLanes(moduli, key, xorKey).HasValue());
    EXPECT_TRUE(DecryptionKey::FromLanes(moduli, key, xorKey).HasValue());
}

// Un XOR de más de 16 bits que pasa FromLanes se usa entero, como en el original: Pair18 con el XOR
// del carril 0 en 0x11357, contra encrypt_block. Con el XOR cortado a 16 bits saldría otra cosa.
// Los productos entran en 32 bits: 0x6AE1 * 0x1FFFF y 0x27BB * 0x24062.
TEST(SimpleModulusTest, UsesAWideXorWhole)
{
    const KeyPair pair = PairOf({0x24063, 0x3EC45, 0x2E467, 0x3AE38}, {0x6AE1, 0xBDB9, 0xE98F, 0xF5E3},
                                {0x27BB, 0x24B9, 0x1819, 0x235B}, {0x11357, 0x2468, 0x9BDF, 0xACE0});
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const CipherBlock expected = BytesOf({0xF0, 0xF8, 0x1A, 0x91, 0xF6, 0x21, 0x74, 0x53, 0x06, 0xCD, 0xF8});

    EXPECT_TRUE(SameBytes(EncryptBlock(pair.encryption, plain, 8), expected));
    ExpectDecryptsTo(pair.decryption, expected, plain, 8);
}

} // namespace
