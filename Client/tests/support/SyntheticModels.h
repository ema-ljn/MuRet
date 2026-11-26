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

// A synthetic .bmd, built in memory in 05's format ("Modelos .bmd"): in clear or encrypted with K16, as 0x4f1ac0
// writes it. None of this comes from the original's Data\.
// The bytes the reader does not read (the vertex's and the normal's padding, what follows a name's first 0, the 38
// spare bytes of each triangle) carry UnreadByte and not zeros: a shifted reader reads garbage and the test notices.
namespace Client::Tests
{
inline constexpr std::byte UnreadByte{0xA5};
// What is written in the dummy bone's byte. Any value other than 0 is dummy (05).
inline constexpr std::byte DummyBoneByte{0x01};

inline void AppendInt16(std::vector<std::byte>& bytes, std::int16_t value)
{
    AppendLittleEndian(bytes, std::bit_cast<std::uint16_t>(value), sizeof(value));
}

// The float's bits, little-endian: a NaN keeps its payload.
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

// A 32-byte name: the text, a 0 and garbage to the end. `name` must leave room for the 0.
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

// A 64-byte triangle: the type, a padding byte, the three index sets and what the reader does not use.
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

// A non-dummy bone carries, for each action, its positions and then its rotations.
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

// `model`'s content in clear, without "BMD" or the version: what goes from +4 in clear or what is encrypted from +8.
// The counts come from each vector's length, except the keys, which are keyCount.
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

// "BMD", the version and `content` in clear.
inline std::vector<std::byte> PlainBmd(const std::vector<std::byte>& content, std::uint8_t version)
{
    std::vector<std::byte> bytes;
    AppendMagic(bytes, version);
    bytes.insert(bytes.end(), content.begin(), content.end());
    return bytes;
}

// "BMD", 0x0C, `content`'s size and `content` encrypted with K16 from its first byte, like 0x4f1ac0.
inline std::vector<std::byte> EncryptedBmd(const std::vector<std::byte>& content)
{
    std::vector<std::byte> bytes;
    AppendMagic(bytes, Data::Models::EncryptedBmdVersion);
    AppendLittleEndian(bytes, static_cast<std::uint32_t>(content.size()), sizeof(std::uint32_t));
    const std::vector<std::byte> encrypted = EncryptK16(content);
    bytes.insert(bytes.end(), encrypted.begin(), encrypted.end());
    return bytes;
}

// A different float for each `k`, with decimals that fit exactly in a float: a value read in place of another does
// not give the same.
inline float SampleFloat(int k)
{
    return static_cast<float>(k) * 0.75f - 10.125f;
}

inline std::array<float, 3> SampleVector(int k)
{
    return {SampleFloat(k), SampleFloat(k + 1), -SampleFloat(k + 2)};
}

// A bone's keys in an action of `keyCount` keys, different per bone and per action.
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

// The test model's mesh: three vertices, three normals, three UVs and a triangle, with the indices in another order
// than the vectors' so that one set read in place of another shows.
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

// The test model: a mesh, two actions (of 2 keys, and of 3 with lock_positions) and three bones: a root, a child and
// a dummy.
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
