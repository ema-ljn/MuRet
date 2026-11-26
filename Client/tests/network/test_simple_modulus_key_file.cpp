#include "Crypto/SimpleModulusKeyFile.h"

#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"
#include "support/LittleEndian.h"
#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

// With key files built here: the magic, the size and the three blocks with the table's XOR. The real ones are in
// test_golden_simple_modulus_key_file.cpp.
namespace
{
using Client::Crypto::CryptoError;
using Client::Crypto::KeyFileMagic;
using Client::Crypto::KeyFileSize;
using Client::Crypto::KeyFileXor;
using Client::Crypto::Lanes;
using Client::Crypto::LoadDecryptionKey;
using Client::Crypto::LoadEncryptionKey;
using Client::Tests::AppendLittleEndian;

// Without the table's XOR.
const Lanes Modulus{0x1A47D, 0x13273, 0x1CEDF, 0x1E0FF};
const Lanes Key{0xE5E9, 0x4BB8, 0x4EC1, 0x8DD9};
const Lanes XorKey{0xA5A5, 0x5A5A, 0x0F0F, 0xF0F0};

// Each u32 with its lane's table XOR.
std::vector<std::byte> KeyFile(std::uint16_t magic, std::uint32_t declared, const Lanes& modulus = Modulus)
{
    std::vector<std::byte> file;
    AppendLittleEndian(file, magic, sizeof(magic));
    AppendLittleEndian(file, declared, sizeof(declared));
    for (const Lanes& block : {modulus, Key, XorKey})
    {
        for (std::size_t lane = 0; lane < block.size(); ++lane)
        {
            AppendLittleEndian(file, block[lane] ^ KeyFileXor[lane], sizeof(block[lane]));
        }
    }
    return file;
}

std::vector<std::byte> GoodKeyFile()
{
    return KeyFile(KeyFileMagic, KeyFileSize);
}

TEST(SimpleModulusKeyFileTest, LoadsTheThreeBlocksWithoutTheTable)
{
    const std::vector<std::byte> file = GoodKeyFile();
    ASSERT_EQ(file.size(), KeyFileSize);

    const auto encryption = LoadEncryptionKey(file);
    const auto decryption = LoadDecryptionKey(file);

    ASSERT_TRUE(encryption.HasValue());
    ASSERT_TRUE(decryption.HasValue());
    EXPECT_EQ(encryption.Value().Modulus(), Modulus);
    EXPECT_EQ(encryption.Value().Key(), Key);
    EXPECT_EQ(encryption.Value().XorKey(), XorKey);
    EXPECT_EQ(decryption.Value().Modulus(), Modulus);
    EXPECT_EQ(decryption.Value().Key(), Key);
    EXPECT_EQ(decryption.Value().XorKey(), XorKey);
}

// The table as 00686240-carga-clave.md lists it, byte by byte: a block of its own bytes reads as four zeros, and with
// each lane's bit 16 flipped as four 0x10000, the smallest modulus that works (Q-C41).
TEST(SimpleModulusKeyFileTest, TheTableIsTheOneAt0x6d84fc)
{
    // Little-endian.
    const std::vector<std::byte> header{std::byte{0x12}, std::byte{0x11}, std::byte{0x36},
                                        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    // 0x6d84fc's table: the 4 bytes of each KeyFileXor lane, low first.
    constexpr std::array<int, 16> Table{0x9B, 0xA7, 0x08, 0x3F, 0x87, 0xC2, 0x5C, 0xE2,
                                        0xB9, 0x7A, 0xD2, 0x93, 0xBF, 0xA7, 0xDE, 0x20};
    // The moduli, the keys and the XORs; each lane's 4 bytes, low first.
    constexpr std::size_t BlockCount = 3;
    constexpr std::size_t BytesPerLane = 4;
    std::vector<std::byte> file = header;
    for (std::size_t block = 0; block < BlockCount; ++block)
    {
        for (std::size_t k = 0; k < Table.size(); ++k)
        {
            constexpr std::size_t ThirdByte = 2;
            const int flip = (block == 0 && k % BytesPerLane == ThirdByte) ? 0x01 : 0x00;
            file.push_back(static_cast<std::byte>(Table[k] ^ flip));
        }
    }

    const auto key = LoadEncryptionKey(file);

    ASSERT_TRUE(key.HasValue());
    EXPECT_EQ(key.Value().Modulus(), (Lanes{0x10000, 0x10000, 0x10000, 0x10000}));
    EXPECT_EQ(key.Value().Key(), (Lanes{0, 0, 0, 0}));
    EXPECT_EQ(key.Value().XorKey(), (Lanes{0, 0, 0, 0}));
}

// Also the same magic byte-swapped, and a declared size whose low two bytes give 54 (the size is a u32). The original
// does not load the first two either; a short one it loads with what was left in the stack buffer, the same for all
// three blocks (Q-C41).
TEST(SimpleModulusKeyFileTest, RejectsAnotherMagicAnotherSizeOrAShortFile)
{
    std::vector<std::byte> shortFile = GoodKeyFile();
    shortFile.pop_back();
    const struct
    {
        const char* name;
        std::vector<std::byte> file;
    } cases[] = {
        {"magic 0x1113", KeyFile(0x1113, KeyFileSize)},
        {"magic 0x1211", KeyFile(0x1211, KeyFileSize)},
        {"tamaño 70, el de cuatro bloques", KeyFile(KeyFileMagic, 70)},
        {"tamaño 53", KeyFile(KeyFileMagic, 53)},
        {"tamaño 0", KeyFile(KeyFileMagic, 0)},
        {"tamaño 54 + 0x10000", KeyFile(KeyFileMagic, KeyFileSize + 0x10000)},
        {"53 bytes", shortFile},
        {"vacío", {}},
    };

    for (const auto& rejected : cases)
    {
        const auto encryption = LoadEncryptionKey(rejected.file);
        const auto decryption = LoadDecryptionKey(rejected.file);

        ASSERT_FALSE(encryption.HasValue()) << rejected.name;
        ASSERT_FALSE(decryption.HasValue()) << rejected.name;
        EXPECT_EQ(encryption.Error(), CryptoError::InvalidKeyFile) << rejected.name;
        EXPECT_EQ(decryption.Error(), CryptoError::InvalidKeyFile) << rejected.name;
    }
}

// As in the original, which reads the header and the blocks and closes the file.
TEST(SimpleModulusKeyFileTest, IgnoresWhatFollowsTheBlocks)
{
    std::vector<std::byte> file = GoodKeyFile();
    file.insert(file.end(), 10, std::byte{0xEE});

    const auto key = LoadDecryptionKey(file);

    ASSERT_TRUE(key.HasValue());
    EXPECT_EQ(key.Value().Modulus(), Modulus);
    EXPECT_EQ(key.Value().XorKey(), XorKey);
}

// Q-C41: a well-formed file with a modulus that does not fit is InvalidKey, not InvalidKeyFile.
TEST(SimpleModulusKeyFileTest, AModulusThatDoesNotFitIsInvalidKey)
{
    for (const std::uint32_t modulus : {std::uint32_t{0}, std::uint32_t{0x40001}})
    {
        Lanes moduli = Modulus;
        moduli[2] = modulus;
        const std::vector<std::byte> file = KeyFile(KeyFileMagic, KeyFileSize, moduli);

        const auto encryption = LoadEncryptionKey(file);
        const auto decryption = LoadDecryptionKey(file);

        ASSERT_FALSE(encryption.HasValue()) << modulus;
        ASSERT_FALSE(decryption.HasValue()) << modulus;
        EXPECT_EQ(encryption.Error(), CryptoError::InvalidKey) << modulus;
        EXPECT_EQ(decryption.Error(), CryptoError::InvalidKey) << modulus;
    }
}

TEST(SimpleModulusKeyFileTest, TheLargestModulusLoads)
{
    Lanes moduli = Modulus;
    moduli[1] = 0x40000;

    const auto key = LoadEncryptionKey(KeyFile(KeyFileMagic, KeyFileSize, moduli));

    ASSERT_TRUE(key.HasValue());
    EXPECT_EQ(key.Value().Modulus(), moduli);
}
} // namespace
