#include "Crypto/SimpleModulusKeyFile.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Crypto
{
namespace
{
// One u32 per lane, each XOR the table's. std::nullopt if no bytes are left.
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

// Both formats are the same: only what the second block is for changes.
template <KeyRole Role> Core::Expected<SimpleModulusKey<Role>, CryptoError> LoadKey(std::span<const std::byte> file)
{
    // 0x686240 ignores the file's size: it reads the header and blocks into the same stack buffer, and a short file
    // leaves what remained of the previous block (Q-C41). Here a short file does not load: ReadBlock gives
    // std::nullopt. What follows the 54 bytes is not read.
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
} // namespace Client::Crypto
