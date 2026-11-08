#include "World/ItemState.h"

#include "Data/Tables/ItemTable.h"
#include "Scenes/GameData.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace
{
using Client::World::InitializeItemState;
using Client::World::ItemState;
using Metadata = std::array<std::byte, Client::Data::Tables::ItemRecordSize>;

void Initialize(ItemState& item, const Metadata& metadata, std::uint32_t b1 = 0, std::uint32_t b3 = 0,
                std::uint32_t b4 = 0)
{
    const std::uint16_t contextWord = 1000;
    InitializeItemState(item, metadata, {b1, b3, b4}, contextWord, {});
}

// Detecta inicialización completa indebida, borrado de valores previos y stores omitidos.
TEST(ItemStateTest, CopiesMetadataAndPreservesUntouchedFields)
{
    ItemState item;
    item.id = 0x20;
    item.durability = 77;
    item.flags1b = 88;
    item.flags1c = 99;
    item.occupied = 0x12345678;
    item.gridColumn = 13;
    item.gridRow = 27;
    item.optionBytes.fill(0xa5);
    Metadata metadata{};
    metadata[0x1e] = std::byte{9};
    metadata[0x2b] = std::byte{10};
    metadata[0x26] = std::byte{11};
    metadata[0x27] = std::byte{12};
    metadata[0x28] = std::byte{13};
    metadata[0x29] = std::byte{14};
    metadata[0x2a] = std::byte{15};
    metadata[0x2c] = std::byte{16};
    metadata[0x2f] = std::byte{17};
    Initialize(item, metadata);
    EXPECT_EQ(item.scalar1Low, 0u);
    EXPECT_EQ(item.byte0a, 9);
    EXPECT_EQ(item.byte17, 10);
    EXPECT_EQ(item.word0c, 11);
    EXPECT_EQ(item.word0e, 12);
    EXPECT_EQ(item.byte10, 13);
    EXPECT_EQ(item.word12, 14);
    EXPECT_EQ(item.word14, 15);
    EXPECT_EQ(item.word18, 16);
    EXPECT_EQ(item.byte16, 8);
    EXPECT_EQ(item.word1e, 0);
    EXPECT_EQ(item.word20, 0);
    EXPECT_EQ(item.word22, 0);
    EXPECT_EQ(item.word24, 0);
    EXPECT_EQ(item.word26, 0);
    EXPECT_EQ(item.word28, 0);
    EXPECT_EQ(item.optionCount, 0);
    EXPECT_EQ(item.optionBytes[0], 0xa5);
    EXPECT_EQ(item.OptionValue(0), 0xa5);
    EXPECT_EQ(item.durability, 77);
    EXPECT_EQ(item.flags1b, 88);
    EXPECT_EQ(item.flags1c, 99);
    EXPECT_EQ(item.occupied, 0x12345678u);
    EXPECT_EQ(item.gridColumn, 13);
    EXPECT_EQ(item.gridRow, 27);
}

// Los catorce/quince usan el default de tabla, no saturan el bonus en trece.
TEST(ItemStateTest, CoversAllLevelThresholdsWithIndependentLiterals)
{
    constexpr std::array<std::uint16_t, 16> Expected{1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 32, 37, 43, 50, 28, 28};
    Metadata metadata{};
    metadata[0x26] = std::byte{1};
    for (std::uint32_t level = 0; level < Expected.size(); ++level)
    {
        SCOPED_TRACE(level);
        ItemState item;
        Initialize(item, metadata, level << 3);
        EXPECT_EQ(item.word0c, Expected[level]);
    }
}

TEST(ItemStateTest, RetainsFullThirdScalarForThe180bOverride)
{
    Metadata metadata{};
    constexpr std::array<std::uint16_t, 16> Expected{30,     60,     90,     130,    170,    210,    300,    0xabcd,
                                                     0xabcd, 0xabcd, 0xabcd, 0xabcd, 0xabcd, 0xabcd, 0xabcd, 0xabcd};
    for (std::uint32_t level = 0; level < Expected.size(); ++level)
    {
        SCOPED_TRACE(level);
        ItemState item;
        item.id = 0x180b;
        Initialize(item, metadata, 0xdead0000 | (level << 3), 0x1234abcd, 0xaabb0000);
        EXPECT_EQ(item.word22, Expected[level]);
    }
}

TEST(ItemStateTest, NinthTagOverwritesTheFirstValueAndKeepsTagOnlyValues)
{
    Metadata metadata{};
    metadata[0x23] = std::byte{0x70};
    ItemState item;
    item.id = 0x0001;
    item.optionBytes.fill(0xa5);
    Initialize(item, metadata, 0x85, 0x3f);
    constexpr std::array<std::uint8_t, 9> Tags{0x70, 0x54, 0x50, 0x5c, 0x5d, 0x5e, 0x61, 0x62, 0x63};
    ASSERT_EQ(item.optionCount, 9);
    for (std::uint8_t index = 0; index < Tags.size(); ++index)
    {
        EXPECT_EQ(item.OptionTag(index), Tags[index]);
    }
    EXPECT_EQ(item.OptionValue(0), 0x63);
    EXPECT_EQ(item.OptionValue(1), 0xa5);
    EXPECT_EQ(item.OptionValue(2), 4);
    EXPECT_EQ(item.OptionValue(3), 0xa5);
    EXPECT_EQ(item.OptionValue(4), 50);
    EXPECT_EQ(item.OptionValue(5), 2);
    EXPECT_EQ(item.OptionValue(6), 7);
    EXPECT_EQ(item.OptionValue(7), 0xa5);
    EXPECT_EQ(item.OptionValue(8), 0xa5);
    EXPECT_EQ(item.word1e, 5);
}

TEST(ItemStateTest, NinthArmorTagOverwritesValueZeroWithoutClearingTheOtherTagOnlyValues)
{
    Metadata metadata{};
    metadata[0x23] = std::byte{0x70};
    ItemState item;
    item.id = 0xc00;
    item.optionBytes.fill(0xa5);
    Initialize(item, metadata, 0x85, 0x3f);
    constexpr std::array<std::uint8_t, 9> Tags{0x70, 0x54, 0x52, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b};
    constexpr std::array<std::uint8_t, 9> Values{0x5b, 0xa5, 5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
    ASSERT_EQ(item.optionCount, 9);
    for (std::uint8_t index = 0; index < Tags.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(item.OptionTag(index), Tags[index]);
        EXPECT_EQ(item.OptionValue(index), Values[index]);
    }
    EXPECT_EQ(item.word1e, 5);
}

TEST(ItemStateTest, AppliesSpecialOptionFamiliesInOrder)
{
    Metadata metadata{};
    ItemState item;
    item.id = 0x1803;
    Initialize(item, metadata, 0x0b, 0x7f);
    constexpr std::array<std::uint8_t, 6> Tags{0x64, 0x65, 0x66, 0x67, 0x61, 0x55};
    constexpr std::array<std::uint8_t, 6> Values{55, 55, 3, 50, 5, 7};
    ASSERT_EQ(item.optionCount, 6);
    for (std::uint8_t index = 0; index < Tags.size(); ++index)
    {
        EXPECT_EQ(item.OptionTag(index), Tags[index]);
        EXPECT_EQ(item.OptionValue(index), Values[index]);
    }
    item.id = 0x1a1e;
    Initialize(item, metadata, 0x0b, 0x3f);
    constexpr std::array<std::uint8_t, 7> OtherTags{0x96, 0x94, 0x64, 0x65, 0x66, 0x90, 0x50};
    constexpr std::array<std::uint8_t, 7> OtherValues{17, 22, 55, 55, 3, 15, 12};
    ASSERT_EQ(item.optionCount, 7);
    for (std::uint8_t index = 0; index < OtherTags.size(); ++index)
    {
        EXPECT_EQ(item.OptionTag(index), OtherTags[index]);
        EXPECT_EQ(item.OptionValue(index), OtherValues[index]);
    }
}

TEST(ItemStateTest, ClassifiesSignedIdsWithPriorityAndLevelExceptions)
{
    struct Case
    {
        std::uint16_t id;
        std::uint8_t level;
        std::uint8_t expected;
    };
    constexpr std::array Cases{
        Case{0, 0, 0},         Case{0x800, 0, 1},   Case{0x807, 0, 1},     Case{0x808, 0, 0},     Case{0x811, 0, 1},
        Case{0xc00, 0, 1},     Case{0xe00, 0, 2},   Case{0x1000, 0, 3},    Case{0x1200, 0, 4},    Case{0x1400, 0, 5},
        Case{0x1600, 0, 6},    Case{0x1800, 0, 7},  Case{0x1807, 0, 0xff}, Case{0x1a05, 0, 1},    Case{0x1a00, 0, 8},
        Case{0x1a08, 0, 10},   Case{0x1a14, 0, 10}, Case{0x1a14, 3, 10},   Case{0x1a14, 4, 9},    Case{0x1a18, 0, 10},
        Case{0x1a19, 0, 9},    Case{0x1bff, 0, 9},  Case{0x1c00, 0, 0xff}, Case{0xffff, 0, 0xff}, Case{0x7ff, 0, 0},
        Case{0x810, 0, 0},     Case{0x812, 0, 0},   Case{0xbff, 0, 0},     Case{0xdff, 0, 1},     Case{0xfff, 0, 2},
        Case{0x11ff, 0, 3},    Case{0x13ff, 0, 4},  Case{0x15ff, 0, 5},    Case{0x17ff, 0, 6},    Case{0x1806, 0, 7},
        Case{0x19ff, 0, 0xff}, Case{0x1a07, 0, 8},  Case{0x1a0b, 0, 10},   Case{0x1a0c, 0, 9},    Case{0x1a13, 0, 9},
        Case{0x1a15, 0, 10},   Case{0x1a1c, 0, 9},  Case{0x8000, 0, 0xff}};
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        Initialize(item, Metadata{}, static_cast<std::uint32_t>(entry.level) << 3);
        EXPECT_EQ(item.itemClass, entry.expected);
    }
}
// Detecta bonus P/Q ausentes, uso de T27 en vez de T26 y requisitos antes de escala correcta.
TEST(ItemStateTest, CalculatesAllMetadataFieldsAndSeparateArmorFormula)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{100};
    metadata[0x26] = std::byte{20};
    metadata[0x27] = std::byte{30};
    metadata[0x28] = std::byte{50};
    metadata[0x29] = std::byte{60};
    metadata[0x2a] = std::byte{70};
    metadata[0x2f] = std::byte{40};
    for (const std::size_t offset : std::array<std::size_t, 6>{0x30, 0x32, 0x34, 0x36, 0x38, 0x3a})
    {
        metadata[offset] = std::byte{100};
    }
    ItemState item;
    item.id = 1;
    Initialize(item, metadata, 10 << 3, 1, 1);
    EXPECT_EQ(item.word0c, 69);
    EXPECT_EQ(item.word0e, 79);
    EXPECT_EQ(item.byte10, 98);
    EXPECT_EQ(item.byte16, 65);
    EXPECT_EQ(item.word12, 130);
    EXPECT_EQ(item.word14, 101);
    EXPECT_EQ(item.word1e, 485);
    EXPECT_EQ(item.word20, 485);
    EXPECT_EQ(item.word22, 640);
    EXPECT_EQ(item.word24, 485);
    EXPECT_EQ(item.word26, 100);
    EXPECT_EQ(item.word28, 120);
    item.id = 0xc00;
    Initialize(item, metadata, 10 << 3, 1, 1);
    EXPECT_EQ(item.word12, 82);
    item.id = 0x406;
    Initialize(item, metadata, 10 << 3, 1, 1);
    EXPECT_EQ(item.word0c, 74);
    item.id = 0x806;
    Initialize(item, metadata, 10 << 3, 1, 1);
    EXPECT_EQ(item.word0c, 89);
    item.id = 0xa07;
    Initialize(item, metadata, 10 << 3, 1, 1);
    EXPECT_EQ(item.word0c, 84);
}

// Expected derivados del contrato ASM 54bf40 c338..c433: 2*min(L,9)+h para estas familias.
TEST(ItemStateTest, UsesTheTwofoldWord12LevelAdjustmentOnlyForTheSpecialFamilies)
{
    Metadata metadata{};
    metadata[0x29] = std::byte{60};
    struct Case
    {
        std::uint16_t id;
        std::uint16_t expected;
    };
    constexpr std::array Cases{Case{0x1802, 91}, Case{0x1803, 82}, Case{0x1804, 82}, Case{0x1805, 82},
                               Case{0x1806, 82}, Case{0x1807, 91}, Case{0x1a1e, 82}, Case{0x1a1f, 91}};
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        Initialize(item, metadata, 10 << 3);
        EXPECT_EQ(item.word12, entry.expected);
    }
}

TEST(ItemStateTest, LowTwoB4BitsOfThreeNeitherEnablePNorRestoreSuppressedQ)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{100};
    metadata[0x26] = std::byte{20};
    metadata[0x29] = std::byte{60};
    metadata[0x30] = std::byte{100};
    ItemState item;
    item.id = 0x1803;
    Initialize(item, metadata, 0, 1, 0xffff0003);
    EXPECT_EQ(item.word0c, 20);
    EXPECT_EQ(item.word12, 60);
    EXPECT_EQ(item.word1e, 320);
    item.id = 1;
    Initialize(item, metadata, 0, 1, 0xffff0003);
    EXPECT_EQ(item.word0c, 30);
    EXPECT_EQ(item.word12, 91);
    EXPECT_EQ(item.word1e, 395);
}

TEST(ItemStateTest, WrapsByte10WithAndWithoutTheQBonus)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{1};
    metadata[0x28] = std::byte{255};
    ItemState item;
    Initialize(item, metadata, 13 << 3);
    EXPECT_EQ(item.byte10, 48);
    Initialize(item, metadata, 13 << 3, 1);
    // 255 + (25*255/1+5) +27+22 =6684, cuyo low8 es28.
    EXPECT_EQ(item.byte10, 28);
}

TEST(ItemStateTest, AppliesTheSpecialBonusToWord0eAndByte16OnlyWithANonzeroDenominator)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{100};
    metadata[0x26] = std::byte{20};
    metadata[0x27] = std::byte{30};
    metadata[0x2f] = std::byte{40};
    ItemState item;
    item.id = 0x806;
    Initialize(item, metadata, 10 << 3, 1, 1);
    // k=30 distingue ambos campos de la fórmula ordinaria (79/65).
    EXPECT_EQ(item.word0e, 99);
    EXPECT_EQ(item.byte16, 72);
    metadata[0x20] = std::byte{0};
    Initialize(item, metadata, 10 << 3, 1, 1);
    EXPECT_EQ(item.word0e, 66);
    EXPECT_EQ(item.byte16, 56);
}

TEST(ItemStateTest, KeepsTheDenominatorAndItsPlusThirtyWiderThanAWord)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{0xff};
    metadata[0x21] = std::byte{0xff};
    metadata[0x26] = std::byte{1};
    metadata[0x29] = std::byte{1};
    metadata[0x2f] = std::byte{1};
    metadata[0x30] = std::byte{100};
    ItemState item;
    Initialize(item, metadata, 0, 0, 1);
    EXPECT_EQ(item.word0c, 1650);
    EXPECT_EQ(item.word12, 15299);
    EXPECT_EQ(item.byte16, 38);
    EXPECT_EQ(item.word1e, 92);
}

TEST(ItemStateTest, WrapsByteBeforeItsLogicalShift)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{1};
    metadata[0x2f] = std::byte{255};
    ItemState item;
    Initialize(item, metadata, 13 << 3, 1);
    EXPECT_EQ(item.byte16, 40);
    item.id = 0x408;
    Initialize(item, metadata, 13 << 3, 1);
    EXPECT_EQ(item.byte16, 14);
}

TEST(ItemStateTest, WrapsProductsTo32BitsBeforeSignedDivisionTruncatingTowardZero)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{0x40};
    metadata[0x21] = std::byte{0x9c}; // e=40000
    for (const std::size_t offset : std::array<std::size_t, 5>{0x30, 0x32, 0x34, 0x36, 0x38})
    {
        metadata[offset] = std::byte{0xff};
        metadata[offset + 1] = std::byte{0xff};
    }
    ItemState item;
    Initialize(item, metadata, 15 << 3, 1);
    // low32(40070*65535*3)=3582995054 => -711972242 /100=-7119722.
    EXPECT_EQ(item.word1e, 23722);
    EXPECT_EQ(item.word20, 23722);
    EXPECT_EQ(item.word22, 3660);
    EXPECT_EQ(item.word24, 23722);
    EXPECT_EQ(item.word26, 65535); // sobrescritura raw posterior al requisito
    item.id = 0x1c17;
    Initialize(item, metadata, 15 << 3, 1);
    EXPECT_EQ(item.word26, 170);
}

TEST(ItemStateTest, PreservesOverridePriorityAndTheBonusExclusions)
{
    Metadata metadata{};
    metadata[0x3a] = std::byte{100};
    struct Case
    {
        std::uint16_t id;
        std::uint16_t expected;
    };
    constexpr std::array Cases{
        Case{0x1803, 110}, Case{0x1807, 120}, Case{0x1a00, 120}, Case{0x1a25, 100}, Case{0x1a01, 128},
        Case{0x0016, 100}, Case{0x0815, 100}, Case{0x0e1d, 100}, Case{0x1021, 100}, Case{0x1622, 120},
        Case{0x0e1c, 120}, Case{0x0e21, 100}, Case{0x0e22, 120}, Case{0x101c, 120}, Case{0x101d, 100},
        Case{0x1022, 120}, Case{0x121c, 120}, Case{0x121d, 100}, Case{0x1221, 100}, Case{0x1222, 120},
        Case{0x141c, 120}, Case{0x141d, 100}, Case{0x1421, 100}, Case{0x1422, 120}, Case{0x161c, 120},
        Case{0x161d, 100}, Case{0x1621, 100}, Case{0xffff, 0},   Case{0x8000, 0}};
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        Initialize(item, metadata, 2 << 3, 1);
        EXPECT_EQ(item.word28, entry.expected);
    }
}

TEST(ItemStateTest, SuppressesQForExceptionalIdsButRestoresItWithLowTwoBitsOfB4)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{100};
    metadata[0x26] = std::byte{20};
    for (const std::uint16_t id :
         std::array<std::uint16_t, 9>{0x1803, 0x1804, 0x1805, 0x1806, 0x13, 0x812, 0xa0a, 0x40d, 0x1a1e})
    {
        SCOPED_TRACE(id);
        ItemState item;
        item.id = id;
        Initialize(item, metadata, 0, 1);
        EXPECT_EQ(item.word0c, 20);
        Initialize(item, metadata, 0xdead0000, 0xabcd0001, 0xbeef0005);
        EXPECT_EQ(item.word0c, 38);
        EXPECT_EQ(item.scalar1Low, 0u);
    }
}

TEST(ItemStateTest, RoutesTheOptionMagnitudeAcrossEveryIdFamily)
{
    struct Case
    {
        std::uint16_t id;
        std::uint8_t tag;
        std::uint8_t value;
        std::uint16_t requirement;
    };
    constexpr std::array Cases{Case{1, 0x50, 28, 35},     Case{0xa00, 0x51, 28, 35}, Case{0xc00, 0x52, 35, 35},
                               Case{0xe00, 0x53, 28, 35}, Case{0x1a08, 0x55, 7, 0},  Case{0x1a18, 0xb0, 7, 0},
                               Case{0x1a1c, 0xb1, 7, 0},  Case{0x1800, 0x55, 7, 0},  Case{0x1801, 0x51, 28, 0},
                               Case{0x1802, 0x50, 28, 0}, Case{0x1803, 0x50, 28, 0}, Case{0x1804, 0x55, 7, 0},
                               Case{0x1805, 0x55, 7, 0},  Case{0x1806, 0x51, 28, 0}};
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        Initialize(item, Metadata{}, 3, 0x40);
        ASSERT_EQ(item.optionCount, 1);
        EXPECT_EQ(item.OptionTag(0), entry.tag);
        EXPECT_EQ(item.OptionValue(0), entry.value);
        EXPECT_EQ(item.word1e, entry.requirement);
    }
}

TEST(ItemStateTest, AddsTheThree1a03OptionsAndTheLate1a14Options)
{
    ItemState item;
    item.id = 0x1a03;
    Initialize(item, Metadata{}, 3, 0x40);
    ASSERT_EQ(item.optionCount, 3);
    EXPECT_EQ(item.OptionTag(0), 0x68);
    EXPECT_EQ(item.OptionTag(1), 0x67);
    EXPECT_EQ(item.OptionTag(2), 0x61);
    EXPECT_EQ(item.OptionValue(0), 5);
    EXPECT_EQ(item.OptionValue(1), 50);
    EXPECT_EQ(item.OptionValue(2), 5);
    EXPECT_EQ(item.word1e, 0);
    item.id = 0x1a14;
    Initialize(item, Metadata{}, 3 << 3);
    ASSERT_EQ(item.optionCount, 3);
    EXPECT_EQ(item.OptionTag(0), 0x60);
    EXPECT_EQ(item.OptionTag(1), 0x5e);
    EXPECT_EQ(item.OptionTag(2), 0x61);
    EXPECT_EQ(item.OptionValue(2), 10);
    Initialize(item, Metadata{}, 4 << 3);
    EXPECT_EQ(item.optionCount, 0);
}

TEST(ItemStateTest, KeepsValuesForAllSixArmorTags)
{
    ItemState item;
    item.id = 0xc00;
    item.optionBytes.fill(0x79);
    Initialize(item, Metadata{}, 0, 0x3f);
    ASSERT_EQ(item.optionCount, 6);
    constexpr std::array<std::uint8_t, 6> Tags{0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b};
    for (std::uint8_t index = 0; index < Tags.size(); ++index)
    {
        EXPECT_EQ(item.OptionTag(index), Tags[index]);
        EXPECT_EQ(item.OptionValue(index), 0x79);
    }
}

TEST(ItemStateTest, HandlesZeroDenominatorAndForcesPFromEitherSpecialB4Value)
{
    Metadata metadata{};
    metadata[0x26] = std::byte{255};
    metadata[0x29] = std::byte{255};
    metadata[0x2f] = std::byte{250};
    for (const std::uint32_t b4 : {1u, 2u})
    {
        ItemState item;
        Initialize(item, metadata, 10 << 3, 0, b4);
        EXPECT_EQ(item.word0c, 291);
        EXPECT_EQ(item.word12, 314);
        EXPECT_EQ(item.byte16, 33);
    }
}

TEST(ItemStateTest, Excludes807And80fFromTag54AndMagnitude)
{
    Metadata metadata{};
    for (const std::uint16_t id : std::array<std::uint16_t, 2>{0x807, 0x80f})
    {
        ItemState item;
        item.id = id;
        item.optionCount = 2;
        item.word1e = 99;
        Initialize(item, metadata, 7, 0x40);
        EXPECT_EQ(item.optionCount, 0);
        EXPECT_EQ(item.word1e, 0);
    }
}

TEST(ItemStateTest, SelectsBothLateTagFamiliesAndKeepsTheLiteralSeven)
{
    Metadata metadata{};
    struct Case
    {
        std::uint16_t id;
        std::uint8_t tag4;
        std::uint8_t tag3;
    };
    constexpr std::array Cases{Case{1, 0x5d, 0x5e},      Case{0x807, 0x5d, 0x5e},  Case{0x80f, 0x5d, 0x5e},
                               Case{0xa00, 0x5f, 0x60},  Case{0x1a0c, 0x5f, 0x60}, Case{0x1a0d, 0x5d, 0x5e},
                               Case{0x1a19, 0x5f, 0x60}, Case{0x1a1a, 0x5d, 0x5e}, Case{0x1a1b, 0x5f, 0x60},
                               Case{0x1a1c, 0x5d, 0x5e}};
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        item.optionBytes.fill(0x7a);
        Initialize(item, metadata, 0, 0x3f);
        ASSERT_EQ(item.optionCount, 6);
        EXPECT_EQ(item.OptionTag(0), 0x5c);
        EXPECT_EQ(item.OptionTag(1), entry.tag4);
        EXPECT_EQ(item.OptionTag(2), entry.tag3);
        EXPECT_EQ(item.OptionTag(3), 0x61);
        EXPECT_EQ(item.OptionTag(4), 0x62);
        EXPECT_EQ(item.OptionTag(5), 0x63);
        EXPECT_EQ(item.OptionValue(0), 0x7a);
        EXPECT_EQ(item.OptionValue(1), 50);
        EXPECT_EQ(item.OptionValue(2), 2);
        EXPECT_EQ(item.OptionValue(3), 7);
        EXPECT_EQ(item.OptionValue(4), 0x7a);
        EXPECT_EQ(item.OptionValue(5), 0x7a);
    }
}

// El doble representa la frontera requerida; no acredita sus auxiliares transitivos.
TEST(ItemStateTest, Calls4225d0OnTheSameDestinationBeforeClassification)
{
    Metadata metadata{};
    metadata[0x23] = std::byte{0x70};
    ItemState item;
    item.id = 0x1a04;
    item.itemClass = 0xa5;
    item.optionBytes.fill(0x79);
    const std::uint16_t contextWord = 0;
    unsigned calls = 0;
    InitializeItemState(item, metadata, {0x87, 0x3f, 0}, contextWord,
                        [&](ItemState& destination)
                        {
                            ++calls;
                            EXPECT_EQ(&destination, &item);
                            EXPECT_EQ(destination.scalar1Low, 0x87u);
                            EXPECT_EQ(destination.itemClass, 0xa5);
                            EXPECT_EQ(destination.optionCount, 1);
                            EXPECT_EQ(destination.OptionTag(0), 0x70);
                            EXPECT_EQ(destination.word1e, 0);
                            destination.optionBytes[9] = 42;
                            destination.optionBytes[1] = 0x96;
                            destination.optionCount = 2;
                        });
    EXPECT_EQ(calls, 1u);
    ASSERT_EQ(item.optionCount, 2);
    EXPECT_EQ(item.OptionTag(1), 0x96);
    EXPECT_EQ(item.OptionValue(1), 42);
    EXPECT_EQ(item.itemClass, 8);
    item.id = 1;
    InitializeItemState(item, metadata, {}, contextWord, [&](ItemState&) { ++calls; });
    EXPECT_EQ(calls, 1u);
}

TEST(ItemStateTest, AnAbsentRequired4225d0CannotSilentlySucceed)
{
    ItemState item;
    item.id = 0x1a04;
    const Metadata metadata{};
    const std::uint16_t contextWord = 0;
    EXPECT_THROW(InitializeItemState(item, metadata, {}, contextWord, {}), std::bad_function_call);
}

TEST(ItemStateTest, PreservesNinthTagAliasingInThe1a1eFamily)
{
    Metadata metadata{};
    metadata[0x23] = std::byte{0x70};
    ItemState item;
    item.id = 0x1a1e;
    item.optionBytes.fill(0x79);
    Initialize(item, metadata, 0x87, 0x7f);
    ASSERT_EQ(item.optionCount, 9);
    EXPECT_EQ(item.OptionTag(0), 0x96);
    EXPECT_EQ(item.OptionTag(6), 0x70);
    EXPECT_EQ(item.OptionTag(7), 0x54);
    EXPECT_EQ(item.OptionTag(8), 0x50);
    EXPECT_EQ(item.OptionValue(0), 0x50);
    EXPECT_EQ(item.OptionValue(8), 28);
}

TEST(ItemStateTest, SwitchesTheMagnitudeFamilyOnBit5)
{
    struct Case
    {
        std::uint16_t id;
        std::uint8_t tag;
        std::uint8_t value;
    };
    constexpr std::array Cases{Case{0x1803, 0x55, 7}, Case{0x1804, 0x51, 28}, Case{0x1805, 0x50, 28},
                               Case{0x1806, 0x50, 28}};
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        Initialize(item, Metadata{}, 3, 0x60);
        ASSERT_EQ(item.optionCount, 1);
        EXPECT_EQ(item.OptionTag(0), entry.tag);
        EXPECT_EQ(item.OptionValue(0), entry.value);
    }
}

TEST(ItemStateTest, Applies1a0aLevelOverrideBeforeTheQBonus)
{
    struct Case
    {
        std::uint32_t level;
        std::uint32_t q;
        std::uint16_t expected;
    };
    constexpr std::array Cases{Case{2, 0, 20}, Case{3, 0, 50}, Case{2, 1, 40}, Case{3, 1, 70}};
    for (const auto& entry : Cases)
    {
        ItemState item;
        item.id = 0x1a0a;
        Initialize(item, Metadata{}, entry.level << 3, entry.q);
        EXPECT_EQ(item.word28, entry.expected);
    }
}

TEST(ItemStateTest, TruncatesTheContextQuotientToAByte)
{
    ItemState item;
    item.id = 1;
    const std::uint16_t contextWord = 65535;
    InitializeItemState(item, Metadata{}, {0, 0x10, 0}, contextWord, {});
    ASSERT_EQ(item.optionCount, 1);
    EXPECT_EQ(item.OptionTag(0), 0x5d);
    EXPECT_EQ(item.OptionValue(0), 204);
}

// cd8a..cdac/cde5..ce08 leen el word de la fuente al llegar a la opción, tras los stores D+e.
// Ambos almacenamientos son válidos: un word separado o la fuente coincidente con D+e.
TEST(ItemStateTest, ReadsTheContextWordLateIncludingWhenItAliasesTheDestinationWord)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{100};
    metadata[0x26] = std::byte{20};
    metadata[0x27] = std::byte{80};
    ItemState separate;
    separate.id = 1;
    const std::uint16_t contextWord = 1000;
    InitializeItemState(separate, metadata, {0, 0x10, 0}, contextWord, {});
    EXPECT_EQ(contextWord, 1000);
    EXPECT_EQ(separate.word0e, 90);
    ASSERT_EQ(separate.optionCount, 1);
    EXPECT_EQ(separate.OptionTag(0), 0x5d);
    EXPECT_EQ(separate.OptionValue(0), 50);
    ItemState aliased;
    aliased.id = 1;
    aliased.word0e = 1000;
    InitializeItemState(aliased, metadata, {0, 0x10, 0}, aliased.word0e, {});
    EXPECT_EQ(aliased.word0e, 90);
    ASSERT_EQ(aliased.optionCount, 1);
    EXPECT_EQ(aliased.OptionTag(0), 0x5d);
    EXPECT_EQ(aliased.OptionValue(0), 4);
}

// Si P fuerza Q antes de requisitos, E suma25 incluso con B3 inicialmente cero.
TEST(ItemStateTest, PForcesQBeforeRequirementBaseSelection)
{
    Metadata metadata{};
    metadata[0x20] = std::byte{100};
    metadata[0x30] = std::byte{100};
    ItemState item;
    Initialize(item, metadata, 0, 0, 1);
    EXPECT_EQ(item.word1e, 395);
}

TEST(ItemStateDeathTest, InvalidTagIndicesStopWithADiagnosticInEveryBuild)
{
    const ItemState item;
    for (const std::uint8_t index : std::array<std::uint8_t, 2>{9, 255})
    {
        EXPECT_DEATH(static_cast<void>(item.OptionTag(index)), "ItemState.*fuera de rango");
    }
}

TEST(ItemStateDeathTest, InvalidValueIndicesStopWithADiagnosticInEveryBuild)
{
    const ItemState item;
    for (const std::uint8_t index : std::array<std::uint8_t, 2>{9, 255})
    {
        EXPECT_DEATH(static_cast<void>(item.OptionValue(index)), "ItemState.*fuera de rango");
    }
}

using GoldenItemState = Client::Tests::GoldenDataTest;

// Metadata real cargada una vez: expected de registros observados, no de InitializeItemState.
TEST_F(GoldenItemState, UsesTheItemTableOwnedByGameData)
{
    const auto data = Client::Scenes::LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().items.has_value());
    struct Case
    {
        std::uint16_t id;
        std::uint16_t field0c;
        std::uint16_t field0e;
        std::uint16_t requirement1e;
        std::uint16_t requirement20;
    };
    constexpr std::array Cases{Case{0, 6, 11, 27, 27}, Case{1, 3, 7, 25, 0}, Case{0x406, 102, 130, 335, 132},
                               Case{0xc00, 0, 0, 26, 0}};
    const std::uint16_t contextWord = 0;
    for (const auto& entry : Cases)
    {
        SCOPED_TRACE(entry.id);
        ItemState item;
        item.id = entry.id;
        const auto metadata = data.Value().items->table.Record(item.id);
        ASSERT_TRUE(metadata.has_value());
        InitializeItemState(item, *metadata, {}, contextWord, {});
        EXPECT_EQ(item.word0c, entry.field0c);
        EXPECT_EQ(item.word0e, entry.field0e);
        EXPECT_EQ(item.word1e, entry.requirement1e);
        EXPECT_EQ(item.word20, entry.requirement20);
    }
}
} // namespace
