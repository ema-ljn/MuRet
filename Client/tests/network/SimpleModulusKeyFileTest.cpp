// Client::Network::Crypto::LoadEncryptionKey y LoadDecryptionKey, con archivos de clave armados
// acá: el magic, el tamaño y los tres bloques con el XOR de la tabla. Los reales, en
// GoldenSimpleModulusKeyFileTest.

#include "Network/Crypto/SimpleModulusKeyFile.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace
{

using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::KeyFileMagic;
using Client::Network::Crypto::KeyFileSize;
using Client::Network::Crypto::KeyFileXor;
using Client::Network::Crypto::Lanes;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;

constexpr unsigned BitsPerByte = 8;

// Los tres bloques de un archivo, ya sin el XOR de la tabla.
const Lanes Modulus{0x1A47D, 0x13273, 0x1CEDF, 0x1E0FF};
const Lanes Key{0xE5E9, 0x4BB8, 0x4EC1, 0x8DD9};
const Lanes XorKey{0xA5A5, 0x5A5A, 0x0F0F, 0xF0F0};

void PutLittleEndian(std::vector<std::byte>& file, std::uint32_t value, std::size_t size)
{
    for (std::size_t k = 0; k < size; ++k)
    {
        file.push_back(static_cast<std::byte>(value >> (BitsPerByte * k)));
    }
}

// Un archivo de clave con `magic`, el tamaño `declared` y los tres bloques, cada u32 con el XOR de
// la tabla de su carril.
std::vector<std::byte> KeyFile(std::uint16_t magic, std::uint32_t declared, const Lanes& modulus = Modulus)
{
    std::vector<std::byte> file;
    PutLittleEndian(file, magic, sizeof(magic));
    PutLittleEndian(file, declared, sizeof(declared));
    for (const Lanes& block : {modulus, Key, XorKey})
    {
        for (std::size_t lane = 0; lane < block.size(); ++lane)
        {
            PutLittleEndian(file, block[lane] ^ KeyFileXor[lane], sizeof(block[lane]));
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

// La tabla como la lista 00686240-carga-clave.md, byte por byte: un bloque con sus bytes se lee como
// cuatro ceros, y con el bit de abajo de cada carril cambiado, como cuatro unos.
TEST(SimpleModulusKeyFileTest, TheTableIsTheOneAt0x6d84fc)
{
    // El magic 0x1112 y el tamaño 54 (0x36), little-endian.
    const std::vector<std::byte> header{std::byte{0x12}, std::byte{0x11}, std::byte{0x36},
                                        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    constexpr std::array<int, 16> Table{0x9B, 0xA7, 0x08, 0x3F, 0x87, 0xC2, 0x5C, 0xE2,
                                        0xB9, 0x7A, 0xD2, 0x93, 0xBF, 0xA7, 0xDE, 0x20};
    // Los módulos, las claves y los XOR; y los 4 bytes de cada carril, el de abajo primero.
    constexpr std::size_t BlockCount = 3;
    constexpr std::size_t BytesPerLane = 4;
    std::vector<std::byte> file = header;
    for (std::size_t block = 0; block < BlockCount; ++block)
    {
        for (std::size_t k = 0; k < Table.size(); ++k)
        {
            // El primer bloque, el de los módulos, con el bit 0 de cada carril cambiado.
            const int flip = (block == 0 && k % BytesPerLane == 0) ? 0x01 : 0x00;
            file.push_back(static_cast<std::byte>(Table[k] ^ flip));
        }
    }

    const auto key = LoadEncryptionKey(file);

    ASSERT_TRUE(key.HasValue());
    EXPECT_EQ(key.Value().Modulus(), (Lanes{1, 1, 1, 1}));
    EXPECT_EQ(key.Value().Key(), (Lanes{0, 0, 0, 0}));
    EXPECT_EQ(key.Value().XorKey(), (Lanes{0, 0, 0, 0}));
}

// Otro magic (también el mismo con los bytes al revés), otro tamaño declarado, un archivo corto y
// uno vacío. El original tampoco carga los dos primeros; uno corto lo carga con lo que había en la
// pila (Q-C41).
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

// Lo que sigue a los 54 bytes no se lee, como en el original, que lee la cabecera y los bloques y
// cierra el archivo.
TEST(SimpleModulusKeyFileTest, IgnoresWhatFollowsTheBlocks)
{
    std::vector<std::byte> file = GoodKeyFile();
    file.insert(file.end(), 10, std::byte{0xEE});

    const auto key = LoadDecryptionKey(file);

    ASSERT_TRUE(key.HasValue());
    EXPECT_EQ(key.Value().Modulus(), Modulus);
    EXPECT_EQ(key.Value().XorKey(), XorKey);
}

// Q-C41: un archivo bien formado con un módulo que no sirve es InvalidKey, y no InvalidKeyFile.
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

} // namespace
