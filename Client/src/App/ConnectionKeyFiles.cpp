#include "App/ConnectionKeyFiles.h"

#include "Core/PathText.h"
#include "Data/DataPath.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"

#include <cstddef>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::App
{
namespace
{
using Core::Unexpected;

// Lo que se lee de cada archivo como mucho. El cargador mira los primeros KeyFileSize bytes y lo
// que sigue no lo lee, como el original: con el tope, un archivo enorme no se lee entero.
constexpr std::size_t MaxKeyFileRead = 4096;

// El principio de `name` en `folder`, o qué pasó con él, sin su contenido.
Core::Expected<std::vector<std::byte>, std::string> ReadKeyFile(const std::filesystem::path& folder,
                                                                std::string_view name)
{
    std::error_code error;
    const std::optional<std::filesystem::path> path = Data::ResolveDataPath(folder, name, error);
    if (!path)
    {
        if (error)
        {
            return Unexpected(std::format("no se pudo buscar {}: {}", name, error.message()));
        }
        return Unexpected(std::format("no está {}", name));
    }
    const std::filesystem::file_status status = std::filesystem::status(*path, error);
    if (error)
    {
        return Unexpected(std::format("no se pudo leer {}: {}", name, error.message()));
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return Unexpected(std::format("{} no es un archivo", name));
    }
    std::ifstream file(*path, std::ios::binary);
    std::vector<char> text(MaxKeyFileRead);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.is_open() || file.bad())
    {
        return Unexpected(std::format("no se pudo leer {}", name));
    }
    text.resize(static_cast<std::size_t>(file.gcount()));
    const std::span<const std::byte> bytes = std::as_bytes(std::span(text));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

// Lee `name` y lo carga con `load` (LoadEncryptionKey o LoadDecryptionKey).
template <typename Key>
Core::Expected<Key, std::string>
LoadKey(const std::filesystem::path& folder, std::string_view name,
        Core::Expected<Key, Network::Crypto::CryptoError> (*load)(std::span<const std::byte>))
{
    const auto file = ReadKeyFile(folder, name);
    if (!file.HasValue())
    {
        return Unexpected(file.Error());
    }
    auto key = load(file.Value());
    if (!key.HasValue())
    {
        return Unexpected(std::format("{} no carga: {}", name, Network::Crypto::CryptoErrorText(key.Error())));
    }
    return std::move(key).Value();
}
} // namespace

// Se cargan las dos antes de decir nada, así el error dice lo que pasó con cada una.
Core::Expected<Network::ConnectionKeys, std::string>
LoadConnectionKeys(const std::optional<std::filesystem::path>& dataFolder)
{
    if (!dataFolder)
    {
        return Unexpected(std::format("con un servidor hacen falta las claves de la conexión, {} y {}, de la "
                                      "carpeta de datos: dala con [Data] Path en la config o con --data",
                                      EncryptionKeyFileName, DecryptionKeyFileName));
    }
    auto encryption = LoadKey(*dataFolder, EncryptionKeyFileName, Network::Crypto::LoadEncryptionKey);
    auto decryption = LoadKey(*dataFolder, DecryptionKeyFileName, Network::Crypto::LoadDecryptionKey);
    if (!encryption.HasValue() || !decryption.HasValue())
    {
        std::string problems;
        for (const std::string* problem : {encryption.HasValue() ? nullptr : &encryption.Error(),
                                           decryption.HasValue() ? nullptr : &decryption.Error()})
        {
            if (problem != nullptr)
            {
                problems += (problems.empty() ? "" : "; ") + *problem;
            }
        }
        return Unexpected(std::format("no cargan las claves de la conexión, {} y {}, de la carpeta de datos {}: {}",
                                      EncryptionKeyFileName, DecryptionKeyFileName, Core::PathText(*dataFolder),
                                      problems));
    }
    return Network::ConnectionKeys{std::move(encryption).Value(), std::move(decryption).Value()};
}
} // namespace Client::App
