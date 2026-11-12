#include "Data/Text/SlideTable.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include "support/ExpectedError.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Client::Data::Text::LoadSlideTable: Slide.bmd (0x46b3b0), con textos inventados.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::LoadSlideTable;
using Client::Data::Text::SlideGroupSize;
using Client::Data::Text::SlideTableSize;
using Client::Data::Text::SlideTextSize;
using Client::Tests::ErrorOf;

void PutDword(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
}

void PutText(std::vector<std::byte>& bytes, std::size_t at, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i)
        bytes[at + i] = static_cast<std::byte>(text[i]);
}

std::size_t GroupAt(std::size_t group)
{
    return 8 + group * SlideGroupSize;
}

std::size_t TextAt(std::size_t group, std::size_t text)
{
    return GroupAt(group) + 8 + text * SlideTextSize;
}

// En claro: el período, la velocidad y los tramos vacíos.
std::vector<std::byte> PlainTable(std::uint32_t period, float speed)
{
    std::vector<std::byte> bytes(SlideTableSize);
    PutDword(bytes, 0, period);
    PutDword(bytes, 4, std::bit_cast<std::uint32_t>(speed));
    return bytes;
}

std::vector<std::byte> Encoded(std::vector<std::byte> bytes)
{
    Client::Core::Xor3(bytes);
    return bytes;
}

TEST(SlideTableTest, ReadsThePeriodTheSpeedAndTheGroups)
{
    std::vector<std::byte> plain = PlainTable(45, 3.5f);
    PutDword(plain, GroupAt(0), 12);
    PutDword(plain, GroupAt(0) + 4, 2);
    PutText(plain, TextAt(0, 0), "first synthetic line");
    PutText(plain, TextAt(0, 1), "second one");
    PutDword(plain, GroupAt(4), 400);
    PutDword(plain, GroupAt(4) + 4, 0xffffffffU);

    const auto table = LoadSlideTable(Encoded(plain));

    ASSERT_TRUE(table.HasValue());
    EXPECT_EQ(table.Value().periodSeconds, 45);
    EXPECT_EQ(table.Value().speed, 3.5f);
    EXPECT_EQ(table.Value().groups[0].maxLevel, 12);
    EXPECT_EQ(table.Value().groups[0].count, 2);
    EXPECT_EQ(table.Value().groups[0].texts, (std::vector<std::string>{"first synthetic line", "second one"}));
    // Una cuenta negativa no lee ningún texto, pero queda para el rand() de 0x46b680.
    EXPECT_EQ(table.Value().groups[4].maxLevel, 400);
    EXPECT_EQ(table.Value().groups[4].count, -1);
    EXPECT_TRUE(table.Value().groups[4].texts.empty());
}

// 0x46b4d7..0x46b56b: con más de 32, sigue de a 0x100 en el tramo de al lado, sobre su tope y su cuenta.
TEST(SlideTableTest, ACountOverThirtyTwoReadsIntoTheNextGroup)
{
    std::vector<std::byte> plain = PlainTable(10, 2.5f);
    PutDword(plain, GroupAt(0), 1);
    PutDword(plain, GroupAt(0) + 4, 33);
    PutDword(plain, GroupAt(1), 0x41);

    const auto table = LoadSlideTable(Encoded(plain));

    ASSERT_TRUE(table.HasValue());
    ASSERT_EQ(table.Value().groups[0].texts.size(), 33U);
    EXPECT_EQ(table.Value().groups[0].texts[32], "A");
}

// El original no mira lo que leyó fread (Q-C516).
TEST(SlideTableTest, AShortFileIsAnError)
{
    const std::vector<std::byte> file(SlideTableSize - 1);

    EXPECT_EQ(ErrorOf(LoadSlideTable(file)), DataError::FileTooShort);
}

// Un texto sin cero hasta el final seguiría en la pila del original.
TEST(SlideTableTest, ATextPastTheEndIsInvalid)
{
    std::vector<std::byte> plain = PlainTable(10, 2.5f);
    PutDword(plain, GroupAt(4), 1);
    PutDword(plain, GroupAt(4) + 4, 32);
    for (std::size_t at = TextAt(4, 31); at < SlideTableSize; ++at)
        plain[at] = std::byte{'x'};

    EXPECT_EQ(ErrorOf(LoadSlideTable(Encoded(plain))), DataError::InvalidData);
}

// Lo que sobra después de 0xa030 bytes no se lee.
TEST(SlideTableTest, ATailIsIgnored)
{
    std::vector<std::byte> file = Encoded(PlainTable(7, 1.0f));
    file.resize(SlideTableSize + 16, std::byte{0x55});

    const auto table = LoadSlideTable(file);

    ASSERT_TRUE(table.HasValue());
    EXPECT_EQ(table.Value().periodSeconds, 7);
}
} // namespace
