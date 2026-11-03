#include "Data/Terrain/TerrainObjects.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/K16.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Data::Terrain
{
namespace
{
using Core::ByteReader;

// Un float: 4 bytes little-endian que std::bit_cast pasa a float sin tocar un bit, también si son
// un NaN. std::nullopt si se acabó el archivo.
std::optional<float> ReadFloat(ByteReader& reader)
{
    const std::optional<std::uint32_t> bits = reader.ReadU32Le();
    if (!bits.has_value())
    {
        return std::nullopt;
    }
    return std::bit_cast<float>(*bits);
}

// Lee los floats de `values`, uno detrás del otro. Devuelve false si se acaba el archivo antes.
bool ReadFloats(ByteReader& reader, std::span<float> values)
{
    for (float& value : values)
    {
        const std::optional<float> read = ReadFloat(reader);
        if (!read.has_value())
        {
            return false;
        }
        value = *read;
    }
    return true;
}

// Lee el objeto que empieza donde está `reader`, o std::nullopt si no entra entero.
std::optional<TerrainObject> ReadObject(ByteReader& reader)
{
    const std::optional<std::uint16_t> type = reader.ReadU16Le();
    if (!type.has_value())
    {
        return std::nullopt;
    }

    TerrainObject object;
    object.type = std::bit_cast<std::int16_t>(*type);
    if (!ReadFloats(reader, object.position) || !ReadFloats(reader, object.angle))
    {
        return std::nullopt;
    }
    const std::optional<float> scale = ReadFloat(reader);
    if (!scale.has_value())
    {
        return std::nullopt;
    }
    object.scale = *scale;
    return object;
}
} // namespace

Core::Expected<std::vector<TerrainObject>, DataError> LoadTerrainObjects(std::span<const std::byte> file,
                                                                         std::uint8_t world)
{
    if (file.size() < TerrainObjectsHeaderSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    // Se descifra todo, como en el original. Hasta leer la cabecera no se sabe cuántos objetos hay.
    std::vector<std::byte> plain(file.begin(), file.end());
    DecryptK16(plain);
    const std::span<const std::byte> decrypted = plain;

    // La cabecera ya se midió arriba: la cantidad siempre está, y el 0 no se usa nunca.
    ByteReader header(decrypted.subspan(TerrainObjectsCountOffset));
    const std::int16_t count = std::bit_cast<std::int16_t>(header.ReadU16Le().value_or(0));

    std::vector<TerrainObject> objects;
    ByteReader reader(decrypted.subspan(TerrainObjectsHeaderSize));
    for (int k = 0; k < count; ++k)
    {
        const std::optional<TerrainObject> object = ReadObject(reader);
        if (!object.has_value())
        {
            return Core::Unexpected(DataError::FileTooShort);
        }
        objects.push_back(*object);
    }

    if (std::to_integer<std::uint8_t>(decrypted[TerrainObjectsWorldOffset]) != world)
    {
        return Core::Unexpected(DataError::WrongWorld);
    }
    return objects;
}
} // namespace Client::Data::Terrain
