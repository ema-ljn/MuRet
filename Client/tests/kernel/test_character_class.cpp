#include "Kernel/CharacterClass.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
using Client::Kernel::BaseClassOf;
using Client::Kernel::CharacterClass;
using Client::Kernel::ClassNumberOf;

TEST(CharacterClassTest, TheBaseClassIsTheLowThreeBitsOfEveryByte)
{
    for (unsigned packed = 0; packed <= 0xff; ++packed)
    {
        const auto byte = static_cast<std::uint8_t>(packed);
        EXPECT_EQ(ClassNumberOf(BaseClassOf(byte)), static_cast<std::uint8_t>(packed & 7u)) << packed;
    }
}

TEST(CharacterClassTest, ASecondClassInTheHighBitsKeepsItsBaseClass)
{
    constexpr std::uint8_t SecondClassBit = 0x08;
    EXPECT_EQ(BaseClassOf(static_cast<std::uint8_t>(2 | SecondClassBit)), CharacterClass::Elf);
    EXPECT_EQ(BaseClassOf(static_cast<std::uint8_t>(0 | SecondClassBit)), CharacterClass::DarkWizard);
    EXPECT_EQ(BaseClassOf(static_cast<std::uint8_t>(1 | SecondClassBit)), CharacterClass::DarkKnight);
}

TEST(CharacterClassTest, TheClassesKeepTheOriginalsNumbers)
{
    EXPECT_EQ(ClassNumberOf(CharacterClass::DarkWizard), 0);
    EXPECT_EQ(ClassNumberOf(CharacterClass::DarkKnight), 1);
    EXPECT_EQ(ClassNumberOf(CharacterClass::Elf), 2);
    EXPECT_EQ(ClassNumberOf(CharacterClass::MagicGladiator), 3);
    EXPECT_EQ(ClassNumberOf(CharacterClass::DarkLord), 4);
}
} // namespace
