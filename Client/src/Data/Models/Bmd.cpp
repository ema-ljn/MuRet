#include "Data/Models/Bmd.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/K16.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Data::Models
{
namespace
{
using Core::ByteReader;
using Core::Unexpected;

template <typename T> using Read = Core::Expected<T, DataError>;

// Un hueso que no es dummy lleva este byte en 0 (05).
constexpr std::uint8_t RealBoneByte = 0;

// Las tres cantidades de la cabecera del modelo, en el orden del archivo.
struct ModelCounts
{
    std::size_t meshes = 0;
    std::size_t bones = 0;
    std::size_t actions = 0;
};

// Las cinco cantidades de la cabecera de una malla, en el orden del archivo.
struct MeshCounts
{
    std::size_t vertices = 0;
    std::size_t normals = 0;
    std::size_t texCoords = 0;
    std::size_t triangles = 0;
    std::int16_t texture = 0;
};

std::optional<std::int16_t> ReadInt16(ByteReader& reader)
{
    const std::optional<std::uint16_t> bits = reader.ReadU16Le();
    if (!bits.has_value())
    {
        return std::nullopt;
    }
    return std::bit_cast<std::int16_t>(*bits);
}

// Una cantidad del archivo: un int16 que no puede ser negativo.
Read<std::size_t> ReadCount(ByteReader& reader)
{
    const std::optional<std::int16_t> count = ReadInt16(reader);
    if (!count.has_value())
    {
        return Unexpected(DataError::FileTooShort);
    }
    if (*count < 0)
    {
        return Unexpected(DataError::InvalidData);
    }
    return static_cast<std::size_t>(*count);
}

// Tres floats: 4 bytes little-endian cada uno, que std::bit_cast pasa a float sin tocar un bit.
std::optional<std::array<float, 3>> ReadVector(ByteReader& reader)
{
    std::array<float, 3> vector{};
    for (float& value : vector)
    {
        const std::optional<std::uint32_t> bits = reader.ReadU32Le();
        if (!bits.has_value())
        {
            return std::nullopt;
        }
        value = std::bit_cast<float>(*bits);
    }
    return vector;
}

// Un nombre de largo fijo, hasta el primer 0 (o entero, si no tiene).
std::optional<std::string> ReadName(ByteReader& reader)
{
    const std::optional<std::span<const std::byte>> bytes = reader.ReadBytes(BmdNameSize);
    if (!bytes.has_value())
    {
        return std::nullopt;
    }
    const auto end = std::ranges::find(*bytes, std::byte{0});
    std::string name;
    for (auto it = bytes->begin(); it != end; ++it)
    {
        name.push_back(static_cast<char>(*it));
    }
    return name;
}

// Lee `count` elementos de `elementSize` bytes con `readOne`, sin pedir memoria antes de saber que
// entran: una cantidad grande en un archivo corto no reserva nada. Lo único que no ocupa bytes son
// los BoneKeys de las acciones sin keys, y esos los cuenta ReadBone.
template <typename T, typename ReadOne>
std::optional<std::vector<T>> ReadEach(ByteReader& reader, std::size_t count, std::size_t elementSize, ReadOne readOne)
{
    if (count > reader.Remaining() / elementSize)
    {
        return std::nullopt;
    }
    std::vector<T> elements;
    elements.reserve(count);
    for (std::size_t k = 0; k < count; ++k)
    {
        std::optional<T> element = readOne(reader);
        if (!element.has_value())
        {
            return std::nullopt;
        }
        elements.push_back(std::move(*element));
    }
    return elements;
}

std::optional<Vertex> ReadVertex(ByteReader& reader)
{
    const std::optional<std::int16_t> bone = ReadInt16(reader);
    const std::optional<std::span<const std::byte>> padding = reader.ReadBytes(sizeof(std::int16_t));
    const std::optional<std::array<float, 3>> position = ReadVector(reader);
    if (!bone.has_value() || !padding.has_value() || !position.has_value())
    {
        return std::nullopt;
    }
    return Vertex{*bone, *position};
}

std::optional<Normal> ReadNormal(ByteReader& reader)
{
    const std::optional<std::int16_t> bone = ReadInt16(reader);
    const std::optional<std::span<const std::byte>> padding = reader.ReadBytes(sizeof(std::int16_t));
    const std::optional<std::array<float, 3>> normal = ReadVector(reader);
    const std::optional<std::int16_t> vertex = ReadInt16(reader);
    const std::optional<std::span<const std::byte>> tail = reader.ReadBytes(sizeof(std::int16_t));
    if (!bone.has_value() || !padding.has_value() || !normal.has_value() || !vertex.has_value() || !tail.has_value())
    {
        return std::nullopt;
    }
    return Normal{*bone, *normal, *vertex};
}

std::optional<TexCoord> ReadTexCoord(ByteReader& reader)
{
    const std::optional<std::uint32_t> u = reader.ReadU32Le();
    const std::optional<std::uint32_t> v = reader.ReadU32Le();
    if (!u.has_value() || !v.has_value())
    {
        return std::nullopt;
    }
    return TexCoord{std::bit_cast<float>(*u), std::bit_cast<float>(*v)};
}

// Los cuatro int16 de un juego de índices, que empieza en `offset` del triángulo.
std::array<std::int16_t, 4> ReadIndices(std::span<const std::byte> triangle, std::size_t offset)
{
    ByteReader reader(triangle.subspan(offset));
    std::array<std::int16_t, 4> indices{};
    for (std::int16_t& index : indices)
    {
        index = ReadInt16(reader).value_or(0);
    }
    return indices;
}

// Un triángulo entero, de 64 bytes, del que se leen el tipo y los tres juegos de índices.
std::optional<Triangle> ReadTriangle(ByteReader& reader)
{
    const std::optional<std::span<const std::byte>> bytes = reader.ReadBytes(BmdTriangleSize);
    if (!bytes.has_value())
    {
        return std::nullopt;
    }
    Triangle triangle;
    triangle.polygon = std::to_integer<std::uint8_t>((*bytes)[0]);
    triangle.vertices = ReadIndices(*bytes, BmdTriangleVerticesOffset);
    triangle.normals = ReadIndices(*bytes, BmdTriangleNormalsOffset);
    triangle.texCoords = ReadIndices(*bytes, BmdTriangleTexCoordsOffset);
    return triangle;
}

Read<MeshCounts> ReadMeshCounts(ByteReader& reader)
{
    MeshCounts counts;
    for (std::size_t* count : {&counts.vertices, &counts.normals, &counts.texCoords, &counts.triangles})
    {
        const Read<std::size_t> read = ReadCount(reader);
        if (!read.HasValue())
        {
            return Unexpected(read.Error());
        }
        *count = read.Value();
    }
    const std::optional<std::int16_t> texture = ReadInt16(reader);
    if (!texture.has_value())
    {
        return Unexpected(DataError::FileTooShort);
    }
    counts.texture = *texture;
    return counts;
}

Read<Mesh> ReadMesh(ByteReader& reader)
{
    const Read<MeshCounts> counts = ReadMeshCounts(reader);
    if (!counts.HasValue())
    {
        return Unexpected(counts.Error());
    }
    std::optional<std::vector<Vertex>> vertices =
        ReadEach<Vertex>(reader, counts.Value().vertices, BmdVertexSize, ReadVertex);
    std::optional<std::vector<Normal>> normals =
        ReadEach<Normal>(reader, counts.Value().normals, BmdNormalSize, ReadNormal);
    std::optional<std::vector<TexCoord>> texCoords =
        ReadEach<TexCoord>(reader, counts.Value().texCoords, BmdTexCoordSize, ReadTexCoord);
    std::optional<std::vector<Triangle>> triangles =
        ReadEach<Triangle>(reader, counts.Value().triangles, BmdTriangleSize, ReadTriangle);
    std::optional<std::string> textureName = ReadName(reader);
    // Una parte que no entró no avanza el lector, y las que siguen leen corrido: el resultado no se
    // usa, porque falta la anterior.
    if (!vertices || !normals || !texCoords || !triangles || !textureName)
    {
        return Unexpected(DataError::FileTooShort);
    }
    return Mesh{counts.Value().texture, std::move(*vertices),  std::move(*normals),
                std::move(*texCoords),  std::move(*triangles), std::move(*textureName)};
}

Read<Action> ReadAction(ByteReader& reader)
{
    const Read<std::size_t> keyCount = ReadCount(reader);
    if (!keyCount.HasValue())
    {
        return Unexpected(keyCount.Error());
    }
    const std::optional<std::uint8_t> lock = reader.ReadU8();
    if (!lock.has_value())
    {
        return Unexpected(DataError::FileTooShort);
    }
    Action action;
    action.keyCount = static_cast<std::int16_t>(keyCount.Value());
    action.lockPositions = *lock != 0;
    if (!action.lockPositions)
    {
        return action;
    }
    auto positions = ReadEach<std::array<float, 3>>(reader, keyCount.Value(), BmdVectorSize, ReadVector);
    if (!positions.has_value())
    {
        return Unexpected(DataError::FileTooShort);
    }
    action.lockedPositions = std::move(*positions);
    return action;
}

// Las keys de un hueso en una acción de `keyCount` keys: todas las posiciones y después todas las
// rotaciones.
std::optional<BoneKeys> ReadBoneKeys(ByteReader& reader, std::size_t keyCount)
{
    std::optional<std::vector<std::array<float, 3>>> positions =
        ReadEach<std::array<float, 3>>(reader, keyCount, BmdVectorSize, ReadVector);
    std::optional<std::vector<std::array<float, 3>>> rotations =
        ReadEach<std::array<float, 3>>(reader, keyCount, BmdVectorSize, ReadVector);
    if (!positions || !rotations)
    {
        return std::nullopt;
    }
    return BoneKeys{std::move(*positions), std::move(*rotations)};
}

// Un hueso. Uno que se posa lleva un BoneKeys por acción y descuenta las acciones de
// `posedActionsLeft` (MaxPosedBoneActions) antes de pedir memoria para ellas.
Read<Bone> ReadBone(ByteReader& reader, const std::vector<Action>& actions, std::size_t& posedActionsLeft)
{
    const std::optional<std::uint8_t> dummy = reader.ReadU8();
    if (!dummy.has_value())
    {
        return Unexpected(DataError::FileTooShort);
    }
    Bone bone;
    bone.dummy = *dummy != RealBoneByte;
    if (bone.dummy)
    {
        return bone;
    }
    std::optional<std::string> name = ReadName(reader);
    const std::optional<std::int16_t> parent = ReadInt16(reader);
    if (!name || !parent)
    {
        return Unexpected(DataError::FileTooShort);
    }
    if (actions.size() > posedActionsLeft)
    {
        return Unexpected(DataError::InvalidData);
    }
    posedActionsLeft -= actions.size();
    bone.name = std::move(*name);
    bone.parent = *parent;
    bone.actions.reserve(actions.size());
    for (const Action& action : actions)
    {
        std::optional<BoneKeys> keys = ReadBoneKeys(reader, static_cast<std::size_t>(action.keyCount));
        if (!keys.has_value())
        {
            return Unexpected(DataError::FileTooShort);
        }
        bone.actions.push_back(std::move(*keys));
    }
    return bone;
}

// Las mallas, las acciones y los huesos, en ese orden: los huesos van después porque sus keys
// dependen de las acciones.
Read<Model> ReadParts(ByteReader& reader, Model model, const ModelCounts& counts)
{
    for (std::size_t k = 0; k < counts.meshes; ++k)
    {
        Read<Mesh> mesh = ReadMesh(reader);
        if (!mesh.HasValue())
        {
            return Unexpected(mesh.Error());
        }
        model.meshes.push_back(std::move(mesh).Value());
    }
    for (std::size_t k = 0; k < counts.actions; ++k)
    {
        Read<Action> action = ReadAction(reader);
        if (!action.HasValue())
        {
            return Unexpected(action.Error());
        }
        model.actions.push_back(std::move(action).Value());
    }
    std::size_t posedActionsLeft = MaxPosedBoneActions;
    for (std::size_t k = 0; k < counts.bones; ++k)
    {
        Read<Bone> bone = ReadBone(reader, model.actions, posedActionsLeft);
        if (!bone.HasValue())
        {
            return Unexpected(bone.Error());
        }
        model.bones.push_back(std::move(bone).Value());
    }
    model.trailingBytes = reader.Remaining();
    return model;
}

// El contenido, en claro: el nombre, las tres cantidades (mallas, huesos y acciones) y las partes.
Read<Model> ReadContent(std::uint8_t version, std::span<const std::byte> content)
{
    ByteReader reader(content);
    Model model;
    model.version = version;
    std::optional<std::string> name = ReadName(reader);
    if (!name.has_value())
    {
        return Unexpected(DataError::FileTooShort);
    }
    model.name = std::move(*name);
    ModelCounts counts;
    for (std::size_t* count : {&counts.meshes, &counts.bones, &counts.actions})
    {
        const Read<std::size_t> read = ReadCount(reader);
        if (!read.HasValue())
        {
            return Unexpected(read.Error());
        }
        *count = read.Value();
    }
    return ReadParts(reader, std::move(model), counts);
}
} // namespace

Core::Expected<Model, DataError> LoadBmd(std::span<const std::byte> file)
{
    if (file.size() < BmdPlainOffset)
    {
        return Unexpected(DataError::FileTooShort);
    }
    const auto version = std::to_integer<std::uint8_t>(file[BmdVersionOffset]);
    if (version != EncryptedBmdVersion)
    {
        return ReadContent(version, file.subspan(BmdPlainOffset));
    }

    ByteReader header(file.subspan(BmdEncryptedSizeOffset));
    const std::optional<std::uint32_t> size = header.ReadU32Le();
    if (!size.has_value() || *size > file.size() - BmdEncryptedOffset)
    {
        return Unexpected(DataError::FileTooShort);
    }
    // K16 desde el primer byte del bloque, sobre una copia: los bytes del que llama no se tocan.
    const std::span<const std::byte> block = file.subspan(BmdEncryptedOffset, *size);
    std::vector<std::byte> content(block.begin(), block.end());
    Terrain::DecryptK16(content);
    return ReadContent(version, content);
}
} // namespace Client::Data::Models
