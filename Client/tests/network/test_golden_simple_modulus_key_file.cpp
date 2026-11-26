#include "Crypto/SimpleModulus.h"
#include "Crypto/SimpleModulusKeyFile.h"

#include "support/FixedRandom.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

// The real keys: the client's Enc1.dat and Dec2.dat from the golden folder, the server's Dec1.dat and Enc2.dat from
// the wire folder. No test shows a key value, not even on failure: only the lane.
namespace
{
using Client::Crypto::DecryptBlock;
using Client::Crypto::DecryptedBlock;
using Client::Crypto::DecryptionKey;
using Client::Crypto::EncryptBlock;
using Client::Crypto::EncryptionKey;
using Client::Crypto::LoadDecryptionKey;
using Client::Crypto::LoadEncryptionKey;
using Client::Crypto::PlainBlockSize;
using Client::Tests::Below;
using Client::Tests::FixedSeed;
using Client::Tests::LoadKeyFile;
using Client::Tests::SameBytesWithoutValues;

constexpr std::uint64_t MaxPlainLane = 0xFFFF;
constexpr std::uint64_t MaxProduct = std::numeric_limits<std::uint32_t>::max();

struct KeyPair
{
    const char* name;
    EncryptionKey encryption;
    DecryptionKey decryption;
};

class GoldenSimpleModulusKeyFile : public Client::Tests::GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        // The client encrypts with Enc1.dat and the server decrypts with Dec1.dat; the server encrypts with Enc2.dat
        // and the client decrypts with Dec2.dat.
        const std::optional<EncryptionKey> enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        const std::optional<DecryptionKey> dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        const std::optional<EncryptionKey> enc2 = LoadKeyFile(WireFile("Enc2.dat"), LoadEncryptionKey);
        const std::optional<DecryptionKey> dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(enc1 && dec1 && enc2 && dec2);
        m_pairs = {{"Enc1.dat y Dec1.dat", *enc1, *dec1}, {"Enc2.dat y Dec2.dat", *enc2, *dec2}};
    }

    std::vector<KeyPair> m_pairs;
};

// In each lane the decryption key is the inverse of the encryption key: their product is 1 modulo m.
TEST_F(GoldenSimpleModulusKeyFile, EachPairHasInverseKeys)
{
    for (const KeyPair& pair : m_pairs)
    {
        for (std::size_t lane = 0; lane < pair.encryption.Modulus().size(); ++lane)
        {
            const std::uint64_t modulus = pair.encryption.Modulus()[lane];
            const std::uint64_t product =
                std::uint64_t{pair.encryption.Key()[lane]} * std::uint64_t{pair.decryption.Key()[lane]};

            EXPECT_TRUE(pair.decryption.Modulus()[lane] == modulus) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.decryption.XorKey()[lane] == pair.encryption.XorKey()[lane])
                << pair.name << ", carril " << lane;
            EXPECT_TRUE(product % modulus == 1) << pair.name << ", carril " << lane;
        }
    }
}

// What c3c4_reference.py assumes, and what decrypting needs to undo encrypting without a product wrapping: the
// modulus is over 16 bits, so a plain lane fits whole; the XOR is 16 bits; the encryption key times 0xFFFF fits in
// 32 bits; and the decryption key times the largest encrypted lane, m - 1, too.
TEST_F(GoldenSimpleModulusKeyFile, TheRealKeysFitTheLaneArithmetic)
{
    for (const KeyPair& pair : m_pairs)
    {
        for (std::size_t lane = 0; lane < pair.encryption.Modulus().size(); ++lane)
        {
            const std::uint64_t modulus = pair.encryption.Modulus()[lane];

            EXPECT_TRUE(modulus > MaxPlainLane) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.encryption.XorKey()[lane] <= MaxPlainLane) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.encryption.Key()[lane] * MaxPlainLane <= MaxProduct) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.decryption.Key()[lane] * (modulus - 1) <= MaxProduct) << pair.name << ", carril " << lane;
        }
    }
}

TEST_F(GoldenSimpleModulusKeyFile, WhatOneSideEncryptsTheOtherDecrypts)
{
    constexpr std::uint32_t BlocksPerPair = 2000; // 250 per useful length, 1 to 8
    constexpr std::uint32_t ByteValues = 256;
    std::mt19937 engine(FixedSeed);

    for (const KeyPair& pair : m_pairs)
    {
        for (std::uint32_t round = 0; round < BlocksPerPair; ++round)
        {
            std::array<std::byte, PlainBlockSize> plain{};
            for (std::byte& value : plain)
            {
                value = static_cast<std::byte>(Below(engine, ByteValues));
            }
            const auto useful = static_cast<std::uint8_t>(1 + round % PlainBlockSize);

            const std::optional<DecryptedBlock> decrypted =
                DecryptBlock(pair.decryption, EncryptBlock(pair.encryption, plain, useful));

            ASSERT_TRUE(decrypted.has_value()) << pair.name << ", vuelta " << round;
            EXPECT_TRUE(SameBytesWithoutValues(decrypted->bytes, plain)) << pair.name << ", vuelta " << round;
            EXPECT_EQ(decrypted->usefulSize, useful) << pair.name << ", vuelta " << round;
        }
    }
}
} // namespace
