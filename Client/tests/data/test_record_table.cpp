#include "Data/Tables/RecordTable.h"

#include "Core/Xor3.h"
#include "support/BytePattern.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// Client::Data::Tables::RecordTable: los registros de largo fijo de las tablas .bmd.
namespace
{
using Client::Core::Xor3;
using Client::Data::Tables::RecordTable;
using Client::Tests::BytePattern;

// Un largo que no es múltiplo de 3, para que se note que la clave vuelve a empezar en cada registro.
using SmallTable = RecordTable<4>;

// Record() se puede pedir a una tabla que sigue viva, y no a una temporal.
template <typename Table>
concept ReadsRecords = requires(Table&& table) { std::forward<Table>(table).Record(0); };
static_assert(ReadsRecords<SmallTable&>);
static_assert(ReadsRecords<const SmallTable&>);
static_assert(!ReadsRecords<SmallTable>);
static_assert(!ReadsRecords<const SmallTable>);

// El registro tiene el largo en el tipo: un std::span de 4 bytes, no de los que vengan.
static_assert(std::is_same_v<decltype(std::declval<const SmallTable&>().Record(0)),
                             std::optional<std::span<const std::byte, 4>>>);

bool HasBytes(std::optional<std::span<const std::byte, 4>> record, std::array<std::byte, 4> expected)
{
    return record.has_value() && std::ranges::equal(*record, expected);
}

// Sobre ceros, cada registro queda en FC CF AB FC: el original descifra registro por registro
// (0x5496c0 con el largo del registro), así que la clave arranca de nuevo. De punta a punta, el
// segundo registro daría CF AB FC CF.
TEST(RecordTableTest, TheKeyStartsAgainOnEachRecord)
{
    const std::vector<std::byte> zeros(8);

    const SmallTable table(zeros);

    const std::array<std::byte, 4> key = {std::byte{0xFC}, std::byte{0xCF}, std::byte{0xAB}, std::byte{0xFC}};
    ASSERT_EQ(table.Count(), 2u);
    EXPECT_TRUE(HasBytes(table.Record(0), key));
    EXPECT_TRUE(HasBytes(table.Record(1), key));
}

// Lo que se cifró registro por registro vuelve a estar en claro.
TEST(RecordTableTest, GivesBackThePlainRecords)
{
    const std::vector<std::byte> plain = BytePattern(12, 37, 11);
    std::vector<std::byte> encrypted = plain;
    for (std::size_t offset = 0; offset < encrypted.size(); offset += 4)
    {
        Xor3(std::span(encrypted).subspan(offset, 4));
    }

    const SmallTable table(encrypted);

    ASSERT_EQ(table.Count(), 3u);
    for (std::size_t index = 0; index < table.Count(); ++index)
    {
        const std::optional<std::span<const std::byte, 4>> record = table.Record(index);
        ASSERT_TRUE(record.has_value()) << "el registro " << index;
        EXPECT_TRUE(std::ranges::equal(*record, std::span(plain).subspan(index * 4, 4))) << "el registro " << index;
    }
}

// Los bytes que no llegan a un registro no se copian, y no hay registro para ellos.
TEST(RecordTableTest, AnIncompleteRecordIsLeftOut)
{
    const std::vector<std::byte> bytes(4 + 3);

    const SmallTable table(bytes);

    EXPECT_EQ(table.Count(), 1u);
    EXPECT_TRUE(table.Record(0).has_value());
    EXPECT_FALSE(table.Record(1).has_value());
}

// Tampoco hay registro para un índice que, multiplicado por el largo, da la vuelta y caería adentro.
TEST(RecordTableTest, IndexPastTheTableHasNoRecord)
{
    constexpr std::size_t Largest = std::numeric_limits<std::size_t>::max();
    const SmallTable table(std::vector<std::byte>(8));

    EXPECT_FALSE(table.Record(2).has_value());
    EXPECT_FALSE(table.Record(Largest / 4 + 1).has_value());
    EXPECT_FALSE(table.Record(Largest).has_value());
}

TEST(RecordTableTest, AnEmptyFileGivesAnEmptyTable)
{
    const SmallTable table(std::span<const std::byte>{});

    EXPECT_EQ(table.Count(), 0u);
    EXPECT_FALSE(table.Record(0).has_value());
}
} // namespace
