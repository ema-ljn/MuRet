#include "Data/Terrain/TerrainObjects.h"

#include "Core/ByteReader.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Core::ByteReader;
using Client::Data::Terrain::LoadTerrainObjects;
using Client::Data::Terrain::TerrainObject;
using Client::Data::Terrain::TerrainObjectsHeaderSize;
using Client::Data::Terrain::TerrainObjectSize;
using Client::Tests::LabWorlds;
using Client::Tests::LorenciaObjectsCapture;
using Client::Tests::ObjectBits;
using Client::Tests::ObjectValueCount;
using Client::Tests::ReadFileBytes;

// The path the original builds: "Data\\%s\\EncTerrain%d.obj" with "World%d" (0x6d12b8, at 0x61a560).
std::string ObjectsFile(std::uint8_t world)
{
    const std::string number = std::to_string(world);
    return "World" + number + "\\EncTerrain" + number + ".obj";
}

// std::nullopt, with the test already failed, if they do not load.
std::optional<std::vector<TerrainObject>> LoadObjects(const std::filesystem::path& path, std::uint8_t world)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(path);
    if (!file.has_value())
    {
        ADD_FAILURE() << "no se pudo leer el mundo " << int{world};
        return std::nullopt;
    }
    auto loaded = LoadTerrainObjects(std::as_bytes(std::span(*file)), world);
    if (!loaded.HasValue())
    {
        ADD_FAILURE() << "el mundo " << int{world} << " no cargó";
        return std::nullopt;
    }
    return std::move(loaded).Value();
}

class GoldenTerrainObjects : public Client::Tests::GoldenDataTest
{
protected:
    // std::nullopt, with the test already failed, if they do not load.
    std::optional<std::vector<TerrainObject>> LoadWorld(std::uint8_t world) const
    {
        const auto path = DataFile(ObjectsFile(world));
        if (!path.has_value())
        {
            return std::nullopt;
        }
        return LoadObjects(*path, world);
    }
};

using MemoryTerrainObjects = Client::Tests::MemoryDumpTest;

bool IsFinite(const TerrainObject& object)
{
    const auto finite = [](float value) { return std::isfinite(value); };
    return std::ranges::all_of(object.position, finite) && std::ranges::all_of(object.angle, finite) &&
           std::isfinite(object.scale);
}

// Exactly the objects the header declares, not a byte more. World6's has none: it is 4 bytes.
TEST_F(GoldenTerrainObjects, LoadsEveryWorldOfTheLab)
{
    for (const std::uint8_t world : LabWorlds)
    {
        const auto path = DataFile(ObjectsFile(world));
        ASSERT_TRUE(path.has_value()) << "mundo " << int{world};
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "mundo " << int{world};

        const auto loaded = LoadTerrainObjects(std::as_bytes(std::span(*file)), world);

        ASSERT_TRUE(loaded.HasValue()) << "mundo " << int{world};
        EXPECT_EQ(file->size(), TerrainObjectsHeaderSize + loaded.Value().size() * TerrainObjectSize)
            << "mundo " << int{world};
        EXPECT_EQ(loaded.Value().empty(), world == 6) << "mundo " << int{world};
    }
}

// What the research measured in the real EncTerrain1.obj, the same file as the lab's (00607370-encterrain-obj.md §8,
// with its SHA-256): 2870 objects of 109 types, 0 to 153, all finite floats and scales from 0.30 to 3.28. The first
// two objects' bits come from the research's decrypted bytes; the rest of the first, from its rounded values.
TEST_F(GoldenTerrainObjects, LorenciaHasWhatTheResearchMeasured)
{
    const std::optional<std::vector<TerrainObject>> objects = LoadWorld(1);
    ASSERT_TRUE(objects.has_value());

    ASSERT_EQ(objects->size(), 2870u);
    std::set<std::int16_t> types;
    for (const TerrainObject& object : *objects)
    {
        types.insert(object.type);
    }
    EXPECT_EQ(types.size(), 109u);
    EXPECT_EQ(*types.begin(), 0);
    EXPECT_EQ(*types.rbegin(), 153);
    EXPECT_TRUE(std::ranges::all_of(*objects, IsFinite));
    const auto [smallest, largest] = std::ranges::minmax(*objects | std::views::transform(&TerrainObject::scale));
    EXPECT_NEAR(smallest, 0.30f, 0.005f);
    EXPECT_NEAR(largest, 3.28f, 0.005f);

    const TerrainObject& first = (*objects)[0];
    EXPECT_EQ(first.type, 30);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(first.position[0]), 0x44AEA35Cu);
    EXPECT_NEAR(first.position[1], 682.56f, 0.005f);
    EXPECT_NEAR(first.position[2], 44.23f, 0.005f);
    EXPECT_EQ(first.angle, (std::array{0.0f, 0.0f, -60.0f}));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(first.scale), 0x3F70A3D8u);

    const TerrainObject& second = (*objects)[1];
    EXPECT_EQ(second.type, 30);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(second.position[0]), 0x44443F50u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(second.position[1]), 0x4420C038u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(second.position[2]), 0xC1A6D765u);
}

struct ObjectPlace
{
    std::uint8_t world = 0;
    std::size_t index = 0;
    bool operator==(const ObjectPlace&) const = default;
};

// GoogleTest uses it to show a list that differs from the expected one.
[[maybe_unused]] void PrintTo(const ObjectPlace& place, std::ostream* out)
{
    *out << "World" << int{place.world} << " #" << place.index;
}

// Q-C28: the lab's odd objects, which the loader keeps as they come. The only ones with a float that is not a number
// are Devias's 492 and 493, with y at 0xFFFFFFFF; the only negative type is World7's 18, -515, and the two after it
// are all zeros. Four have a negative scale.
TEST_F(GoldenTerrainObjects, TheOddObjectsOfTheLab)
{
    std::vector<ObjectPlace> notFinite;
    std::vector<ObjectPlace> negativeType;
    std::vector<ObjectPlace> allZero;
    std::vector<ObjectPlace> negativeScale;
    for (const std::uint8_t world : LabWorlds)
    {
        const std::optional<std::vector<TerrainObject>> objects = LoadWorld(world);
        ASSERT_TRUE(objects.has_value()) << "mundo " << int{world};

        for (std::size_t k = 0; k < objects->size(); ++k)
        {
            const TerrainObject& object = (*objects)[k];
            const ObjectPlace place{world, k};
            if (!IsFinite(object))
            {
                notFinite.push_back(place);
            }
            if (object.type < 0)
            {
                negativeType.push_back(place);
            }
            if (std::ranges::all_of(ObjectBits(object), [](std::uint32_t bits) { return bits == 0; }))
            {
                allZero.push_back(place);
            }
            if (object.scale < 0.0f)
            {
                negativeScale.push_back(place);
            }
        }
    }

    EXPECT_EQ(notFinite, (std::vector<ObjectPlace>{{3, 492}, {3, 493}}));
    EXPECT_EQ(negativeType, (std::vector<ObjectPlace>{{7, 18}}));
    EXPECT_EQ(allZero, (std::vector<ObjectPlace>{{7, 19}, {7, 20}}));
    EXPECT_EQ(negativeScale, (std::vector<ObjectPlace>{{8, 2258}, {8, 3570}, {9, 1648}, {32, 2950}}));

    const std::optional<std::vector<TerrainObject>> devias = LoadWorld(3);
    const std::optional<std::vector<TerrainObject>> world7 = LoadWorld(7);
    ASSERT_TRUE(devias.has_value());
    ASSERT_TRUE(world7.has_value());
    EXPECT_EQ(std::bit_cast<std::uint32_t>((*devias)[492].position[1]), 0xFFFFFFFFu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>((*devias)[493].position[1]), 0xFFFFFFFFu);
    EXPECT_EQ((*world7)[18].type, -515);
}

// How the original stores each object in obj_nodes.bin, from the Lorencia objects capture (mu-golden's README): the
// list and the node's address (two u32), then the object's 0x1D4 bytes that 0x606660 allocates.
constexpr std::size_t NodeHeaderSize = 2 * sizeof(std::uint32_t);
constexpr std::size_t NodeObjectSize = 0x1D4;
constexpr std::size_t NodeSize = NodeHeaderSize + NodeObjectSize;
static_assert(NodeSize == 0x1DC, "el nodo que da el README de mu-golden (2870 × 0x1DC)");
// Measured on the dumps, not read in 0x606660's asm (05-formatos-de-datos.md, the .obj, and OBJ_NODE_* in the research
// repo's mu-client/tests/assets/test_terrain_golden.c).
constexpr std::size_t NodeTypeOffset = 2;
constexpr std::size_t NodeScaleOffset = 12;
constexpr std::size_t NodePositionOffset = 16;
constexpr std::size_t NodeAngleOffset = 28;
// The capture's scene (0x6D5DC8). 0x606660 changes some objects' scale in scenes 2 and 4
// (mu-client/docs/component-terrain.md), so the comparison holds for this one.
constexpr std::int32_t SceneOfTheCapture = 5;

using ObjectValues = std::array<std::uint32_t, ObjectValueCount>;

// In the order of ObjectBits.
ObjectValues NodeValues(std::span<const std::byte> object)
{
    const auto readU32 = [object](std::size_t offset)
    { return ByteReader(object.subspan(offset)).ReadU32Le().value_or(0); };
    ByteReader type(object.subspan(NodeTypeOffset));
    return {type.ReadU16Le().value_or(0),
            readU32(NodePositionOffset),
            readU32(NodePositionOffset + sizeof(float)),
            readU32(NodePositionOffset + 2 * sizeof(float)),
            readU32(NodeAngleOffset),
            readU32(NodeAngleOffset + sizeof(float)),
            readU32(NodeAngleOffset + 2 * sizeof(float)),
            readU32(NodeScaleOffset)};
}

// From a 4-byte dump, like g_world or the scene.
std::optional<std::int32_t> DumpInt32(const std::filesystem::path& path)
{
    const std::optional<std::vector<char>> bytes = ReadFileBytes(path);
    if (!bytes || bytes->size() != sizeof(std::int32_t))
    {
        return std::nullopt;
    }
    ByteReader reader(std::as_bytes(std::span(*bytes)));
    return std::bit_cast<std::int32_t>(reader.ReadU32Le().value_or(0));
}

// Lorencia's objects as the original main.exe had them in scene 5: the same, with the same bits, in the same order
// and as many as its counter (0x07D36598). The original spreads them over 256 lists, one per map block (0x606660),
// each in file order; since the file is already sorted by block, lists 0 to 255 give the file's order.
TEST_F(MemoryTerrainObjects, LorenciaEqualsTheOriginalsMemory)
{
    const auto path = DataFile(ObjectsFile(LorenciaObjectsCapture.world));
    const auto nodesDump = DumpFile(LorenciaObjectsCapture, "obj_nodes.bin");
    const auto countDump = DumpFile(LorenciaObjectsCapture, "obj_count.bin");
    const auto worldDump = DumpFile(LorenciaObjectsCapture, "obj_gworld.bin");
    const auto sceneDump = DumpFile(LorenciaObjectsCapture, "obj_scene.bin");
    ASSERT_TRUE(path && nodesDump && countDump && worldDump && sceneDump);
    ASSERT_EQ(DumpInt32(*worldDump), 0) << "g_world de Lorencia";
    ASSERT_EQ(DumpInt32(*sceneDump), SceneOfTheCapture);
    const std::optional<std::int32_t> count = DumpInt32(*countDump);
    const std::optional<std::vector<TerrainObject>> objects = LoadObjects(*path, LorenciaObjectsCapture.world);
    const std::optional<std::vector<char>> memory = ReadFileBytes(*nodesDump);
    ASSERT_TRUE(count.has_value());
    ASSERT_TRUE(objects.has_value());
    ASSERT_TRUE(memory.has_value());
    ASSERT_EQ(memory->size() % NodeSize, 0u);

    std::vector<ObjectValues> loaded;
    for (const TerrainObject& object : *objects)
    {
        loaded.push_back(ObjectBits(object));
    }
    std::vector<ObjectValues> inMemory;
    const std::span<const std::byte> nodes = std::as_bytes(std::span(*memory));
    for (std::size_t node = 0; node < nodes.size(); node += NodeSize)
    {
        inMemory.push_back(NodeValues(nodes.subspan(node + NodeHeaderSize, NodeObjectSize)));
    }

    EXPECT_EQ(loaded.size(), static_cast<std::size_t>(*count));
    ASSERT_EQ(loaded.size(), inMemory.size());
    const auto [mine, theirs] = std::ranges::mismatch(loaded, inMemory);
    EXPECT_TRUE(mine == loaded.end()) << "el objeto " << (mine - loaded.begin())
                                      << " de los ordenados es distinto: " << ::testing::PrintToString(*mine)
                                      << " en MuRet y " << ::testing::PrintToString(*theirs) << " en memoria";
}
} // namespace
