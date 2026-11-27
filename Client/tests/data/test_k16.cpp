#include "Crypto/K16.h"

#include "support/BytePattern.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace
{
using Client::Crypto::DecryptK16;
using Client::Tests::BytePattern;
using Client::Tests::EncryptK16;

// By hand, with two encrypted zero bytes:
//   plain[0] = (D1 ^ 00) - 5E = 73, and the previous becomes 00 + 3D = 3D;
//   plain[1] = (73 ^ 00) - 3D = 36.
// If the previous came from the plain byte (73 + 3D = B0), the second would give C3.
TEST(K16Test, TwoBytesByHand)
{
    std::vector<std::byte> bytes(2);

    DecryptK16(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0x73}, std::byte{0x36}}));
}

// The key restarts at byte 16 but the seed does not: over zeros, byte 16 is D1 - 3D = 94, not byte 0's 73.
TEST(K16Test, TheKeyRepeatsEvery16Bytes)
{
    std::vector<std::byte> bytes(17);

    DecryptK16(bytes);

    EXPECT_EQ(bytes[0], std::byte{0x73});
    EXPECT_EQ(bytes[16], std::byte{0x94});
}

// The real EncTerrain1.att's encrypted header, 8B E5 24 92 (005f8390-encterrain-att.md, in the research repo). K16
// leaves FC CE 54 03, which the .att's XOR3 turns into 00 01 FF FF.
TEST(K16Test, MatchesTheResearchVector)
{
    std::vector<std::byte> bytes = {std::byte{0x8B}, std::byte{0xE5}, std::byte{0x24}, std::byte{0x92}};

    DecryptK16(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0xFC}, std::byte{0xCE}, std::byte{0x54}, std::byte{0x03}}));
}

TEST(K16Test, GivesBackWhatTheExporterEncrypted)
{
    const std::vector<std::byte> plain = BytePattern(300, 37, 11);
    std::vector<std::byte> bytes = EncryptK16(plain);
    ASSERT_NE(bytes, plain);

    DecryptK16(bytes);

    EXPECT_EQ(bytes, plain);
}

// The second half decrypted alone, with the initial seed, is wrong in its first byte only: from the second on,
// `previous` comes from an encrypted byte in the file, and since the half starts at a multiple of 16 the key lines up.
// If `previous` came from the plain byte, the first error would carry to all of them.
TEST(K16Test, ASecondHalfAloneIsWrongOnlyInItsFirstByte)
{
    const std::vector<std::byte> plain = BytePattern(32, 37, 11);
    const std::vector<std::byte> encrypted = EncryptK16(plain);
    std::vector<std::byte> secondHalf(encrypted.begin() + 16, encrypted.end());

    DecryptK16(secondHalf);

    EXPECT_NE(secondHalf[0], plain[16]);
    EXPECT_EQ(std::vector(secondHalf.begin() + 1, secondHalf.end()), std::vector(plain.begin() + 17, plain.end()));
}

// cipher[i] = (plain[i] + previous) ^ K16Key[i % 16], previous = cipher[i] + 3D, the first previous 5E. Bytes 16 and
// 17 take the key's first two bytes again, with the running previous.
TEST(K16Test, EncryptsEighteenZerosByHand)
{
    std::vector<std::byte> bytes(18);

    Client::Crypto::EncryptK16(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0x8F}, std::byte{0xBF}, std::byte{0xAE}, std::byte{0x1D}, std::byte{0x88},
                                  std::byte{0x5F}, std::byte{0x57}, std::byte{0xB3}, std::byte{0xCE}, std::byte{0xA4},
                                  std::byte{0xB8}, std::byte{0xC4}, std::byte{0x36}, std::byte{0xC0}, std::byte{0x1A},
                                  std::byte{0xF5}, std::byte{0xE3}, std::byte{0x53}}));
}

// FF + 5E wraps to 5D, and 5D ^ D1 = 8C. Then (01 + C9) ^ 73 = B9 and (80 + F6) ^ 52 = 24.
TEST(K16Test, EncryptWrapsInEightBits)
{
    std::vector<std::byte> bytes = {std::byte{0xFF}, std::byte{0x01}, std::byte{0x80}};

    Client::Crypto::EncryptK16(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0x8C}, std::byte{0xB9}, std::byte{0x24}}));
}

TEST(K16Test, DecryptUndoesEncrypt)
{
    const std::vector<std::byte> plain = BytePattern(1000, 37, 11);
    std::vector<std::byte> bytes = plain;

    Client::Crypto::EncryptK16(bytes);
    DecryptK16(bytes);

    EXPECT_EQ(bytes, plain);
}
} // namespace
