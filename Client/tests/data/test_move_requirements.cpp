#include "Data/Text/MoveRequirements.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Client::Data::Text::LoadMoveRequirements: movereq.bmd (0x448440), la cuenta en claro y registros de 0x50 con el
// XOR3 de 0x449770 desde el primer byte de cada uno. Los destinos son inventados.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::LoadMoveRequirements;
using Client::Data::Text::MoveRecordSize;
using Client::Data::Text::MoveRequirement;

struct FakeMove
{
    std::int32_t index = 0;
    std::string_view name;
    std::string_view searchName;
    std::int32_t level = 0;
    std::uint32_t zen = 0;
    std::int32_t gate = 0;
};

void PutWord(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xffu);
}

void PutText(std::span<std::byte> bytes, std::size_t offset, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i)
        bytes[offset + i] = static_cast<std::byte>(text[i]);
}

// La cuenta y cada registro cifrado por su cuenta, como los lee 0x448440.
std::vector<std::byte> FileWith(std::int32_t count, std::span<const FakeMove> moves)
{
    std::vector<std::byte> file(4 + moves.size() * MoveRecordSize);
    PutWord(file, 0, static_cast<std::uint32_t>(count));
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
        const std::span<std::byte> record = std::span(file).subspan(4 + i * MoveRecordSize, MoveRecordSize);
        PutWord(record, 0, static_cast<std::uint32_t>(moves[i].index));
        PutText(record, 4, moves[i].name);
        PutText(record, 0x24, moves[i].searchName);
        PutWord(record, 0x44, static_cast<std::uint32_t>(moves[i].level));
        PutWord(record, 0x48, moves[i].zen);
        PutWord(record, 0x4c, static_cast<std::uint32_t>(moves[i].gate));
        Client::Core::Xor3(record);
    }
    return file;
}

std::array<char, 0x20> SearchNameOf(std::string_view text)
{
    std::array<char, 0x20> name{};
    for (std::size_t i = 0; i < text.size(); ++i)
        name[i] = text[i];
    return name;
}

constexpr std::array<FakeMove, 3> ThreeMoves{{
    {.index = 1, .name = "Campo", .searchName = "campo", .level = 10, .zen = 1500, .gate = 7},
    {.index = 2, .name = "Bosque", .searchName = "Bosque", .level = 80, .zen = 0x80000000u, .gate = 9},
    {.index = 3, .name = "Pantano", .searchName = "pantano", .level = -1, .zen = 0, .gate = 11},
}};

// 0x50 no es múltiplo de 3: el XOR3 vuelve a FC en cada registro.
TEST(MoveRequirementsTest, EachRecordIsDecipheredFromItsFirstByte)
{
    const std::vector<std::byte> file = FileWith(3, ThreeMoves);

    const auto moves = LoadMoveRequirements(file);

    ASSERT_TRUE(moves.HasValue());
    ASSERT_EQ(moves.Value().size(), 3U);
    EXPECT_EQ(
        moves.Value()[0],
        (MoveRequirement{
            .index = 1, .name = "Campo", .searchName = SearchNameOf("campo"), .level = 10, .zen = 1500, .gate = 7}));
    EXPECT_EQ(moves.Value()[1].name, "Bosque");
    EXPECT_EQ(moves.Value()[1].zen, 0x80000000u);
    EXPECT_EQ(moves.Value()[2].name, "Pantano");
    EXPECT_EQ(moves.Value()[2].level, -1);
    EXPECT_EQ(moves.Value()[2].gate, 11);
}

// La cuenta manda: lo que sobra no se lee, y con 0 o una negativa no hay ninguno.
TEST(MoveRequirementsTest, TheCountDecidesHowManyAreRead)
{
    const auto two = LoadMoveRequirements(FileWith(2, ThreeMoves));
    ASSERT_TRUE(two.HasValue());
    EXPECT_EQ(two.Value().size(), 2U);

    const auto none = LoadMoveRequirements(FileWith(0, ThreeMoves));
    ASSERT_TRUE(none.HasValue());
    EXPECT_TRUE(none.Value().empty());

    const auto negative = LoadMoveRequirements(FileWith(-5, ThreeMoves));
    ASSERT_TRUE(negative.HasValue());
    EXPECT_TRUE(negative.Value().empty());

    // fread de la cuenta con menos de cuatro bytes deja en 0 los que faltan: vacío, sin error.
    const auto empty = LoadMoveRequirements({});
    ASSERT_TRUE(empty.HasValue());
    EXPECT_TRUE(empty.Value().empty());
}

TEST(MoveRequirementsTest, AMissingRecordIsTooShort)
{
    std::vector<std::byte> file = FileWith(3, ThreeMoves);
    file.pop_back();

    const auto moves = LoadMoveRequirements(file);

    ASSERT_FALSE(moves.HasValue());
    EXPECT_EQ(moves.Error(), DataError::FileTooShort);
}

// strlen sigue en el registro: un nombre de 32 letras sigue con el de +0x24, y uno sin '\0' hasta el final del
// registro saldría del bloque.
TEST(MoveRequirementsTest, ANameRunsIntoTheSearchNameAndMustEndInItsRecord)
{
    constexpr std::string_view Long = "abcdefghijklmnopqrstuvwxyzABCDEF";
    const std::array<FakeMove, 1> longName{
        {{.index = 1, .name = Long, .searchName = "fin", .level = 1, .zen = 0, .gate = 0}}};

    const auto moves = LoadMoveRequirements(FileWith(1, longName));

    ASSERT_TRUE(moves.HasValue());
    EXPECT_EQ(moves.Value()[0].name, "abcdefghijklmnopqrstuvwxyzABCDEFfin");

    std::vector<std::byte> unended = FileWith(1, longName);
    const std::span<std::byte> record = std::span(unended).subspan(4, MoveRecordSize);
    Client::Core::Xor3(record);
    for (std::size_t i = 4; i < MoveRecordSize; ++i)
        record[i] = std::byte{'x'};
    Client::Core::Xor3(record);
    const auto broken = LoadMoveRequirements(unended);
    ASSERT_FALSE(broken.HasValue());
    EXPECT_EQ(broken.Error(), DataError::InvalidData);
}
} // namespace
