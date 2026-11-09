#include "World/WorldObjects.h"

#include "World/WorldObjectRegistrations.h"
#include "World/WorldTerrain.h"

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

// Los 2.144 registros de Data/world3/EncTerrain3.obj, creados por MuRet en el mapa 2 y la escena 5, contra
// las 256 listas de 0x7d325d0 que se leyeron de la memoria del original (P-C37 y Q-C186 del research del
// cliente; el vector es tests/vectors/game/devias_object_lists.txt).
namespace
{
constexpr std::uint8_t Devias = 2;
constexpr std::size_t DeviasRecords = 2144;
// 0x6066b1: la lista de un objeto es su sector x · 16 + su sector y.
constexpr int SectorsPerSide = 16;
constexpr std::size_t ListCount = 256;
// En el vector, los nodos que no salen del archivo van con su tipo en hexadecimal.
constexpr std::string_view TypePrefix = "0x";
constexpr int Decimal = 10;
constexpr int Hexadecimal = 16;

using ObjectLists = std::array<std::vector<std::size_t>, ListCount>;

// Un nodo de la captura que no sale del archivo: su lista, su lugar en ella y su tipo.
struct ForeignNode
{
    std::size_t list = 0;
    std::size_t place = 0;
    int type = 0;
    bool operator==(const ForeignNode&) const = default;
};

// GoogleTest lo usa para mostrar una lista distinta de la esperada.
[[maybe_unused]] void PrintTo(const ForeignNode& node, std::ostream* out)
{
    *out << "lista " << node.list << " #" << node.place << ": 0x" << std::hex << node.type << std::dec;
}

struct CapturedLists
{
    ObjectLists records;
    std::vector<ForeignNode> foreign;
};

// `word` entero como un número en `base`, o std::nullopt.
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

// Un renglón del vector: la lista y, en su orden, el índice del registro de cada nodo o el tipo de los
// que no salen del archivo. false si no se entiende.
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

// Las listas del vector, o std::nullopt (con el test ya fallado) si no se pudo leer.
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

// Las listas de MuRet: cada objeto en la de su sector, en el orden en que los recorre World::DrawOrder.
ObjectLists ListsOf(std::span<const Client::World::WorldObject> objects)
{
    ObjectLists lists;
    for (const std::size_t index : Client::World::DrawOrder(objects))
    {
        const Client::World::WorldObject& object = objects[index];
        const int list = (Client::World::SectorX(object) * SectorsPerSide) + Client::World::SectorY(object);
        lists.at(static_cast<std::size_t>(list)).push_back(index);
    }
    return lists;
}

class GoldenDeviasWorldObjects : public Client::Tests::GoldenDataTest
{
};

// Los 2.144 registros se crean, también el 492 y el 493, con la y en NaN, en la lista 32 (Q-C186), y cada
// lista tiene los del archivo en su orden. Los dos nodos de más, el primero de las listas 176 y 177, no
// salen del archivo: los crea 0x616ec0 al cargar el mapa 2 (obj_donkey 0x201 y obj_flag 0x202), y acá
// solo se comprueba que son esos dos.
TEST_F(GoldenDeviasWorldObjects, EveryListEqualsTheOriginalsMemory)
{
    const auto captured =
        ReadCapturedLists(Client::Tests::World55VectorsDir().parent_path() / "game" / "devias_object_lists.txt");
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->foreign, (std::vector<ForeignNode>{{176, 0, 0x201}, {177, 0, 0x202}}));

    const auto records = Client::World::LoadWorldObjects(m_dataRoot, Devias);
    ASSERT_TRUE(records.HasValue()) << records.Error();
    ASSERT_EQ(records.Value().size(), DeviasRecords);
    Client::World::WorldObjectRegistrations registrations;
    Client::World::WorldObjectCreationContext context{Devias, Client::World::ObjectScene::Other, registrations};
    const auto created = Client::World::CreateWorldObjects(records.Value(), context);
    ASSERT_FALSE(created.failure.has_value());
    ASSERT_EQ(created.objects.size(), DeviasRecords) << "uno por registro: el índice del objeto es el del registro";

    const ObjectLists lists = ListsOf(created.objects);
    for (std::size_t list = 0; list < ListCount; ++list)
    {
        EXPECT_EQ(lists[list], captured->records[list]) << "lista " << list;
    }
}
} // namespace
