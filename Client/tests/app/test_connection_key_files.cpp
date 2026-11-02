#include "App/ConnectionKeyFiles.h"

#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"

#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

// Client::App::LoadConnectionKeys: Enc1.dat y Dec2.dat de la carpeta de datos (el plan de la CL4,
// CL4.4, PR B). Sin archivos de clave sintéticos (D-C7): los casos que fallan usan una carpeta
// temporal vacía o con un archivo que no es una clave, y los que cargan, la carpeta golden. Los
// errores se imprimen enteros porque nunca traen un byte de la clave; las claves se comparan con
// EXPECT_TRUE, que no imprime sus valores.
namespace
{
using Client::App::DecryptionKeyFileName;
using Client::App::EncryptionKeyFileName;
using Client::App::LoadConnectionKeys;
using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::CryptoErrorText;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;
using Client::Tests::LoadKeyFile;
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
