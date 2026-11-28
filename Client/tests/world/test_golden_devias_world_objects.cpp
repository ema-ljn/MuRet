#include "WorldObjects/WorldObjects.h"

#include "Kernel/WorldObject.h"
#include "Terrain/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/WorldObjectVectors.h"

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The 2144 records of Data/world3/EncTerrain3.obj, created by MuRet on map 2 and scene 5, against the 256 lists of
// 0x7d325d0 read from the original's memory (P-C37 and Q-C186 of the client research; the vector is
// tests/vectors/game/devias_object_lists.txt).
namespace
{
constexpr std::uint8_t Devias = 2;
constexpr std::size_t DeviasRecords = 2144;
// 0x6066b1: an object's list is its sector x · 16 + its sector y.
constexpr int SectorsPerSide = 16;
constexpr std::size_t ListCount = 256;
// In the vector, the nodes that do not come from the file carry their type in hexadecimal.
constexpr std::string_view TypePrefix = "0x";
constexpr int Decimal = 10;
constexpr int Hexadecimal = 16;

using ObjectLists = std::array<std::vector<std::size_t>, ListCount>;

// A captured node that does not come from the file.
struct ForeignNode
{
    std::size_t list = 0;
    std::size_t place = 0;
    int type = 0;
    bool operator==(const ForeignNode&) const = default;
};

// GoogleTest uses it to print a list that differs from the expected one.
[[maybe_unused]] void PrintTo(const ForeignNode& node, std::ostream* out)
{
    *out << "lista " << node.list << " #" << node.place << ": 0x" << std::hex << node.type << std::dec;
}

struct CapturedLists
{
    ObjectLists records;
    std::vector<ForeignNode> foreign;
};

// The whole `word` as a number in `base`, or std::nullopt.
template <typename Number> std::optional<Number> ParseNumber(std::string_view word, int base)
{
    Number value{};
    const char* const end = word.data() + word.size();
    const auto [stop, error] = std::from_chars(word.data(), end, value, base);
    if (error != std::errc{} || stop != end)
    {
        return std::nullopt;
    }
    return value;
}

// A line of the vector: the list and, in order, the record index of each node or the type of those that do not come
// from the file. false if it cannot be parsed.
bool ReadListLine(const std::string& line, CapturedLists& out)
{
    std::istringstream words(line);
    std::string word;
    words >> word;
    const auto list = ParseNumber<std::size_t>(word, Decimal);
    if (!list.has_value() || *list >= ListCount)
    {
        return false;
    }
    for (std::size_t place = 0; words >> word; ++place)
    {
        if (word.starts_with(TypePrefix))
        {
            const auto type = ParseNumber<int>(std::string_view(word).substr(TypePrefix.size()), Hexadecimal);
            if (!type.has_value())
            {
                return false;
            }
            out.foreign.push_back({*list, place, *type});
            continue;
        }
        const auto record = ParseNumber<std::size_t>(word, Decimal);
        if (!record.has_value())
        {
            return false;
        }
        out.records.at(*list).push_back(*record);
    }
    return true;
}

// The vector's lists, or std::nullopt (with the test already failed) if they could not be read.
std::optional<CapturedLists> ReadCapturedLists(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        ADD_FAILURE() << "no se pudo abrir " << path;
        return std::nullopt;
    }
    CapturedLists lists;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        if (!ReadListLine(line, lists))
        {
            ADD_FAILURE() << "renglón que no se entiende: " << line;
            return std::nullopt;
        }
    }
    return lists;
}

// MuRet's lists: each object in its sector's, in the order WorldObjects::DrawOrder walks them.
ObjectLists ListsOf(std::span<const Client::Kernel::WorldObject> objects)
{
    ObjectLists lists;
    for (const std::size_t index : Client::WorldObjects::DrawOrder(objects))
    {
        const Client::Kernel::WorldObject& object = objects[index];
        const int list =
            (Client::WorldObjects::SectorX(object) * SectorsPerSide) + Client::WorldObjects::SectorY(object);
        lists.at(static_cast<std::size_t>(list)).push_back(index);
    }
    return lists;
}

class GoldenDeviasWorldObjects : public Client::Tests::GoldenDataTest
{
};

// The 2144 records are created, also 492 and 493, with y at NaN, in list 32 (Q-C186), and each list has the file's
// records in order. The two extra nodes, the first of lists 176 and 177, do not come from the file: 0x616ec0 creates
// them when loading map 2 (obj_donkey 0x201 and obj_flag 0x202), and here the test only checks that they are those two.
TEST_F(GoldenDeviasWorldObjects, EveryListEqualsTheOriginalsMemory)
{
    const auto captured =
        ReadCapturedLists(Client::Tests::World55VectorsDir().parent_path() / "game" / "devias_object_lists.txt");
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->foreign, (std::vector<ForeignNode>{{176, 0, 0x201}, {177, 0, 0x202}}));

    const auto records = Client::Terrain::LoadWorldObjects(m_dataRoot, Devias);
    ASSERT_TRUE(records.HasValue()) << records.Error();
    ASSERT_EQ(records.Value().size(), DeviasRecords);
    Client::Kernel::WorldObjectRegistrations registrations;
    Client::WorldObjects::WorldObjectCreationContext context{Devias, Client::WorldObjects::ObjectScene::Other,
                                                             registrations};
    const auto created = Client::WorldObjects::CreateWorldObjects(records.Value(), context);
    ASSERT_FALSE(created.failure.has_value());
    ASSERT_EQ(created.objects.size(), DeviasRecords) << "uno por registro: el índice del objeto es el del registro";

    const ObjectLists lists = ListsOf(created.objects);
    for (std::size_t list = 0; list < ListCount; ++list)
    {
        EXPECT_EQ(lists[list], captured->records[list]) << "lista " << list;
    }
}
} // namespace
