#include "Crypto/SimpleModulus.h"

#include "Crypto/CryptoError.h"
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

// With synthetic keys written here. The expected encrypted blocks come from the research repo's c3c4_reference.py
// encrypt_block, and the decrypted ones from decrypt_block.
namespace
{
using Client::Crypto::CipherBlockSize;
using Client::Crypto::CryptoError;
using Client::Crypto::DecryptBlock;
using Client::Crypto::DecryptedBlock;
using Client::Crypto::DecryptionKey;
using Client::Crypto::EncryptBlock;
using Client::Crypto::EncryptionKey;
using Client::Crypto::Lanes;
using Client::Crypto::MaxModulus;
using Client::Crypto::PlainBlockSize;
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

static_assert(!std::is_convertible_v<DecryptionKey, EncryptionKey>);
static_assert(!std::is_convertible_v<EncryptionKey, DecryptionKey>);

// Q-C41's limit as a number: 2^18.
static_assert(MaxModulus == 0x40000);

// In a std::array as long as what is written: with one less, the block is neither a PlainBlock nor a CipherBlock
// and does not compile.
template <std::size_t N> std::array<std::byte, N> BytesOf(const int (&values)[N])
{
    std::array<std::byte, N> bytes{};
    std::ranges::transform(values, bytes.begin(), [](int value) { return static_cast<std::byte>(value); });
    return bytes;
}

void ExpectDecryptsTo(const DecryptionKey& key, const CipherBlock& cipher, const PlainBlock& plain,
                      std::uint8_t usefulSize)
{
    const std::optional<DecryptedBlock> block = DecryptBlock(key, cipher);
    ASSERT_TRUE(block.has_value());
    EXPECT_TRUE(SameBytes(block->bytes, plain));
    EXPECT_EQ(block->usefulSize, usefulSize);
}

// mu-client's FIX_C3_SERIAL0, FIX_C3_RANDA5 and FIX_C3_SERIALFF blocks: the notice `C1 06 F1 03 06 00`, already with
// XOR32, with the serial instead of the size, 5 useful bytes and the padding. The `C3 0D` before the 11 bytes is the
// packet's.
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

// A single bit set, with the identity key. Lane 0 is 1 and each chains with the previous one already encrypted, so
// before the second pass all four are 1. The second pass combines each with the next and leaves them 0, 0, 0 and 1.
// Lane 3 starts at the block's bit 54, and its bit 0 is the last of its first byte: bit 61, 0x04 in byte 7. The
// tail: the checksum is 0xF8 ^ 0x01 = 0xF9, and the length 8 ^ 0x3D ^ 0xF9 = 0xCC.
TEST(SimpleModulusTest, EncryptsASingleBitLikeTheReference)
{
    const PlainBlock plain = BytesOf({0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0xCC, 0xF9});
    const KeyPair identity = IdentityPair();

    EXPECT_TRUE(SameBytes(EncryptBlock(identity.encryption, plain, 8), expected));
    ExpectDecryptsTo(identity.decryption, expected, plain, 8);
}

// A lane's bits 16 and 17, which the identity key never sets and a round trip misses if written and read in the same
// wrong order. With this block encrypted lane 0 has only bit 16 and lane 1 only bit 17: after the low 16 bits comes
// 17 then 16, so byte 2 starts with 0 and 1 (`0x50`).
TEST(SimpleModulusTest, WritesBit17BeforeBit16)
{
    const KeyPair pair = PairOf({0x3FFFB, 0x3FFFB, 0x3FFFB, 0x3FFFB}, {0x6666, 0x5555, 0x4444, 0x3C3C},
                                {10, 12, 15, 17}, {0x1234, 0x5678, 0x9ABC, 0xDEF0});
    const PlainBlock plain = BytesOf({0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf({0xE2, 0x32, 0x50, 0x6E, 0xA6, 0x98, 0x95, 0x49, 0x10, 0xDD, 0xE8});

    EXPECT_TRUE(SameBytes(EncryptBlock(pair.encryption, plain, 8), expected));
    ExpectDecryptsTo(pair.decryption, expected, plain, 8);
}

// A full block and one with 4 useful bytes and the padding at 0.
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

// Split into blocks of 8 with the last padded with zeros, like c3c4_reference.py's encrypt.
TEST(SimpleModulusTest, DecryptUndoesEncryptFrom1To64Bytes)
{
    constexpr std::uint32_t Rounds = 500;
    constexpr std::uint32_t MaxBytes = 64; // ocho bloques
    constexpr std::uint32_t ByteValues = 256;
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

// The checksum is one byte: 0xF8 XOR the 8 plain bytes. A change in byte 10, the checksum's, always shows, since the
// plain bytes do not change and that byte does.
TEST(SimpleModulusTest, AChangedChecksumByteAlwaysFails)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    const CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);
    ASSERT_TRUE(DecryptBlock(pair.decryption, cipher).has_value());
    constexpr std::size_t ChecksumByte = CipherBlockSize - 1;
    constexpr unsigned BitsPerByte = 8; // each is flipped, one at a time

    for (unsigned bit = 0; bit < BitsPerByte; ++bit)
    {
        CipherBlock changed = cipher;
        changed[ChecksumByte] ^= std::byte{0x01} << bit;

        EXPECT_FALSE(DecryptBlock(pair.decryption, changed).has_value()) << "bit " << bit;
    }
}

// A change in the lanes' bytes gives another plain block, whose XOR is almost always different: with this block,
// bytes 0 and 4 changed fail. In general about one in 256 passes (next test).
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

// Block 08 00 00 00 00 00 00 00 with Pair17 and bit 4 of byte 3 flipped decrypts to another plain block with the same
// XOR, and the checksum misses it. The original does too.
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

// The checksum covers the 8 plain bytes: a change in byte 9, the useful length, still decrypts and gives another
// length. The packet checks it is at most 8.
TEST(SimpleModulusTest, TheChecksumDoesNotCoverTheUsefulSize)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    CipherBlock cipher = EncryptBlock(pair.encryption, plain, 8);
    cipher[9] ^= std::byte{0x01};

    ExpectDecryptsTo(pair.decryption, cipher, plain, 9);
}

// 0, 9, 0x18 (which with 4 bits would be 8) and 0xFF. The packet checks it is between 1 and 8.
TEST(SimpleModulusTest, KeepsAnyUsefulSize)
{
    const KeyPair pair = Pair17();
    const PlainBlock plain = BytesOf({0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});

    for (const std::uint8_t useful : {std::uint8_t{0}, std::uint8_t{9}, std::uint8_t{0x18}, std::uint8_t{0xFF}})
    {
        ExpectDecryptsTo(pair.decryption, EncryptBlock(pair.encryption, plain, useful), plain, useful);
    }
}

// An encrypted lane with its 18 bits set, 0x3FFFF, larger than its modulus: a good server does not send it, but the
// original multiplies it anyway. It goes in lane 3, which skips the second pass and reaches the multiplication as it
// came: 0x50F2 * 0x3FFFF exceeds 32 bits and wraps before the `%`. The tail was built so the checksum matches; with
// a 64-bit product bytes 6 and 7 would differ and the checksum would fail.
TEST(SimpleModulusTest, DecryptWrapsTheProductLikeTheImul)
{
    const CipherBlock cipher = BytesOf({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0x52, 0x67});
    const PlainBlock expected = BytesOf({0x29, 0x27, 0xF3, 0x8A, 0x7E, 0x97, 0x21, 0x20});

    ExpectDecryptsTo(Pair17().decryption, cipher, expected, 8);
}

// The same when encrypting with a key over 16 bits: 0xFFFF * 0x3FFF0 exceeds 32 bits. The real keys do not get there
// (test_golden_simple_modulus_key_file.cpp), but the original does not check.
TEST(SimpleModulusTest, EncryptWrapsTheProductLikeTheImul)
{
    const EncryptionKey key = Encryption({0x3FFFB, 0x3FFFB, 0x3FFFB, 0x3FFFB}, {0x3FFF0, 1, 1, 1}, {0, 0, 0, 0});
    const PlainBlock plain = BytesOf({0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const CipherBlock expected = BytesOf({0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0xDC, 0xFC, 0xCD, 0xF8});

    EXPECT_TRUE(SameBytes(EncryptBlock(key, plain, 8), expected));
}

// Q-C41: a modulus of 0 would divide by zero; a 16-bit one like 0xFFFF cannot hold a plain lane, which reaches 0xFFFF;
// and one over 0x40000 does not fit the encrypted lane's 18 bits. In any lane, for both key kinds.
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

// Q-C41 with the XOR too: `xor ^ w ^ previous` reaches `xor | 0xFFFF`, and if that is not below the modulus the `%`
// loses the lane. With Pair17's 17-bit moduli, any XOR with bit 16 set; with Pair18's lane 0 18-bit one, 0x24063,
// 0x20000.
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

// The smallest modulus for a 16-bit XOR, 0x10000; the largest, 0x40000, with the largest XOR it holds, 0x3FFFF; and a
// 17-bit XOR with an 18-bit modulus.
TEST(SimpleModulusTest, FromLanesAcceptsTheEdges)
{
    const Lanes moduli{0x10000, 0x40000, 0x24063, 0x1A47D};
    const Lanes key{1, 1, 1, 1};
    const Lanes xorKey{0xFFFF, 0x3FFFF, 0x1FFFF, 0xFFFF};

    EXPECT_TRUE(EncryptionKey::FromLanes(moduli, key, xorKey).HasValue());
    EXPECT_TRUE(DecryptionKey::FromLanes(moduli, key, xorKey).HasValue());
}

// A XOR over 16 bits that passes FromLanes is used whole, like in the original: Pair18 with lane 0's XOR at 0x11357,
// against encrypt_block. Cut to 16 bits it would give something else. The products fit in 32 bits: 0x6AE1 * 0x1FFFF
// and 0x27BB * 0x24062.
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
