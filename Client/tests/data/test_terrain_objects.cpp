#include "Data/Terrain/TerrainObjects.h"

#include "Data/DataError.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

// Client::Data::Terrain::LoadTerrainObjects: EncTerrain.obj, con archivos sintéticos.
namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::LoadTerrainObjects;
using Client::Data::Terrain::TerrainObject;
using Client::Data::Terrain::TerrainObjectsHeaderSize;
using Client::Data::Terrain::TerrainObjectSize;
using Client::Tests::EncryptK16;
using Client::Tests::ObjectBits;
using Client::Tests::PlainTerrainObjects;
using Client::Tests::SampleTerrainObjects;

constexpr std::uint8_t Devias = 3;
// Los objetos del EncTerrain1.obj real (00607370-encterrain-obj.md §8).
constexpr std::size_t ObjectsOfLorencia = 2870;

// Un archivo de Devias con `objects` y su cantidad, ya cifrado.
std::vector<std::byte> ObjectsFile(const std::vector<TerrainObject>& objects)
{
    return EncryptK16(PlainTerrainObjects(Devias, static_cast<std::int16_t>(objects.size()), objects));
}

// Con un tamaño distinto corta acá; el que llama la envuelve en ASSERT_NO_FATAL_FAILURE para cortar él
// también antes de mirar un objeto que no está.
void ExpectTheSameObjects(const std::vector<TerrainObject>& loaded, const std::vector<TerrainObject>& expected)
{
    ASSERT_EQ(loaded.size(), expected.size());
    for (std::size_t k = 0; k < loaded.size(); ++k)
    {
        EXPECT_EQ(ObjectBits(loaded[k]), ObjectBits(expected[k])) << "objeto " << k;
    }
}

// El error de cargar `plain` ya cifrado, o std::nullopt si cargó.
std::optional<DataError> LoadError(const std::vector<std::byte>& plain)
{
    const auto loaded = LoadTerrainObjects(EncryptK16(plain), Devias);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// Los primeros bytes descifrados del EncTerrain1.obj real que da el research
// (00607370-encterrain-obj.md §8): la cabecera 00 01 y 2870 (0x0B36), y el primer objeto, de tipo
// 30 (0x001E), con x en 0x44AEA35C.
TEST(TerrainObjectsTest, ReadsTheBytesOfTheResearch)
{
    const std::vector<std::byte> plain{std::byte{0x00}, std::byte{0x01}, std::byte{0x36}, std::byte{0x0B},
                                       std::byte{0x1E}, std::byte{0x00}, std::byte{0x5C}, std::byte{0xA3},
                                       std::byte{0xAE}, std::byte{0x44}};
    std::vector<std::byte> file = plain;
    file.resize(TerrainObjectsHeaderSize + ObjectsOfLorencia * TerrainObjectSize);

    const auto loaded = LoadTerrainObjects(EncryptK16(file), 1);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().size(), ObjectsOfLorencia);
    EXPECT_EQ(loaded.Value()[0].type, 30);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value()[0].position[0]), 0x44AEA35Cu);
}

// Cada campo en su lugar, en el orden tipo, posición, ángulo y escala, y cada objeto 30 bytes
// después del anterior.
TEST(TerrainObjectsTest, ReadsEachFieldOfEachObject)
{
    const std::vector<TerrainObject> objects = SampleTerrainObjects(5);

    const auto loaded = LoadTerrainObjects(ObjectsFile(objects), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheSameObjects(loaded.Value(), objects));
    EXPECT_EQ(loaded.Value()[1].position[0], 101.5f);
    EXPECT_EQ(loaded.Value()[1].angle[2], -106.125f);
    EXPECT_EQ(loaded.Value()[1].scale, 100.875f);
}

// El tipo y la cantidad son int16 con signo: 0xFFFF es -1, no 65535.
TEST(TerrainObjectsTest, TheTypeHasASign)
{
    std::vector<TerrainObject> objects = SampleTerrainObjects(1);
    objects[0].type = -515;

    const auto loaded = LoadTerrainObjects(ObjectsFile(objects), Devias);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()[0].type, -515);
}

// Q-C28: cada float llega con sus bits, aunque no sea un número común. Un NaN como el 0xFFFFFFFF de
// los dos objetos de Devias, uno de señal (0x7F800001 y 0xFFA00001: son los que cambiarían si
// pasaran por una operación de punto flotante, que los vuelve silenciosos), el -0, el float más
// chico de todos (un subnormal, como los del objeto 18 de World7) e infinito.
TEST(TerrainObjectsTest, KeepsTheBitsOfEveryFloat)
{
    const auto from = [](std::uint32_t bits) { return std::bit_cast<float>(bits); };
    std::vector<TerrainObject> objects = SampleTerrainObjects(1);
    objects[0].position = {from(0xFFFFFFFF), from(0x7F800001), from(0x80000000)};
    objects[0].angle = {from(0x00000001), from(0xFFA00001), from(0x7F800000)};
    objects[0].scale = from(0x7FC00001);

    const auto loaded = LoadTerrainObjects(ObjectsFile(objects), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheSameObjects(loaded.Value(), objects));
}

// El .obj de World6 es solo la cabecera, con cantidad 0. Con una negativa tampoco hay objetos.
TEST(TerrainObjectsTest, AFileWithOnlyTheHeaderHasNoObjects)
{
    for (const std::int16_t count : {std::int16_t{0}, std::int16_t{-1}})
    {
        const std::vector<std::byte> plain = PlainTerrainObjects(Devias, count, {});
        ASSERT_EQ(plain.size(), TerrainObjectsHeaderSize);

        const auto loaded = LoadTerrainObjects(EncryptK16(plain), Devias);

        ASSERT_TRUE(loaded.HasValue()) << "cantidad " << count;
        EXPECT_TRUE(loaded.Value().empty()) << "cantidad " << count;
    }
}

// Con una cantidad negativa el original no crea ningún objeto, aunque detrás haya.
TEST(TerrainObjectsTest, ZeroOrANegativeCountHasNoObjects)
{
    const std::vector<TerrainObject> objects = SampleTerrainObjects(3);
    for (const std::int16_t count : {std::int16_t{0}, std::int16_t{-1}, std::numeric_limits<std::int16_t>::min()})
    {
        const auto loaded = LoadTerrainObjects(EncryptK16(PlainTerrainObjects(Devias, count, objects)), Devias);

        ASSERT_TRUE(loaded.HasValue()) << "cantidad " << count;
        EXPECT_TRUE(loaded.Value().empty()) << "cantidad " << count;
    }
}

// Q-C24: el original no mira el tamaño. Falta la cabecera, o un objeto, o el último byte del
// último.
TEST(TerrainObjectsTest, AShortFileIsAnError)
{
    const std::vector<std::byte> whole = PlainTerrainObjects(Devias, 3, SampleTerrainObjects(3));
    for (const std::size_t size : {std::size_t{0}, TerrainObjectsHeaderSize - 1,
                                   TerrainObjectsHeaderSize + 2 * TerrainObjectSize, whole.size() - 1})
    {
        const std::vector<std::byte> plain(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(size));

        EXPECT_EQ(LoadError(plain), DataError::FileTooShort) << size << " bytes";
    }
}

// Lo que sigue al último objeto se ignora, como en el original.
TEST(TerrainObjectsTest, IgnoresTheBytesAfterTheLastObject)
{
    const std::vector<TerrainObject> objects = SampleTerrainObjects(3);
    std::vector<std::byte> plain = PlainTerrainObjects(Devias, 2, objects);

    const auto loaded = LoadTerrainObjects(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheSameObjects(loaded.Value(), {objects[0], objects[1]}));
}

// El byte 0 no se lee: con cualquier valor, el archivo carga.
TEST(TerrainObjectsTest, DoesNotReadTheFirstByte)
{
    std::vector<std::byte> plain = PlainTerrainObjects(Devias, 1, SampleTerrainObjects(1));
    plain[0] = std::byte{0xA5};

    EXPECT_EQ(LoadError(plain), std::nullopt);
}

TEST(TerrainObjectsTest, AnotherWorldIsAnError)
{
    EXPECT_EQ(LoadError(PlainTerrainObjects(2, 1, SampleTerrainObjects(1))), DataError::WrongWorld);
}

// Un archivo corto es corto, sea del mundo que sea, como en el .map.
TEST(TerrainObjectsTest, AShortFileComesBeforeAnotherWorld)
{
    std::vector<std::byte> plain = PlainTerrainObjects(2, 2, SampleTerrainObjects(2));
    plain.pop_back();

    EXPECT_EQ(LoadError(plain), DataError::FileTooShort);
}
} // namespace
