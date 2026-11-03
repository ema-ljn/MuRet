#include "Data/Models/Bmd.h"

#include "Data/DataError.h"
#include "Data/Models/Model.h"
#include "support/ExpectedError.h"
#include "support/LittleEndian.h"
#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Client::Data::Models::LoadBmd: el .bmd de un modelo, con archivos sintéticos (support/SyntheticModels.h).
namespace
{
using Client::Data::DataError;
using Client::Data::Models::Action;
using Client::Data::Models::BmdEncryptedSizeOffset;
using Client::Data::Models::BmdNameSize;
using Client::Data::Models::BmdNormalSize;
using Client::Data::Models::BmdTexCoordSize;
using Client::Data::Models::BmdTriangleSize;
using Client::Data::Models::BmdVertexSize;
using Client::Data::Models::Bone;
using Client::Data::Models::EncryptedBmdVersion;
using Client::Data::Models::LoadBmd;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Data::Models::Triangle;
using Client::Tests::BmdContent;
using Client::Tests::EncryptedBmd;
using Client::Tests::ErrorOf;
using Client::Tests::PlainBmd;
using Client::Tests::SampleModel;

// La versión de 1.142 de los 1.962 modelos del laboratorio, que va en claro (05).
constexpr std::uint8_t ClearVersion = 0x0A;

// Las tres cantidades del modelo van después del nombre: mallas, huesos y acciones. Las cinco de la
// primera malla, después.
constexpr std::size_t MeshCountOffset = BmdNameSize;
constexpr std::size_t BoneCountOffset = MeshCountOffset + 2;
constexpr std::size_t ActionCountOffset = MeshCountOffset + 4;
constexpr std::size_t FirstMeshOffset = MeshCountOffset + 6;
constexpr std::size_t MeshHeaderSize = 5 * sizeof(std::int16_t);

// Lo que ocupa `mesh` en el archivo, con su cabecera y el nombre de la textura.
std::size_t MeshBytes(const Mesh& mesh)
{
    return MeshHeaderSize + BmdVertexSize * mesh.vertices.size() + BmdNormalSize * mesh.normals.size() +
           BmdTexCoordSize * mesh.texCoords.size() + BmdTriangleSize * mesh.triangles.size() + BmdNameSize;
}

// Pone `value` en el int16 de `offset` de `content`.
void PutInt16(std::vector<std::byte>& content, std::size_t offset, std::int16_t value)
{
    const auto bits = std::bit_cast<std::uint16_t>(value);
    content[offset] = static_cast<std::byte>(bits & 0xFFu);
    content[offset + 1] = static_cast<std::byte>(bits >> 8);
}

void ExpectSameMesh(const Mesh& loaded, const Mesh& expected)
{
    EXPECT_EQ(loaded.texture, expected.texture);
    EXPECT_EQ(loaded.textureName, expected.textureName);
    ASSERT_EQ(loaded.vertices.size(), expected.vertices.size());
    for (std::size_t k = 0; k < loaded.vertices.size(); ++k)
    {
        EXPECT_EQ(loaded.vertices[k].bone, expected.vertices[k].bone) << "vértice " << k;
        EXPECT_EQ(loaded.vertices[k].position, expected.vertices[k].position) << "vértice " << k;
    }
    ASSERT_EQ(loaded.normals.size(), expected.normals.size());
    for (std::size_t k = 0; k < loaded.normals.size(); ++k)
    {
        EXPECT_EQ(loaded.normals[k].bone, expected.normals[k].bone) << "normal " << k;
        EXPECT_EQ(loaded.normals[k].normal, expected.normals[k].normal) << "normal " << k;
        EXPECT_EQ(loaded.normals[k].vertex, expected.normals[k].vertex) << "normal " << k;
    }
    ASSERT_EQ(loaded.texCoords.size(), expected.texCoords.size());
    for (std::size_t k = 0; k < loaded.texCoords.size(); ++k)
    {
        EXPECT_EQ(loaded.texCoords[k].u, expected.texCoords[k].u) << "UV " << k;
        EXPECT_EQ(loaded.texCoords[k].v, expected.texCoords[k].v) << "UV " << k;
    }
    ASSERT_EQ(loaded.triangles.size(), expected.triangles.size());
    for (std::size_t k = 0; k < loaded.triangles.size(); ++k)
    {
        EXPECT_EQ(loaded.triangles[k].polygon, expected.triangles[k].polygon) << "triángulo " << k;
        EXPECT_EQ(loaded.triangles[k].vertices, expected.triangles[k].vertices) << "triángulo " << k;
        EXPECT_EQ(loaded.triangles[k].normals, expected.triangles[k].normals) << "triángulo " << k;
        EXPECT_EQ(loaded.triangles[k].texCoords, expected.triangles[k].texCoords) << "triángulo " << k;
    }
}

void ExpectSameBone(const Bone& loaded, const Bone& expected)
{
    EXPECT_EQ(loaded.dummy, expected.dummy);
    EXPECT_EQ(loaded.name, expected.name);
    EXPECT_EQ(loaded.parent, expected.parent);
    ASSERT_EQ(loaded.actions.size(), expected.actions.size());
    for (std::size_t action = 0; action < loaded.actions.size(); ++action)
    {
        EXPECT_EQ(loaded.actions[action].positions, expected.actions[action].positions) << "acción " << action;
        EXPECT_EQ(loaded.actions[action].rotations, expected.actions[action].rotations) << "acción " << action;
    }
}

// Cada campo de `loaded` contra `expected`, menos la versión y lo que sobra, que mira cada test.
void ExpectSameModel(const Model& loaded, const Model& expected)
{
    EXPECT_EQ(loaded.name, expected.name);
    ASSERT_EQ(loaded.meshes.size(), expected.meshes.size());
    for (std::size_t k = 0; k < loaded.meshes.size(); ++k)
    {
        SCOPED_TRACE(::testing::Message() << "malla " << k);
        ExpectSameMesh(loaded.meshes[k], expected.meshes[k]);
    }
    ASSERT_EQ(loaded.actions.size(), expected.actions.size());
    for (std::size_t k = 0; k < loaded.actions.size(); ++k)
    {
        EXPECT_EQ(loaded.actions[k].keyCount, expected.actions[k].keyCount) << "acción " << k;
        EXPECT_EQ(loaded.actions[k].lockPositions, expected.actions[k].lockPositions) << "acción " << k;
        EXPECT_EQ(loaded.actions[k].lockedPositions, expected.actions[k].lockedPositions) << "acción " << k;
    }
    ASSERT_EQ(loaded.bones.size(), expected.bones.size());
    for (std::size_t k = 0; k < loaded.bones.size(); ++k)
    {
        SCOPED_TRACE(::testing::Message() << "hueso " << k);
        ExpectSameBone(loaded.bones[k], expected.bones[k]);
    }
}

// El modelo de los tests, en claro, cargado. Con un error falla el test y devuelve uno vacío.
Model LoadedClearSample()
{
    auto loaded = LoadBmd(PlainBmd(BmdContent(SampleModel()), ClearVersion));
    EXPECT_TRUE(loaded.HasValue());
    return loaded.HasValue() ? std::move(loaded).Value() : Model{};
}

// Cada campo en su lugar: las cantidades en el orden mallas, huesos, acciones; los nombres hasta el
// primer 0; el relleno del vértice, de la normal y del triángulo, salteado.
TEST(BmdTest, ReadsAClearModel)
{
    const Model expected = SampleModel();

    const auto loaded = LoadBmd(PlainBmd(BmdContent(expected), ClearVersion));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().version, ClearVersion);
    EXPECT_EQ(loaded.Value().trailingBytes, 0u);
    ASSERT_NO_FATAL_FAILURE(ExpectSameModel(loaded.Value(), expected));
}

// El mismo modelo con la versión 0x0C: el tamaño en +4 y el bloque cifrado con K16 desde +8, desde
// su primer byte.
TEST(BmdTest, ReadsAnEncryptedModel)
{
    const Model expected = SampleModel();

    const auto loaded = LoadBmd(EncryptedBmd(BmdContent(expected)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().version, EncryptedBmdVersion);
    EXPECT_EQ(loaded.Value().trailingBytes, 0u);
    ASSERT_NO_FATAL_FAILURE(ExpectSameModel(loaded.Value(), expected));
}

// Q-C86: cualquier versión que no sea 0x0C va en claro, también la 0x00 de los cuatro de Object3.
TEST(BmdTest, AnyVersionButTwelveIsClear)
{
    const Model expected = SampleModel();
    for (const std::uint8_t version : {std::uint8_t{0x0A}, std::uint8_t{0x0B}, std::uint8_t{0x00}, std::uint8_t{0xFF}})
    {
        SCOPED_TRACE(::testing::Message() << "versión " << int{version});

        const auto loaded = LoadBmd(PlainBmd(BmdContent(expected), version));

        ASSERT_TRUE(loaded.HasValue());
        EXPECT_EQ(loaded.Value().version, version);
        ASSERT_NO_FATAL_FAILURE(ExpectSameModel(loaded.Value(), expected));
    }
}

// Q-C86: el original no mira el "BMD", y MuRet tampoco.
TEST(BmdTest, DoesNotLookAtTheMagic)
{
    const Model expected = SampleModel();
    std::vector<std::byte> clear = PlainBmd(BmdContent(expected), ClearVersion);
    std::vector<std::byte> encrypted = EncryptedBmd(BmdContent(expected));
    for (std::vector<std::byte>* file : {&clear, &encrypted})
    {
        (*file)[0] = std::byte{'X'};
        (*file)[1] = std::byte{'Y'};
        (*file)[2] = std::byte{'Z'};
    }

    const auto loadedClear = LoadBmd(clear);
    const auto loadedEncrypted = LoadBmd(encrypted);

    ASSERT_TRUE(loadedClear.HasValue());
    ASSERT_TRUE(loadedEncrypted.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectSameModel(loadedClear.Value(), expected));
    ASSERT_NO_FATAL_FAILURE(ExpectSameModel(loadedEncrypted.Value(), expected));
}

// Cada prefijo del archivo es corto: el original seguiría leyendo la memoria de después del buffer.
// En claro, el archivo cortado; cifrado, el archivo cortado (el tamaño de +4 pasa de lo que queda) y
// el bloque cortado con su tamaño bien.
TEST(BmdTest, EveryCutIsTooShort)
{
    const std::vector<std::byte> content = BmdContent(SampleModel());
    const std::vector<std::byte> clear = PlainBmd(content, ClearVersion);
    const std::vector<std::byte> encrypted = EncryptedBmd(content);
    for (std::size_t size = 0; size < clear.size(); ++size)
    {
        const std::vector<std::byte> cut(clear.begin(), clear.begin() + static_cast<std::ptrdiff_t>(size));
        EXPECT_EQ(ErrorOf(LoadBmd(cut)), DataError::FileTooShort) << "en claro, con " << size << " bytes";
    }
    for (std::size_t size = 0; size < encrypted.size(); ++size)
    {
        const std::vector<std::byte> cut(encrypted.begin(), encrypted.begin() + static_cast<std::ptrdiff_t>(size));
        EXPECT_EQ(ErrorOf(LoadBmd(cut)), DataError::FileTooShort) << "cifrado, con " << size << " bytes";
    }
    for (std::size_t size = 0; size < content.size(); ++size)
    {
        const std::vector<std::byte> block(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(size));
        EXPECT_EQ(ErrorOf(LoadBmd(EncryptedBmd(block))), DataError::FileTooShort)
            << "el bloque cifrado, con " << size << " bytes";
    }
}

// Q-C86: un tamaño cifrado que no entra en el archivo. El original descifraría más allá del buffer.
TEST(BmdTest, AnEncryptedSizeBeyondTheFileIsTooShort)
{
    const std::vector<std::byte> content = BmdContent(SampleModel());
    for (const std::uint32_t size : {static_cast<std::uint32_t>(content.size() + 1), std::uint32_t{0xFFFFFFFF}})
    {
        std::vector<std::byte> file = EncryptedBmd(content);
        std::vector<std::byte> sizeBytes;
        Client::Tests::AppendLittleEndian(sizeBytes, size, sizeof(size));
        std::copy(sizeBytes.begin(), sizeBytes.end(), file.begin() + BmdEncryptedSizeOffset);

        EXPECT_EQ(ErrorOf(LoadBmd(file)), DataError::FileTooShort) << "tamaño " << size;
    }
}

// Lo que sobra después del último hueso se cuenta y no se lee: en claro, el resto del archivo;
// cifrado, el resto del bloque.
TEST(BmdTest, CountsTheTrailingBytes)
{
    constexpr std::size_t Extra = 5;
    std::vector<std::byte> content = BmdContent(SampleModel());
    content.insert(content.end(), Extra, std::byte{0x5A});

    const auto clear = LoadBmd(PlainBmd(content, ClearVersion));
    const auto encrypted = LoadBmd(EncryptedBmd(content));

    ASSERT_TRUE(clear.HasValue());
    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_EQ(clear.Value().trailingBytes, Extra);
    EXPECT_EQ(encrypted.Value().trailingBytes, Extra);
}

// Lo que va en el archivo después del bloque cifrado no es del modelo: el original descifra los
// bytes que dice +4 y no mira más.
TEST(BmdTest, BytesAfterTheEncryptedBlockAreNotRead)
{
    std::vector<std::byte> file = EncryptedBmd(BmdContent(SampleModel()));
    file.insert(file.end(), 3, std::byte{0x5A});

    const auto loaded = LoadBmd(file);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().trailingBytes, 0u);
}

// Cada float con los bits del archivo, también un NaN con su carga, en claro y cifrado.
TEST(BmdTest, KeepsTheFloatBits)
{
    constexpr std::uint32_t SignalingNan = 0x7FA00001;
    constexpr std::uint32_t NegativeQuietNan = 0xFFC12345;
    Model model = SampleModel();
    model.meshes[0].vertices[1].position[0] = std::bit_cast<float>(SignalingNan);
    model.bones[1].actions[1].rotations[2][1] = std::bit_cast<float>(NegativeQuietNan);
    const std::vector<std::byte> content = BmdContent(model);

    for (const auto& loaded : {LoadBmd(PlainBmd(content, ClearVersion)), LoadBmd(EncryptedBmd(content))})
    {
        ASSERT_TRUE(loaded.HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value().meshes[0].vertices[1].position[0]), SignalingNan);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value().bones[1].actions[1].rotations[2][1]), NegativeQuietNan);
    }
}

// Un hueso dummy no trae nombre, padre ni keys: es un byte. Cualquiera que no sea 0 (05).
TEST(BmdTest, ADummyBoneHasNoKeys)
{
    std::vector<std::byte> content = BmdContent(SampleModel());
    // El dummy es el último hueso, y su byte el último del contenido.
    content.back() = std::byte{0x80};

    const auto loaded = LoadBmd(PlainBmd(content, ClearVersion));

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().bones.size(), 3u);
    const Bone& dummy = loaded.Value().bones[2];
    EXPECT_TRUE(dummy.dummy);
    EXPECT_EQ(dummy.name, "");
    EXPECT_EQ(dummy.parent, -1);
    EXPECT_TRUE(dummy.actions.empty());
    EXPECT_FALSE(loaded.Value().bones[0].dummy);
    EXPECT_FALSE(loaded.Value().bones[1].dummy);
    EXPECT_EQ(loaded.Value().trailingBytes, 0u);
}

// Una acción con lock_positions trae una posición de 12 bytes por key; una sin, ninguna.
TEST(BmdTest, LockPositionsReadTwelveBytesPerKey)
{
    const Model model = LoadedClearSample();

    ASSERT_EQ(model.actions.size(), 2u);
    const Action& free = model.actions[0];
    const Action& locked = model.actions[1];
    EXPECT_EQ(free.keyCount, 2);
    EXPECT_FALSE(free.lockPositions);
    EXPECT_TRUE(free.lockedPositions.empty());
    EXPECT_EQ(locked.keyCount, 3);
    EXPECT_TRUE(locked.lockPositions);
    EXPECT_EQ(locked.lockedPositions, SampleModel().actions[1].lockedPositions);
    // Con 12 bytes de más o de menos, los huesos que siguen se leerían corridos.
    ASSERT_EQ(model.bones.size(), 3u);
    EXPECT_EQ(model.bones[1].actions[1].rotations, SampleModel().bones[1].actions[1].rotations);
}

// El tipo y los tres juegos de cuatro índices, en +0, +2, +10 y +18 de cada triángulo de 64 bytes,
// también el cuarto índice de un cuadrilátero.
TEST(BmdTest, TheTriangleKeepsItsThreeIndexSets)
{
    Model model = SampleModel();
    const Triangle quad{4, {2, 0, 1, 2}, {1, 2, 0, 1}, {0, 2, 1, 0}};
    model.meshes[0].triangles.push_back(quad);

    const auto loaded = LoadBmd(PlainBmd(BmdContent(model), ClearVersion));

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().meshes.size(), 1u);
    const std::vector<Triangle>& triangles = loaded.Value().meshes[0].triangles;
    ASSERT_EQ(triangles.size(), 2u);
    EXPECT_EQ(triangles[0].polygon, 3);
    EXPECT_EQ(triangles[0].vertices, (std::array<std::int16_t, 4>{2, 0, 1, 0}));
    EXPECT_EQ(triangles[0].normals, (std::array<std::int16_t, 4>{1, 2, 0, 0}));
    EXPECT_EQ(triangles[0].texCoords, (std::array<std::int16_t, 4>{0, 2, 1, 0}));
    EXPECT_EQ(triangles[1].polygon, 4);
    EXPECT_EQ(triangles[1].vertices, quad.vertices);
    EXPECT_EQ(triangles[1].normals, quad.normals);
    EXPECT_EQ(triangles[1].texCoords, quad.texCoords);
}

// Una cantidad negativa no es un modelo: el original la usaría para pedir memoria. Ninguno del
// laboratorio la tiene.
TEST(BmdTest, ANegativeCountIsInvalidData)
{
    const std::vector<std::byte> content = BmdContent(SampleModel());
    const std::size_t firstActionOffset = FirstMeshOffset + MeshBytes(SampleModel().meshes[0]);
    const std::vector<std::size_t> counts = {
        MeshCountOffset,     BoneCountOffset,     ActionCountOffset,   FirstMeshOffset,
        FirstMeshOffset + 2, FirstMeshOffset + 4, FirstMeshOffset + 6, firstActionOffset,
    };
    for (const std::size_t offset : counts)
    {
        std::vector<std::byte> negative = content;
        PutInt16(negative, offset, -1);

        EXPECT_EQ(ErrorOf(LoadBmd(PlainBmd(negative, ClearVersion))), DataError::InvalidData)
            << "la cantidad de " << offset;
    }
}
} // namespace
