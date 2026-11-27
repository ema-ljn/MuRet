#include "Data/Terrain/TerrainObjectsFile.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainObjects.h"
#include "support/SameBytes.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::LoadTerrainObjects;
using Client::Data::Terrain::ReadTerrainObjectsFile;
using Client::Data::Terrain::TerrainObject;
using Client::Data::Terrain::TerrainObjectsFile;
using Client::Data::Terrain::TerrainObjectsHeaderSize;
using Client::Data::Terrain::TerrainObjectSize;
using Client::Data::Terrain::WriteTerrainObjectsFile;
using Client::Tests::EncryptK16;
using Client::Tests::ObjectBits;
using Client::Tests::PlainTerrainObjects;
using Client::Tests::SameBytes;
using Client::Tests::SampleTerrainObjects;

constexpr std::uint8_t Dungeon = 2;

std::vector<std::byte> DungeonFile(const std::vector<TerrainObject>& objects)
{
    return EncryptK16(PlainTerrainObjects(Dungeon, static_cast<std::int16_t>(objects.size()), objects));
}

void ExpectTheSameObjects(const std::vector<TerrainObject>& actual, const std::vector<TerrainObject>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t k = 0; k < actual.size(); ++k)
    {
        EXPECT_EQ(ObjectBits(actual[k]), ObjectBits(expected[k])) << "object " << k;
    }
}

TEST(TerrainObjectsFileTest, ReadsTheHeaderAndEveryObject)
{
    const std::vector<TerrainObject> sample = SampleTerrainObjects(3);

    const auto read = ReadTerrainObjectsFile(DungeonFile(sample));

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().version, 0);
    EXPECT_EQ(read.Value().world, Dungeon);
    ExpectTheSameObjects(read.Value().objects, sample);
    EXPECT_TRUE(read.Value().tail.empty());
}

TEST(TerrainObjectsFileTest, WritesTheLayoutOfTheLoader)
{
    const std::vector<TerrainObject> sample = SampleTerrainObjects(3);
    const TerrainObjectsFile objects{.version = 0, .world = Dungeon, .objects = sample, .tail = {}};

    const auto written = WriteTerrainObjectsFile(objects);

    ASSERT_TRUE(written.HasValue());
    EXPECT_TRUE(SameBytes(written.Value(), DungeonFile(sample)));
}

TEST(TerrainObjectsFileTest, WritesTheBitsOfEveryFloat)
{
    TerrainObject odd = SampleTerrainObjects(1)[0];
    odd.position = {std::bit_cast<float>(0x7FC00001U), std::bit_cast<float>(0x80000000U),
                    std::bit_cast<float>(0x00000001U)};
    odd.scale = std::bit_cast<float>(0xFF800000U);
    const TerrainObjectsFile objects{.version = 0, .world = Dungeon, .objects = {odd}, .tail = {}};

    const auto written = WriteTerrainObjectsFile(objects);

    ASSERT_TRUE(written.HasValue());
    EXPECT_TRUE(SameBytes(written.Value(), DungeonFile({odd})));
    const auto readBack = ReadTerrainObjectsFile(written.Value());
    ASSERT_TRUE(readBack.HasValue());
    ExpectTheSameObjects(readBack.Value().objects, {odd});
}

TEST(TerrainObjectsFileTest, AFileWithNoObjectsIsTheHeader)
{
    const std::vector<std::byte> file = DungeonFile({});
    ASSERT_EQ(file.size(), TerrainObjectsHeaderSize);

    const auto read = ReadTerrainObjectsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_TRUE(read.Value().objects.empty());
    const auto written = WriteTerrainObjectsFile(read.Value());
    ASSERT_TRUE(written.HasValue());
    EXPECT_TRUE(SameBytes(written.Value(), file));
}

TEST(TerrainObjectsFileTest, KeepsTheBytesAfterTheLastObject)
{
    const std::vector<TerrainObject> sample = SampleTerrainObjects(3);
    const std::vector<std::byte> extra = {std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}, std::byte{5}};
    std::vector<std::byte> plain = PlainTerrainObjects(Dungeon, 3, sample);
    plain.insert(plain.end(), extra.begin(), extra.end());
    const std::vector<std::byte> file = EncryptK16(plain);

    const auto read = ReadTerrainObjectsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().tail, extra);
    const auto written = WriteTerrainObjectsFile(read.Value());
    ASSERT_TRUE(written.HasValue());
    EXPECT_TRUE(SameBytes(written.Value(), file));
}

TEST(TerrainObjectsFileTest, KeepsTheFirstByte)
{
    std::vector<std::byte> plain = PlainTerrainObjects(Dungeon, 1, SampleTerrainObjects(1));
    plain[0] = std::byte{0x2A};
    const std::vector<std::byte> file = EncryptK16(plain);

    const auto read = ReadTerrainObjectsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().version, 0x2A);
    const auto written = WriteTerrainObjectsFile(read.Value());
    ASSERT_TRUE(written.HasValue());
    EXPECT_TRUE(SameBytes(written.Value(), file));
}

TEST(TerrainObjectsFileTest, KeepsTheWorldByte)
{
    constexpr std::uint8_t sharedByTwoFiles = 31;
    const std::vector<TerrainObject> sample = SampleTerrainObjects(2);
    const std::vector<std::byte> file = EncryptK16(PlainTerrainObjects(sharedByTwoFiles, 2, sample));

    const auto read = ReadTerrainObjectsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().world, sharedByTwoFiles);
    const auto written = WriteTerrainObjectsFile(read.Value());
    ASSERT_TRUE(written.HasValue());
    EXPECT_TRUE(SameBytes(written.Value(), file));
}

TEST(TerrainObjectsFileTest, ANegativeCountIsAnError)
{
    const auto read = ReadTerrainObjectsFile(EncryptK16(PlainTerrainObjects(Dungeon, -1, {})));

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), DataError::InvalidData);
}

TEST(TerrainObjectsFileTest, AShortFileIsAnError)
{
    const std::vector<TerrainObject> sample = SampleTerrainObjects(2);
    std::vector<std::byte> header = EncryptK16(PlainTerrainObjects(Dungeon, 0, {}));
    header.pop_back();

    const auto noHeader = ReadTerrainObjectsFile(header);
    const auto missingAnObject = ReadTerrainObjectsFile(EncryptK16(PlainTerrainObjects(Dungeon, 3, sample)));

    ASSERT_FALSE(noHeader.HasValue());
    EXPECT_EQ(noHeader.Error(), DataError::FileTooShort);
    ASSERT_FALSE(missingAnObject.HasValue());
    EXPECT_EQ(missingAnObject.Error(), DataError::FileTooShort);
}

TEST(TerrainObjectsFileTest, MoreObjectsThanTheCountHoldsIsAnError)
{
    constexpr auto mostObjects = static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max());
    TerrainObjectsFile objects{.version = 0, .world = Dungeon, .objects = {}, .tail = {}};
    objects.objects.resize(mostObjects);

    const auto fits = WriteTerrainObjectsFile(objects);
    objects.objects.emplace_back();
    const auto tooMany = WriteTerrainObjectsFile(objects);

    ASSERT_TRUE(fits.HasValue());
    EXPECT_EQ(fits.Value().size(), TerrainObjectsHeaderSize + TerrainObjectSize * mostObjects);
    ASSERT_FALSE(tooMany.HasValue());
    EXPECT_EQ(tooMany.Error(), DataError::InvalidData);
}

TEST(TerrainObjectsFileTest, TheClientsLoaderReadsAnEdit)
{
    auto read = ReadTerrainObjectsFile(DungeonFile(SampleTerrainObjects(3)));
    ASSERT_TRUE(read.HasValue());
    std::vector<TerrainObject>& objects = read.Value().objects;
    objects[1].position[0] += 100.0f;
    objects.push_back(SampleTerrainObjects(4)[3]);
    const auto written = WriteTerrainObjectsFile(read.Value());
    ASSERT_TRUE(written.HasValue());

    const auto loaded = LoadTerrainObjects(written.Value(), Dungeon);

    ASSERT_TRUE(loaded.HasValue());
    ExpectTheSameObjects(loaded.Value(), objects);
}
} // namespace
