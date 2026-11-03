#include "App/ConnectionKeyFiles.h"

#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"

#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/SimpleModulusKeys.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

// Client::App::LoadConnectionKeys: Enc1.dat y Dec2.dat de la carpeta de datos (el plan de la CL4,
// CL4.4, PR B). Ningún archivo de clave va a Git (D-C7): los casos que fallan usan una carpeta
// temporal vacía o con un archivo que no es una clave; los que cargan, dos archivos que el test
// escribe al correr con las claves sintéticas de SimpleModulusKeys.h, y la carpeta golden. Los
// errores se imprimen enteros porque nunca traen un byte de la clave; las claves se comparan con
// EXPECT_TRUE, que no imprime sus valores.
namespace
{
using Client::App::DecryptionKeyFileName;
using Client::App::EncryptionKeyFileName;
using Client::App::LoadConnectionKeys;
using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::CryptoErrorText;
using Client::Network::Crypto::KeyFileMagic;
using Client::Network::Crypto::KeyFileSize;
using Client::Network::Crypto::KeyFileXor;
using Client::Network::Crypto::Lanes;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;
using Client::Tests::LoadKeyFile;
using Client::Tests::Pair17;
using Client::Tests::Pair18;
using Client::Tests::TemporaryFolder;

bool Says(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

// Un archivo con `text`, que no es una clave.
void WriteFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}

// `count` bytes de `value`, en little-endian.
void AppendLe(std::string& bytes, std::uint32_t value, int count)
{
    for (int index = 0; index < count; ++index)
    {
        bytes.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
    }
}

// `key` como la guarda un archivo de clave (SimpleModulusKeyFile.h): el magic, el tamaño y los tres
// bloques, cada u32 con el XOR de su carril. Lo borra la carpeta temporal.
template <typename Key> void WriteKeyFile(const std::filesystem::path& path, const Key& key)
{
    std::string bytes;
    AppendLe(bytes, KeyFileMagic, 2);
    AppendLe(bytes, static_cast<std::uint32_t>(KeyFileSize), 4);
    for (const Lanes* block : {&key.Modulus(), &key.Key(), &key.XorKey()})
    {
        for (std::size_t lane = 0; lane < block->size(); ++lane)
        {
            AppendLe(bytes, (*block)[lane] ^ KeyFileXor[lane], 4);
        }
    }
    ASSERT_EQ(bytes.size(), KeyFileSize);
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    ASSERT_TRUE(file.good());
}

// Si dos claves tienen los mismos tres bloques.
template <typename Key> bool SameLanes(const Key& a, const Key& b)
{
    return a.Modulus() == b.Modulus() && a.Key() == b.Key() && a.XorKey() == b.XorKey();
}

// Sin carpeta de datos no hay de dónde sacarlas: el error nombra los dos archivos y cómo darla.
TEST(ConnectionKeyFilesTest, WithoutADataFolderNamesBoth)
{
    const auto loaded = LoadConnectionKeys(std::nullopt);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_TRUE(Says(loaded.Error(), EncryptionKeyFileName)) << loaded.Error();
    EXPECT_TRUE(Says(loaded.Error(), DecryptionKeyFileName)) << loaded.Error();
    EXPECT_TRUE(Says(loaded.Error(), "--data")) << loaded.Error();
}

// Una carpeta sin las claves: el error nombra los dos, cada uno con lo que le pasó.
TEST(ConnectionKeyFilesTest, AFolderWithoutThemNamesBoth)
{
    const TemporaryFolder folder("claves-vacia");

    const auto loaded = LoadConnectionKeys(folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_TRUE(Says(loaded.Error(), std::string("no está ") + std::string(EncryptionKeyFileName))) << loaded.Error();
    EXPECT_TRUE(Says(loaded.Error(), std::string("no está ") + std::string(DecryptionKeyFileName))) << loaded.Error();
}

// Un Enc1.dat que no es una clave dice por qué no carga, con el texto de CryptoErrorText, y el
// Dec2.dat que falta se dice también: los dos problemas en un mensaje.
TEST(ConnectionKeyFilesTest, AFileThatIsNotAKeySaysWhy)
{
    const TemporaryFolder folder("claves-rota");
    WriteFile(folder.Path() / EncryptionKeyFileName, "no es una clave");

    const auto loaded = LoadConnectionKeys(folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_TRUE(Says(loaded.Error(), CryptoErrorText(CryptoError::InvalidKeyFile))) << loaded.Error();
    EXPECT_TRUE(Says(loaded.Error(), std::string("no está ") + std::string(DecryptionKeyFileName))) << loaded.Error();
}

// Una carpeta con el nombre de la clave no es un archivo, y no se intenta leer.
TEST(ConnectionKeyFilesTest, AFolderInsteadOfAFileIsNamed)
{
    const TemporaryFolder folder("claves-carpeta");
    std::filesystem::create_directory(folder.Path() / DecryptionKeyFileName);

    const auto loaded = LoadConnectionKeys(folder.Path());

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_TRUE(Says(loaded.Error(), std::string(DecryptionKeyFileName) + " no es un archivo")) << loaded.Error();
}

// Con dos claves de pares distintos, escritas al correr el test: Enc1.dat da la de cifrar y
// Dec2.dat la de descifrar, cada una con los bloques de su archivo. Es el camino que carga, en CI
// también, donde la carpeta golden no está.
TEST(ConnectionKeyFilesTest, LoadsEachKeyFromItsFile)
{
    const TemporaryFolder folder("claves-sinteticas");
    const auto encryption = Pair17().encryption;
    const auto decryption = Pair18().decryption;
    ASSERT_NO_FATAL_FAILURE(WriteKeyFile(folder.Path() / EncryptionKeyFileName, encryption));
    ASSERT_NO_FATAL_FAILURE(WriteKeyFile(folder.Path() / DecryptionKeyFileName, decryption));

    const auto loaded = LoadConnectionKeys(folder.Path());

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_TRUE(SameLanes(loaded.Value().encryption, encryption));
    EXPECT_TRUE(SameLanes(loaded.Value().decryption, decryption));
}

class GoldenConnectionKeyFiles : public Client::Tests::GoldenDataTest
{
};

// Las de la carpeta de datos del laboratorio cargan, y son las mismas que da cada cargador con su
// archivo.
TEST_F(GoldenConnectionKeyFiles, LoadsTheKeysOfTheDataFolder)
{
    const auto encryption = LoadKeyFile(DataFile(EncryptionKeyFileName), LoadEncryptionKey);
    const auto decryption = LoadKeyFile(DataFile(DecryptionKeyFileName), LoadDecryptionKey);
    if (!encryption || !decryption)
    {
        FAIL() << "no cargan las claves de la carpeta golden";
    }

    const auto loaded = LoadConnectionKeys(m_dataRoot);

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_TRUE(SameLanes(loaded.Value().encryption, *encryption));
    EXPECT_TRUE(SameLanes(loaded.Value().decryption, *decryption));
}

// Como el original en Windows, se encuentran sin mirar mayúsculas: copiadas a una carpeta como
// ENC1.DAT y dec2.dat, cargan igual. Las copias se borran con la carpeta.
TEST_F(GoldenConnectionKeyFiles, FindsThemWithoutMindingCase)
{
    const auto encryptionFile = DataFile(EncryptionKeyFileName);
    const auto decryptionFile = DataFile(DecryptionKeyFileName);
    if (!encryptionFile || !decryptionFile)
    {
        FAIL() << "no están las claves en la carpeta golden";
    }
    const TemporaryFolder folder("claves-mayusculas");
    std::error_code error;
    std::filesystem::copy_file(*encryptionFile, folder.Path() / "ENC1.DAT", error);
    ASSERT_FALSE(error) << error.message();
    std::filesystem::copy_file(*decryptionFile, folder.Path() / "dec2.dat", error);
    ASSERT_FALSE(error) << error.message();

    const auto loaded = LoadConnectionKeys(folder.Path());

    EXPECT_TRUE(loaded.HasValue()) << (loaded.HasValue() ? std::string() : loaded.Error());
}
} // namespace
