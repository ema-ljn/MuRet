#include "Kernel/CharacterAppearance.h"

#include "Kernel/CharacterClass.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Kernel
{
namespace
{
// HelmClass to BootClass: the class's parts (0x2212, 0x221a, 0x2222, 0x222a and 0x2232).
constexpr std::array<std::uint16_t, BodyPartCount> ClassPartBase = {0x2212, 0x221a, 0x2222, 0x222a, 0x2232};

// Each equipment part's first model: 0x212 plus groups 7 to 11 (HelmMale to BootMale).
constexpr std::array<std::uint16_t, BodyPartCount> EquipmentPartBase = {0x1012, 0x1212, 0x1412, 0x1612, 0x1812};

// The index that means "empty" in a body part.
constexpr std::uint16_t EmptyPartIndex = 0x1ff;
// 0x513a35/0x513a79: CMP AX,0x1fff. The twelve-bit type never reaches this value (Q-C102).
constexpr std::uint16_t EmptyHandType = 0x1fff;
// An item's first model: the type is added.
constexpr std::uint16_t FirstItemModel = 0x212;

// Class 4 with type 0xa05 on the left (0x420cf0).
constexpr Kernel::CharacterClass RavenClass = Kernel::CharacterClass::DarkLord;
constexpr std::uint16_t RavenType = 0xa05;
constexpr std::uint16_t RavenModel = 0x1c17;

// The wings: 0x1a12 plus b[4]'s bits 2 and 3; with 3, b[8] & 7: none, the cape or 0x1a14 plus the value.
constexpr std::uint16_t FirstWings = 0x1a12;
constexpr std::uint16_t SecondWings = 0x1a14;
constexpr std::uint16_t Cape = 0x1c30;
constexpr std::uint8_t CapeValue = 5;

// The pet: 0x1c12 plus b[4]'s bits 0 and 1; with 3, b[9]'s bit 0; then b[11]'s bits 0 and 2, which override it.
constexpr std::uint16_t FirstPet = 0x1c12;
constexpr std::uint16_t ThirdValuePet = 0x1c15;
constexpr std::uint16_t Byte11Bit0Pet = 0x1c16;
constexpr std::uint16_t Byte11Bit2Pet = 0x1c37;

// The helmets that show the head (0x512930).
constexpr std::array<std::uint16_t, 6> OpenHelmets = {0x1012, 0x1014, 0x101c, 0x101d, 0x101e, 0x101f};

// 0x51338c's table.
constexpr std::array<std::uint8_t, 8> ItemLevels = {0, 3, 5, 7, 9, 11, 12, 13};

// Where each part's 9 index bits come from: b[8]'s high bit, the nibble of b[12] to b[14] two per byte (high or low)
// and the low nibble of b[2] to b[4].
struct PartBits
{
    int byte8Bit = 0;
    std::size_t extendedByte = 0;
    bool extendedHigh = false;
    std::size_t lowByte = 0;
    bool lowHigh = false;
};
constexpr std::array<PartBits, BodyPartCount> PartLayout = {{
    {7, 12, false, 2, true},
    {6, 13, true, 2, false},
    {5, 13, false, 3, true},
    {4, 14, true, 3, false},
    {3, 14, false, 4, true},
}};

// Where each part's 3 level bits go in L = b[5] << 16 | b[6] << 8 | b[7], and the hands'; and each one's excellent
// (b[9]) and set (b[10]) bit.
constexpr std::array<int, BodyPartCount> PartLevelShift = {6, 9, 12, 15, 18};
constexpr int RightLevelShift = 0;
constexpr int LeftLevelShift = 3;
constexpr std::array<int, BodyPartCount> PartFlagBit = {7, 6, 5, 4, 3};
constexpr int RightFlagBit = 2;
constexpr int LeftFlagBit = 1;

std::uint8_t ByteOf(Equipment equipment, std::size_t index)
{
    return std::to_integer<std::uint8_t>(equipment[index]);
}

std::uint8_t Nibble(std::uint8_t value, bool high)
{
    return static_cast<std::uint8_t>(high ? value >> 4 : value & 0x0F);
}

bool Bit(std::uint8_t value, int bit)
{
    return ((value >> bit) & 1) != 0;
}

std::uint16_t PartIndex(Equipment equipment, const PartBits& bits)
{
    const int high = (ByteOf(equipment, 8) >> bits.byte8Bit) & 1;
    const int extended = Nibble(ByteOf(equipment, bits.extendedByte), bits.extendedHigh);
    const int low = Nibble(ByteOf(equipment, bits.lowByte), bits.lowHigh);
    return static_cast<std::uint16_t>((high + extended * 2) * 16 + low);
}

// The hand: the 12-bit type, ((b[high] & 0xf0) << 4) | b[low].
std::uint16_t HandType(Equipment equipment, std::size_t lowByte, std::size_t highByte)
{
    return static_cast<std::uint16_t>(((ByteOf(equipment, highByte) & 0xF0) << 4) | ByteOf(equipment, lowByte));
}

std::optional<std::uint16_t> WingsOf(Equipment equipment)
{
    const auto value = static_cast<std::uint8_t>((ByteOf(equipment, 4) >> 2) & 3);
    if (value != 3)
    {
        return static_cast<std::uint16_t>(FirstWings + value);
    }
    const auto extended = static_cast<std::uint8_t>(ByteOf(equipment, 8) & 7);
    if (extended == 0)
    {
        return std::nullopt;
    }
    return extended == CapeValue ? Cape : static_cast<std::uint16_t>(SecondWings + extended);
}

std::optional<std::uint16_t> PetOf(Equipment equipment)
{
    const auto value = static_cast<std::uint8_t>(ByteOf(equipment, 4) & 3);
    std::optional<std::uint16_t> pet;
    if (value != 3)
    {
        pet = static_cast<std::uint16_t>(FirstPet + value);
    }
    else if (Bit(ByteOf(equipment, 9), 0))
    {
        pet = ThirdValuePet;
    }
    if (Bit(ByteOf(equipment, 11), 0))
    {
        pet = Byte11Bit0Pet;
    }
    if (Bit(ByteOf(equipment, 11), 2))
    {
        pet = Byte11Bit2Pet;
    }
    return pet;
}

// 0x512930: the head with the open helmets, and the scale.
void FinishAppearance(CharacterAppearance& appearance)
{
    const std::uint16_t helm = appearance.bodyParts[static_cast<std::size_t>(BodyPart::Helm)].model;
    appearance.head = HeadUnder(helm, appearance.characterClass);
    if (const std::optional<float> scale = ClassScale(appearance.characterClass, appearance.skin))
    {
        appearance.scale = *scale;
    }
}
} // namespace

std::optional<std::uint16_t> HeadUnder(std::uint16_t helm, std::uint8_t characterClass)
{
    if (std::ranges::find(OpenHelmets, helm) == OpenHelmets.end())
    {
        return std::nullopt;
    }
    return ClassPartModel(BodyPart::Helm, characterClass);
}

std::optional<float> ClassScale(std::uint8_t characterClass, std::uint8_t skin)
{
    const bool withSkin = skin != 0;
    switch (characterClass & 7)
    {
    case 0:
    case 1:
        return withSkin ? 0.93f : 0.9f;
    case 2:
        return withSkin ? 0.86f : 0.88f;
    case 3:
        return 0.95f;
    case 4:
        return 0.92f;
    default:
        return std::nullopt;
    }
}

std::uint16_t ClassPartModel(BodyPart part, std::uint8_t characterClass)
{
    return static_cast<std::uint16_t>(ClassPartBase[static_cast<std::size_t>(part)] + (characterClass >> 3) * 5 +
                                      (characterClass & 7));
}

std::uint8_t ItemLevelOf(std::uint8_t bits)
{
    return ItemLevels[bits & 7];
}

CharacterAppearance ClassAppearance(std::uint8_t characterClass, std::uint8_t skin)
{
    CharacterAppearance appearance;
    appearance.characterClass = characterClass;
    appearance.skin = skin;
    for (std::size_t part = 0; part < BodyPartCount; ++part)
    {
        appearance.bodyParts[part].model = ClassPartModel(static_cast<BodyPart>(part), characterClass);
    }
    FinishAppearance(appearance);
    return appearance;
}

CharacterAppearance AppearanceOf(std::uint8_t characterClass, std::uint8_t skin, Equipment equipment)
{
    CharacterAppearance appearance = ClassAppearance(characterClass, skin);
    const std::uint32_t levels = (static_cast<std::uint32_t>(ByteOf(equipment, 5)) << 16) |
                                 (static_cast<std::uint32_t>(ByteOf(equipment, 6)) << 8) | ByteOf(equipment, 7);
    const auto levelAt = [levels](int shift) { return ItemLevelOf(static_cast<std::uint8_t>((levels >> shift) & 7)); };
    const std::uint8_t excellent = ByteOf(equipment, 9);
    const std::uint8_t ancient = ByteOf(equipment, 10);

    const std::uint16_t right = HandType(equipment, 0, 11);
    if (right != EmptyHandType)
    {
        appearance.rightHand = WornItem{static_cast<std::uint16_t>(FirstItemModel + right), levelAt(RightLevelShift),
                                        Bit(excellent, RightFlagBit), Bit(ancient, RightFlagBit)};
    }
    const std::uint16_t left = HandType(equipment, 1, 12);
    if (left != EmptyHandType)
    {
        const bool raven = Kernel::BaseClassOf(characterClass) == RavenClass && left == RavenType;
        appearance.leftHand = WornItem{raven ? RavenModel : static_cast<std::uint16_t>(FirstItemModel + left),
                                       levelAt(LeftLevelShift), Bit(excellent, LeftFlagBit), Bit(ancient, LeftFlagBit)};
    }
    appearance.wings = WingsOf(equipment);
    appearance.pet = PetOf(equipment);
    for (std::size_t part = 0; part < BodyPartCount; ++part)
    {
        const std::uint16_t index = PartIndex(equipment, PartLayout[part]);
        WornItem& worn = appearance.bodyParts[part];
        if (index != EmptyPartIndex)
        {
            worn.model = static_cast<std::uint16_t>(EquipmentPartBase[part] + index);
        }
        worn.level = levelAt(PartLevelShift[part]);
        worn.excellent = Bit(excellent, PartFlagBit[part]);
        worn.ancient = Bit(ancient, PartFlagBit[part]);
    }
    FinishAppearance(appearance);
    return appearance;
}
} // namespace Client::Kernel
