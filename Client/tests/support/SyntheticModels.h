#pragma once

#include "Data/Models/Bmd.h"
#include "Data/Models/Model.h"
#include "support/LittleEndian.h"
#include "support/TerrainFiles.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Un .bmd sintético, armado en memoria con el formato de 05 ("Modelos .bmd"), para los tests de la
// CL6.1: en claro o cifrado con K16, como lo escribe 0x4f1ac0. Nada de esto sale del Data\ del
// original.
//
// Los bytes que el lector no lee (el relleno del vértice y de la normal, lo que va después del
// primer 0 de un nombre, los 38 que sobran de cada triángulo) llevan UnreadByte y no ceros: un
// lector corrido lee basura y el test lo nota.
namespace Client::Tests
{
inline constexpr std::byte UnreadByte{0xA5};
// Lo que se escribe en el byte del hueso dummy. Cualquiera que no sea 0 es dummy (05).
inline constexpr std::byte DummyBoneByte{0x01};

inline void AppendInt16(std::vector<std::byte>& bytes, std::int16_t value)
{
    AppendLittleEndian(bytes, std::bit_cast<std::uint16_t>(value), sizeof(value));
}

// Los bits del float, little-endian: un NaN va con su carga.
inline void AppendFloat(std::vector<std::byte>& bytes, float value)
{
    AppendLittleEndian(bytes, std::bit_cast<std::uint32_t>(value), sizeof(value));
}

inline void AppendVector(std::vector<std::byte>& bytes, const std::array<float, 3>& vector)
{
    for (const float value : vector)
    {
        AppendFloat(bytes, value);
    }
}

inline void AppendUnread(std::vector<std::byte>& bytes, std::size_t count)
{
    bytes.insert(bytes.end(), count, UnreadByte);
}

// Un nombre de 32 bytes: el texto, un 0 y basura hasta el final. `name` tiene que dejar lugar al 0.
inline void AppendName(std::vector<std::byte>& bytes, std::string_view name)
{
    for (const char letter : name)
    {
        bytes.push_back(static_cast<std::byte>(letter));
    }
    bytes.push_back(std::byte{0});
    AppendUnread(bytes, Data::Models::BmdNameSize - name.size() - 1);
}

inline void AppendIndices(std::vector<std::byte>& bytes, const std::array<std::int16_t, 4>& indices)
{
    for (const std::int16_t index : indices)
    {
        AppendInt16(bytes, index);
    }
}

// Un triángulo de 64 bytes: el tipo, un byte de relleno, los tres juegos de índices y lo que el lector
// no usa.
inline void AppendTriangle(std::vector<std::byte>& bytes, const Data::Models::Triangle& triangle)
{
    const std::size_t start = bytes.size();
    bytes.push_back(std::byte{triangle.polygon});
    AppendUnread(bytes, 1);
    AppendIndices(bytes, triangle.vertices);
    AppendIndices(bytes, triangle.normals);
    AppendIndices(bytes, triangle.texCoords);
    AppendUnread(bytes, Data::Models::BmdTriangleSize - (bytes.size() - start));
}

inline void AppendMesh(std::vector<std::byte>& bytes, const Data::Models::Mesh& mesh)
{
    AppendInt16(bytes, static_cast<std::int16_t>(mesh.vertices.size()));
    AppendInt16(bytes, static_cast<std::int16_t>(mesh.normals.size()));
    AppendInt16(bytes, static_cast<std::int16_t>(mesh.texCoords.size()));
    AppendInt16(bytes, static_cast<std::int16_t>(mesh.triangles.size()));
    AppendInt16(bytes, mesh.texture);
    for (const Data::Models::Vertex& vertex : mesh.vertices)
    {
        AppendInt16(bytes, vertex.bone);
        AppendUnread(bytes, sizeof(std::int16_t));
        AppendVector(bytes, vertex.position);
    }
    for (const Data::Models::Normal& normal : mesh.normals)
    {
        AppendInt16(bytes, normal.bone);
        AppendUnread(bytes, sizeof(std::int16_t));
        AppendVector(bytes, normal.normal);
        AppendInt16(bytes, normal.vertex);
        AppendUnread(bytes, sizeof(std::int16_t));
    }
    for (const Data::Models::TexCoord& texCoord : mesh.texCoords)
    {
        AppendFloat(bytes, texCoord.u);
        AppendFloat(bytes, texCoord.v);
    }
    for (const Data::Models::Triangle& triangle : mesh.triangles)
    {
        AppendTriangle(bytes, triangle);
    }
    AppendName(bytes, mesh.textureName);
}

inline void AppendAction(std::vector<std::byte>& bytes, const Data::Models::Action& action)
{
    AppendInt16(bytes, action.keyCount);
    bytes.push_back(std::byte{action.lockPositions ? std::uint8_t{1} : std::uint8_t{0}});
    for (const std::array<float, 3>& position : action.lockedPositions)
    {
        AppendVector(bytes, position);
    }
}

// Un hueso no dummy lleva, por cada acción, sus posiciones y después sus rotaciones.
inline void AppendBone(std::vector<std::byte>& bytes, const Data::Models::Bone& bone)
{
    if (bone.dummy)
    {
        bytes.push_back(DummyBoneByte);
        return;
    }
    bytes.push_back(std::byte{0});
    AppendName(bytes, bone.name);
    AppendInt16(bytes, bone.parent);
    for (const Data::Models::BoneKeys& keys : bone.actions)
    {
        for (const std::array<float, 3>& position : keys.positions)
        {
            AppendVector(bytes, position);
        }
        for (const std::array<float, 3>& rotation : keys.rotations)
        {
            AppendVector(bytes, rotation);
        }
    }
}

// El contenido de `model` en claro, sin "BMD" ni la versión: lo que va desde +4 en claro o lo que se
// cifra desde +8. Las cantidades salen del largo de cada vector, menos las keys, que son keyCount.
inline std::vector<std::byte> BmdContent(const Data::Models::Model& model)
{
    std::vector<std::byte> bytes;
    AppendName(bytes, model.name);
    AppendInt16(bytes, static_cast<std::int16_t>(model.meshes.size()));
    AppendInt16(bytes, static_cast<std::int16_t>(model.bones.size()));
    AppendInt16(bytes, static_cast<std::int16_t>(model.actions.size()));
    for (const Data::Models::Mesh& mesh : model.meshes)
    {
        AppendMesh(bytes, mesh);
    }
    for (const Data::Models::Action& action : model.actions)
    {
        AppendAction(bytes, action);
    }
    for (const Data::Models::Bone& bone : model.bones)
    {
        AppendBone(bytes, bone);
    }
    return bytes;
}

inline void AppendMagic(std::vector<std::byte>& bytes, std::uint8_t version)
{
    bytes.push_back(std::byte{'B'});
    bytes.push_back(std::byte{'M'});
    bytes.push_back(std::byte{'D'});
    bytes.push_back(std::byte{version});
}

// "BMD", la versión y `content` en claro.
inline std::vector<std::byte> PlainBmd(const std::vector<std::byte>& content, std::uint8_t version)
{
    std::vector<std::byte> bytes;
    AppendMagic(bytes, version);
    bytes.insert(bytes.end(), content.begin(), content.end());
    return bytes;
}

// "BMD", 0x0C, el tamaño de `content` y `content` cifrado con K16 desde su primer byte, como
// 0x4f1ac0.
inline std::vector<std::byte> EncryptedBmd(const std::vector<std::byte>& content)
{
    std::vector<std::byte> bytes;
    AppendMagic(bytes, Data::Models::EncryptedBmdVersion);
    AppendLittleEndian(bytes, static_cast<std::uint32_t>(content.size()), sizeof(std::uint32_t));
    const std::vector<std::byte> encrypted = EncryptK16(content);
    bytes.insert(bytes.end(), encrypted.begin(), encrypted.end());
    return bytes;
}

// Un float distinto para cada `k`, con decimales que entran justos en un float: un valor leído en el
// lugar de otro no da el mismo.
inline float SampleFloat(int k)
{
    return static_cast<float>(k) * 0.75f - 10.125f;
}

inline std::array<float, 3> SampleVector(int k)
{
    return {SampleFloat(k), SampleFloat(k + 1), -SampleFloat(k + 2)};
}

// Las keys de un hueso en una acción de `keyCount` keys, distintas por hueso y por acción.
inline Data::Models::BoneKeys SampleKeys(int bone, int action, std::int16_t keyCount)
{
    Data::Models::BoneKeys keys;
    for (int key = 0; key < keyCount; ++key)
    {
        const int k = 100 * bone + 10 * action + key;
        keys.positions.push_back(SampleVector(3 * k));
        keys.rotations.push_back(SampleVector(3 * k + 1000));
    }
    return keys;
}

// La malla del modelo de los tests: tres vértices, tres normales, tres UV y un triángulo, con los
// índices en otro orden que el de los vectores para que un juego leído en el lugar de otro se note.
inline Data::Models::Mesh SampleMesh()
{
    Data::Models::Mesh mesh;
    mesh.texture = 7;
    mesh.vertices = {{1, SampleVector(1)}, {0, SampleVector(4)}, {2, SampleVector(7)}};
    mesh.normals = {{0, SampleVector(10), 2}, {1, SampleVector(13), 0}, {1, SampleVector(16), 1}};
    mesh.texCoords = {{0.25f, 0.5f}, {0.75f, 0.125f}, {1.0f, 0.875f}};
    mesh.triangles = {{3, {2, 0, 1, 0}, {1, 2, 0, 0}, {0, 2, 1, 0}}};
    mesh.textureName = "skin_test.jpg";
    return mesh;
}

// El modelo de los tests (CL6.1, paso 1): una malla, dos acciones (de 2 keys, y de 3 con
// lock_positions) y tres huesos: una raíz, un hijo y un dummy.
inline Data::Models::Model SampleModel()
{
    Data::Models::Model model;
    model.version = Data::Models::EncryptedBmdVersion;
    model.name = "Sample";
    model.meshes = {SampleMesh()};
    model.actions = {{2, false, {}}, {3, true, {SampleVector(50), SampleVector(53), SampleVector(56)}}};
    model.bones = {
        {false, "Root", -1, {SampleKeys(0, 0, 2), SampleKeys(0, 1, 3)}},
        {false, "Child", 0, {SampleKeys(1, 0, 2), SampleKeys(1, 1, 3)}},
        {true, "", -1, {}},
    };
    return model;
}
} // namespace Client::Tests
