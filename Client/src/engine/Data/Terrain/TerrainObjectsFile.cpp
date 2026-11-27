#include "Data/Terrain/TerrainObjectsFile.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Crypto/K16.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainObjects.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace Client::Data::Terrain
{
namespace
{
using Core::ByteReader;

bool ReadFloats(ByteReader& reader, std::span<float> values)
{
    for (float& value : values)
    {
        const std::optional<std::uint32_t> bits = reader.ReadU32Le();
        if (!bits.has_value())
        {
            return false;
        }
        value = std::bit_cast<float>(*bits);
    }
    return true;
}

std::optional<TerrainObject> ReadObject(ByteReader& reader)
{
    const std::optional<std::uint16_t> type = reader.ReadU16Le();
    if (!type.has_value())
    {
        return std::nullopt;
    }
    TerrainObject object;
    object.type = std::bit_cast<std::int16_t>(*type);
    if (!ReadFloats(reader, object.position) || !ReadFloats(reader, object.angle) ||
        !ReadFloats(reader, std::span(&object.scale, 1)))
    {
        return std::nullopt;
    }
    return object;
}

void AppendLittleEndian(std::vector<std::byte>& bytes, std::uint32_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
    }
}

void AppendFloats(std::vector<std::byte>& bytes, std::span<const float> values)
{
    for (const float value : values)
    {
        AppendLittleEndian(bytes, std::bit_cast<std::uint32_t>(value), sizeof(float));
    }
}
} // namespace

Core::Expected<TerrainObjectsFile, DataError> ReadTerrainObjectsFile(std::span<const std::byte> file)
{
    if (file.size() < TerrainObjectsHeaderSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    std::vector<std::byte> plain(file.begin(), file.end());
    Crypto::DecryptK16(plain);
    const std::span<const std::byte> decrypted = plain;

    ByteReader header(decrypted.subspan(TerrainObjectsCountOffset));
    const std::int16_t count = std::bit_cast<std::int16_t>(header.ReadU16Le().value_or(0));
    if (count < 0)
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    TerrainObjectsFile objects;
    objects.version = std::to_integer<std::uint8_t>(decrypted[0]);
    objects.world = std::to_integer<std::uint8_t>(decrypted[TerrainObjectsWorldOffset]);
    objects.objects.reserve(static_cast<std::size_t>(count));
    ByteReader reader(decrypted.subspan(TerrainObjectsHeaderSize));
    for (int k = 0; k < count; ++k)
    {
        const std::optional<TerrainObject> object = ReadObject(reader);
        if (!object.has_value())
        {
            return Core::Unexpected(DataError::FileTooShort);
        }
        objects.objects.push_back(*object);
    }
    const std::size_t end = TerrainObjectsHeaderSize + TerrainObjectSize * objects.objects.size();
    objects.tail.assign(decrypted.begin() + static_cast<std::ptrdiff_t>(end), decrypted.end());
    return objects;
}

Core::Expected<std::vector<std::byte>, DataError> WriteTerrainObjectsFile(const TerrainObjectsFile& objects)
{
    if (objects.objects.size() > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()))
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    std::vector<std::byte> file{std::byte{objects.version}, std::byte{objects.world}};
    file.reserve(TerrainObjectsHeaderSize + TerrainObjectSize * objects.objects.size() + objects.tail.size());
    AppendLittleEndian(file, static_cast<std::uint32_t>(objects.objects.size()), sizeof(std::int16_t));
    for (const TerrainObject& object : objects.objects)
    {
        AppendLittleEndian(file, std::bit_cast<std::uint16_t>(object.type), sizeof(object.type));
        AppendFloats(file, object.position);
        AppendFloats(file, object.angle);
        AppendFloats(file, std::span(&object.scale, 1));
    }
    file.insert(file.end(), objects.tail.begin(), objects.tail.end());
    Crypto::EncryptK16(file);
    return file;
}
} // namespace Client::Data::Terrain
