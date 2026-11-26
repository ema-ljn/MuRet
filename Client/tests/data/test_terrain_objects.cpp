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
// The real EncTerrain1.obj's (00607370-encterrain-obj.md §8).
constexpr std::size_t ObjectsOfLorencia = 2870;

// A Devias file, already encrypted.
std::vector<std::byte> ObjectsFile(const std::vector<TerrainObject>& objects)
{
    return EncryptK16(PlainTerrainObjects(Devias, static_cast<std::int16_t>(objects.size()), objects));
}

// A different size stops here; the caller wraps it in ASSERT_NO_FATAL_FAILURE to stop too before reading a missing
// object.
void ExpectTheSameObjects(const std::vector<TerrainObject>& loaded, const std::vector<TerrainObject>& expected)
{
    ASSERT_EQ(loaded.size(), expected.size());
    for (std::size_t k = 0; k < loaded.size(); ++k)
    {
        EXPECT_EQ(ObjectBits(loaded[k]), ObjectBits(expected[k])) << "objeto " << k;
    }
}

std::optional<DataError> LoadError(const std::vector<std::byte>& plain)
{
    const auto loaded = LoadTerrainObjects(EncryptK16(plain), Devias);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// The research's first decrypted bytes of the real EncTerrain1.obj (00607370-encterrain-obj.md §8): the header 00 01
// and 2870 (0x0B36), and the first object, type 30 (0x001E), with x at 0x44AEA35C.
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

// Type, position, angle and scale, each object 30 bytes after the previous.
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

// Signed int16: 0xFFFF is -1, not 65535.
TEST(TerrainObjectsTest, TheTypeHasASign)
{
    std::vector<TerrainObject> objects = SampleTerrainObjects(1);
    objects[0].type = -515;

    const auto loaded = LoadTerrainObjects(ObjectsFile(objects), Devias);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()[0].type, -515);
}

// Q-C28: every float keeps its bits, uncommon ones too: a NaN like the two Devias objects' 0xFFFFFFFF, a signaling
// one (0x7F800001 and 0xFFA00001, which a floating-point operation would quiet), -0, the smallest float (a subnormal,
// like World7's object 18) and infinity.
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

// World6's .obj is only the header, with count 0.
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

// The original creates none, even with objects behind.
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

// Q-C24: the original ignores the size. Missing: the header, an object, or the last object's last byte.
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

// As in the original.
TEST(TerrainObjectsTest, IgnoresTheBytesAfterTheLastObject)
{
    const std::vector<TerrainObject> objects = SampleTerrainObjects(3);
    std::vector<std::byte> plain = PlainTerrainObjects(Devias, 2, objects);

    const auto loaded = LoadTerrainObjects(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheSameObjects(loaded.Value(), {objects[0], objects[1]}));
}

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

// As in the .map.
TEST(TerrainObjectsTest, AShortFileComesBeforeAnotherWorld)
{
    std::vector<std::byte> plain = PlainTerrainObjects(2, 2, SampleTerrainObjects(2));
    plain.pop_back();

    EXPECT_EQ(LoadError(plain), DataError::FileTooShort);
}
} // namespace
