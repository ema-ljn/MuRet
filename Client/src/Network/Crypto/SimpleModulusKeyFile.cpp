#include "Network/Crypto/SimpleModulusKeyFile.h"

#include "Core/ByteReader.h"

#include <optional>

namespace Client::Network::Crypto
{
namespace
{
// Un bloque del archivo: un u32 por carril, cada uno con XOR del de la tabla. std::nullopt si no
// quedan bytes.
std::optional<Lanes> ReadBlock(Core::ByteReader& reader)
{
    Lanes block{};
    for (std::size_t lane = 0; lane < block.size(); ++lane)
    {
        const std::optional<std::uint32_t> value = reader.ReadU32Le();
        if (!value)
        {
            return std::nullopt;
        }
        block[lane] = *value ^ KeyFileXor[lane];
    }
    return block;
}

// Los dos formatos son el mismo: cambia para qué sirve el segundo bloque.
template <KeyRole Role> Core::Expected<SimpleModulusKey<Role>, CryptoError> LoadKey(std::span<const std::byte> file)
{
    // 0x686240 no mira cuánto mide el archivo: lee la cabecera y los bloques en el mismo buffer de
    // la pila, y un archivo corto deja lo que quedó del bloque anterior (Q-C41). Acá un archivo
    // corto no carga: el lector no tiene los bytes y ReadBlock da std::nullopt. Lo que sigue a los
    // 54 bytes no se lee.
    Core::ByteReader reader(file);
    const std::optional<std::uint16_t> magic = reader.ReadU16Le();
    const std::optional<std::uint32_t> declared = reader.ReadU32Le();
    if (magic != KeyFileMagic || declared != KeyFileSize)
    {
        return Core::Unexpected(CryptoError::InvalidKeyFile);
    }
    const std::optional<Lanes> modulus = ReadBlock(reader);
    const std::optional<Lanes> key = ReadBlock(reader);
    const std::optional<Lanes> xorKey = ReadBlock(reader);
    if (!modulus || !key || !xorKey)
    {
        return Core::Unexpected(CryptoError::InvalidKeyFile);
    }
    return SimpleModulusKey<Role>::FromLanes(*modulus, *key, *xorKey);
}
} // namespace

Core::Expected<EncryptionKey, CryptoError> LoadEncryptionKey(std::span<const std::byte> file)
{
    return LoadKey<KeyRole::Encrypt>(file);
}

Core::Expected<DecryptionKey, CryptoError> LoadDecryptionKey(std::span<const std::byte> file)
{
    return LoadKey<KeyRole::Decrypt>(file);
}
} // namespace Client::Network::Crypto
