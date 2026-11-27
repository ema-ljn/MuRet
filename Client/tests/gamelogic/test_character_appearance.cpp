#include "Kernel/CharacterAppearance.h"
#include "Protocol/FrontEnd/CharacterList.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Client::Kernel::CharacterAppearance: from the class and the list's 17 equipment bytes to each part's models, as
// 0x517270, 0x5139f0 and 0x512930 do (06 § Los personajes de la lista).
namespace
{
using Client::Kernel::AppearanceOf;
using Client::Kernel::BodyPart;
using Client::Kernel::CharacterAppearance;
using Client::Kernel::ClassAppearance;
using Client::Kernel::ClassPartModel;
using Client::Kernel::ItemLevelOf;
using Client::Protocol::FrontEnd::EquipmentSize;

using EquipmentBytes = std::array<std::byte, EquipmentSize>;

// The class's clothes models: HelmClass to BootClass.
constexpr std::array<std::uint16_t, 5> ClassBase = {0x2212, 0x221a, 0x2222, 0x222a, 0x2232};

// An empty equipment: the hands at 0xfff, the five parts at 0x1ff, no wings (3 with b[8] & 7 at 0) and no pet (3
// without the bits of b[9] and b[11]).
EquipmentBytes Naked()
{
    EquipmentBytes bytes{};
    const std::array<int, EquipmentSize> values = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xF8,
                                                   0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
    for (std::size_t k = 0; k < EquipmentSize; ++k)
    {
        bytes[k] = static_cast<std::byte>(values[k]);
    }
    return bytes;
}

std::uint16_t PartModel(const CharacterAppearance& appearance, BodyPart part)
{
    return appearance.bodyParts[static_cast<std::size_t>(part)].model;
}

// An optional model built from a std::uint16_t: from an int, the conversion lands inside std::optional's constructor
// and MSVC warns about it (C4242), even though the value fits.
std::optional<std::uint16_t> Model(std::uint16_t model)
{
    return model;
}

// 0x517270 outside scene 2: the class's helmet to boots, at HelmClass to BootClass plus (class >> 3) · 5 +
// (class & 7), and the hands, the wings and the pet empty.
TEST(CharacterAppearanceTest, WithoutEquipmentWearsTheClassParts)
{
    for (const int classValue : {0, 1, 2, 3, 4, 8, 9, 10, 11, 12})
    {
        const auto characterClass = static_cast<std::uint8_t>(classValue);
        const CharacterAppearance appearance = ClassAppearance(characterClass, 0);

        for (std::size_t part = 0; part < ClassBase.size(); ++part)
        {
            EXPECT_EQ(appearance.bodyParts[part].model,
                      ClassBase[part] + (characterClass >> 3) * 5 + (characterClass & 7))
                << "clase " << int{characterClass} << ", parte " << part;
            EXPECT_EQ(appearance.bodyParts[part].level, 0);
        }
        EXPECT_FALSE(appearance.rightHand.has_value());
        EXPECT_FALSE(appearance.leftHand.has_value());
        EXPECT_FALSE(appearance.wings.has_value());
        EXPECT_FALSE(appearance.pet.has_value());
    }
}

// The second class (bit 3) wears its class's parts shifted by 5.
TEST(CharacterAppearanceTest, TheSecondClassWearsItsOwnParts)
{
    EXPECT_EQ(ClassPartModel(BodyPart::Helm, 0x08), 0x2212 + 5);
    EXPECT_EQ(ClassPartModel(BodyPart::Boots, 0x0A), 0x2232 + 5 + 2);
}

// An index 0x1ff is "empty": the class's part stays. With empty equipment, like the class alone.
TEST(CharacterAppearanceTest, AnEmptySlotIsTheClassPart)
{
    const CharacterAppearance appearance = AppearanceOf(2, 0, Naked());

    for (std::size_t part = 0; part < ClassBase.size(); ++part)
    {
        EXPECT_EQ(appearance.bodyParts[part].model, ClassBase[part] + 2) << part;
    }
    EXPECT_FALSE(appearance.wings.has_value());
    EXPECT_FALSE(appearance.pet.has_value());
}

// 0x513a35/0x513a79 compare AX with 0x1fff after building only 12 bits. The low DWORD plus 0x212 stays in the model's
// WORD: 0xfff gives 0x1211, not 0xffff.
TEST(CharacterAppearanceTest, AnEncodedEmptyHandKeepsTheNativeUnloadedModel)
{
    const CharacterAppearance appearance = AppearanceOf(0, 0, Naked());
    ASSERT_TRUE(appearance.rightHand.has_value());
    ASSERT_TRUE(appearance.leftHand.has_value());
    EXPECT_EQ(appearance.rightHand->model, 0x1211);
    EXPECT_EQ(appearance.leftHand->model, 0x1211);
}

TEST(CharacterAppearanceTest, EncodedEmptyHandsKeepTheirLevelAndOptionBits)
{
    auto bytes = Naked();
    bytes[7] = std::byte{0x3f};
    bytes[9] = std::byte{6};
    bytes[10] = std::byte{6};
    const CharacterAppearance appearance = AppearanceOf(0, 0, bytes);
    ASSERT_TRUE(appearance.rightHand.has_value());
    ASSERT_TRUE(appearance.leftHand.has_value());
    EXPECT_EQ(appearance.rightHand->level, 13);
    EXPECT_EQ(appearance.leftHand->level, 13);
    EXPECT_TRUE(appearance.rightHand->excellent);
    EXPECT_TRUE(appearance.leftHand->excellent);
    EXPECT_TRUE(appearance.rightHand->ancient);
    EXPECT_TRUE(appearance.leftHand->ancient);
}

// Each equipment byte, on its own, goes to its part (0x5139f0's table in 06).
TEST(CharacterAppearanceTest, EachEquipmentByteGivesItsItem)
{
    EquipmentBytes bytes = Naked();
    // The right hand: type ((b[11] & 0xf0) << 4) | b[0], group 1 and index 0x23.
    bytes[0] = std::byte{0x23};
    bytes[11] = std::byte{0x20};
    // The left: ((b[12] & 0xf0) << 4) | b[1], group 2 and index 4.
    bytes[1] = std::byte{0x04};
    bytes[12] = std::byte{0x40};
    // The helmet: ((b[8] >> 7) + (b[12] & 0xf) · 2) · 16 + (b[2] >> 4), with b[12] & 0xf at 0 and bit 7 of b[8] at
    // 1: 16 + 3.
    // The armor: (((b[8] >> 6) & 1) + (b[13] >> 4) · 2) · 16 + (b[2] & 0xf): 2 · 16 + 5.
    bytes[2] = std::byte{0x35};
    bytes[13] = std::byte{0x16};
    // The pants: (((b[8] >> 5) & 1) + (b[13] & 0xf) · 2) · 16 + (b[3] >> 4): (1 + 12) · 16 + 7.
    // The gloves: (((b[8] >> 4) & 1) + (b[14] >> 4) · 2) · 16 + (b[3] & 0xf): 2 · 16 + 9.
    bytes[3] = std::byte{0x79};
    bytes[14] = std::byte{0x10};
    // The boots: (((b[8] >> 3) & 1) + (b[14] & 0xf) · 2) · 16 + (b[4] >> 4): 16 + 2; the wings, the 1 of b[4]'s bits
    // 2 and 3, and the pet, the 0 of bits 0 and 1.
    bytes[4] = std::byte{0x24};
    bytes[8] = std::byte{0xA8};
    // The levels, 3 bits each from b[7]: the right 1 (3), the left 2 (5), the helmet 3 (7), the armor 4 (9), the
    // pants 5 (11), the gloves 6 (12) and the boots 7 (13).
    const std::uint32_t levels = 1 | (2 << 3) | (3 << 6) | (4 << 9) | (5 << 12) | (6 << 15) | (7 << 18);
    bytes[5] = static_cast<std::byte>(levels >> 16);
    bytes[6] = static_cast<std::byte>((levels >> 8) & 0xFF);
    bytes[7] = static_cast<std::byte>(levels & 0xFF);
    // Excellent: the helmet (bit 7) and the left hand (bit 1).
    bytes[9] = std::byte{0x82};

    const CharacterAppearance appearance = AppearanceOf(0, 0, bytes);

    ASSERT_TRUE(appearance.rightHand.has_value());
    EXPECT_EQ(appearance.rightHand->model, 0x212 + 0x223);
    EXPECT_EQ(appearance.rightHand->level, 3);
    ASSERT_TRUE(appearance.leftHand.has_value());
    EXPECT_EQ(appearance.leftHand->model, 0x212 + 0x404);
    EXPECT_EQ(appearance.leftHand->level, 5);
    EXPECT_TRUE(appearance.leftHand->excellent);
    EXPECT_FALSE(appearance.rightHand->excellent);
    EXPECT_EQ(PartModel(appearance, BodyPart::Helm), 0x1012 + 16 + 3);
    EXPECT_EQ(PartModel(appearance, BodyPart::Armor), 0x1212 + 2 * 16 + 5);
    EXPECT_EQ(PartModel(appearance, BodyPart::Pants), 0x1412 + 13 * 16 + 7);
    EXPECT_EQ(PartModel(appearance, BodyPart::Gloves), 0x1612 + 2 * 16 + 9);
    EXPECT_EQ(PartModel(appearance, BodyPart::Boots), 0x1812 + 16 + 2);
    EXPECT_EQ(appearance.bodyParts[0].level, 7);
    EXPECT_EQ(appearance.bodyParts[1].level, 9);
    EXPECT_EQ(appearance.bodyParts[2].level, 11);
    EXPECT_EQ(appearance.bodyParts[3].level, 12);
    EXPECT_EQ(appearance.bodyParts[4].level, 13);
    EXPECT_TRUE(appearance.bodyParts[0].excellent);
    EXPECT_FALSE(appearance.bodyParts[1].excellent);
    EXPECT_EQ(appearance.wings, Model(0x1a12 + 1));
    EXPECT_EQ(appearance.pet, Model(0x1c12));
}

// 0x513340: each part's 3 bits, 0 to 7, give levels 0, 3, 5, 7, 9, 11, 12 and 13 (0x51338c's table).
TEST(CharacterAppearanceTest, TheLevelBitsGoThroughTheTable)
{
    constexpr std::array<std::uint8_t, 8> Levels = {0, 3, 5, 7, 9, 11, 12, 13};
    for (std::uint8_t bits = 0; bits < Levels.size(); ++bits)
    {
        EXPECT_EQ(ItemLevelOf(bits), Levels[bits]) << int{bits};
    }
}

// The wings: with b[4]'s bits 2 and 3 at 3, b[8] & 7 gives none (0), the cape 0x1c30 (5) or 0x1a14 plus the value.
TEST(CharacterAppearanceTest, TheWingsOfTheThirdValueComeFromByteEight)
{
    EquipmentBytes bytes = Naked();
    bytes[8] = std::byte{0xF8 | 5};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).wings, Model(0x1c30));
    bytes[8] = std::byte{0xF8 | 2};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).wings, Model(0x1a14 + 2));
}

// The pet: with b[4]'s bits 0 and 1 at 3, b[9]'s bit 0 gives 0x1c15; then b[11]'s bit 0 overrides it with 0x1c16,
// and bit 2 with 0x1c37, in that order.
TEST(CharacterAppearanceTest, ThePetBitsOverrideInOrder)
{
    EquipmentBytes bytes = Naked();
    bytes[9] = std::byte{0x01};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).pet, Model(0x1c15));
    bytes[11] = std::byte{0xF1};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).pet, Model(0x1c16));
    bytes[11] = std::byte{0xF5};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).pet, Model(0x1c37));
    bytes[4] = std::byte{0xFE};
    bytes[9] = std::byte{0x00};
    bytes[11] = std::byte{0xF0};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).pet, Model(0x1c12 + 2));
}

// Class 4 with type 0xa05 on the left holds 0x1c17 (0x420cf0).
TEST(CharacterAppearanceTest, ClassFourWithTheTypeA05HoldsTheRaven)
{
    EquipmentBytes bytes = Naked();
    bytes[1] = std::byte{0x05};
    bytes[12] = std::byte{0xAF};

    EXPECT_EQ(AppearanceOf(4, 0, bytes).leftHand->model, 0x1c17);
    EXPECT_EQ(AppearanceOf(3, 0, bytes).leftHand->model, 0x212 + 0xa05);
}

// 0x512930: 0.9 or 0.93 for classes 0 and 1, 0.88 or 0.86 for 2, 0.95 for 3 and 0.92 for 4, as the skin is 0 or not.
TEST(CharacterAppearanceTest, TheScaleGoesByClassAndSkin)
{
    EXPECT_FLOAT_EQ(ClassAppearance(0, 0).scale, 0.9f);
    EXPECT_FLOAT_EQ(ClassAppearance(1, 0).scale, 0.9f);
    EXPECT_FLOAT_EQ(ClassAppearance(0, 1).scale, 0.93f);
    EXPECT_FLOAT_EQ(ClassAppearance(2, 0).scale, 0.88f);
    EXPECT_FLOAT_EQ(ClassAppearance(2, 1).scale, 0.86f);
    EXPECT_FLOAT_EQ(ClassAppearance(3, 0).scale, 0.95f);
    EXPECT_FLOAT_EQ(ClassAppearance(4, 0).scale, 0.92f);
    EXPECT_FLOAT_EQ(ClassAppearance(8, 0).scale, 0.9f);
}

// 0x512930: the class's head shows only with helmets 0x1012, 0x1014 and 0x101c to 0x101f.
TEST(CharacterAppearanceTest, TheHeadShowsUnderTheOpenHelmets)
{
    EquipmentBytes bytes = Naked();
    bytes[8] = std::byte{0x78};
    bytes[12] = std::byte{0xF0};
    for (const int index : {0x0, 0x2, 0xA, 0xB, 0xC, 0xD})
    {
        bytes[2] = static_cast<std::byte>(0x0F | (index << 4));
        EXPECT_EQ(AppearanceOf(1, 0, bytes).head, Model(0x2212 + 1)) << index;
    }
    bytes[2] = std::byte{0x1F};
    EXPECT_FALSE(AppearanceOf(1, 0, bytes).head.has_value());
    EXPECT_FALSE(ClassAppearance(1, 0).head.has_value());
}
} // namespace
