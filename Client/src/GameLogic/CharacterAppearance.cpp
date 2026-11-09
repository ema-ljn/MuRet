#include "GameLogic/CharacterAppearance.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::GameLogic
{
namespace
{
// HelmClass a BootClass: las partes de la clase (0x2212, 0x221a, 0x2222, 0x222a y 0x2232).
constexpr std::array<std::uint16_t, BodyPartCount> ClassPartBase = {0x2212, 0x221a, 0x2222, 0x222a, 0x2232};

// El primer modelo de cada parte del equipo: 0x212 más los grupos 7 a 11 (HelmMale a BootMale).
constexpr std::array<std::uint16_t, BodyPartCount> EquipmentPartBase = {0x1012, 0x1212, 0x1412, 0x1612, 0x1812};

// El índice que es "vacío" en una parte del cuerpo.
constexpr std::uint16_t EmptyPartIndex = 0x1ff;
// El tipo que MuRet toma como "sin ítem" en una mano (Q-C102: el original compara con 0x1fff).
constexpr std::uint16_t EmptyHandType = 0xfff;
// El primer modelo de un ítem: el tipo va sumado.
constexpr std::uint16_t FirstItemModel = 0x212;

// La clase 4 con el tipo 0xa05 en la izquierda (0x420cf0).
constexpr std::uint8_t RavenClass = 4;
constexpr std::uint16_t RavenType = 0xa05;
constexpr std::uint16_t RavenModel = 0x1c17;

// Las alas: 0x1a12 más los bits 2 y 3 de b[4]; con 3, b[8] & 7: ninguna, la capa o 0x1a14 más el
// valor.
constexpr std::uint16_t FirstWings = 0x1a12;
constexpr std::uint16_t SecondWings = 0x1a14;
constexpr std::uint16_t Cape = 0x1c30;
constexpr std::uint8_t CapeValue = 5;

// La mascota: 0x1c12 más los bits 0 y 1 de b[4]; con 3, el bit 0 de b[9]; después, los bits 0 y 2
// de b[11], que la pisan.
constexpr std::uint16_t FirstPet = 0x1c12;
constexpr std::uint16_t ThirdValuePet = 0x1c15;
constexpr std::uint16_t Byte11Bit0Pet = 0x1c16;
constexpr std::uint16_t Byte11Bit2Pet = 0x1c37;

// Los cascos que dejan ver la cabeza (0x512930).
constexpr std::array<std::uint16_t, 6> OpenHelmets = {0x1012, 0x1014, 0x101c, 0x101d, 0x101e, 0x101f};

// La tabla de 0x51338c.
constexpr std::array<std::uint8_t, 8> ItemLevels = {0, 3, 5, 7, 9, 11, 12, 13};

// De dónde salen los 9 bits del índice de cada parte: el bit alto de b[8], el nibble de b[12] a
// b[14] que va por 2 (el de arriba o el de abajo) y el nibble de b[2] a b[4] de abajo de todo.
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

// Dónde van los 3 bits del nivel de cada parte en L = b[5] << 16 | b[6] << 8 | b[7], y los de las
// manos; y el bit de excelente (b[9]) y de set (b[10]) de cada una.
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

// La mano: el tipo de 12 bits, ((b[alto] & 0xf0) << 4) | b[bajo].
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

// 0x512930: la cabeza con los cascos abiertos y la escala.
void FinishAppearance(CharacterAppearance& appearance)
{
    const std::uint16_t helm = appearance.bodyParts[static_cast<std::size_t>(BodyPart::Helm)].model;
    appearance.head.reset();
    if (std::ranges::find(OpenHelmets, helm) != OpenHelmets.end())
    {
        appearance.head = ClassPartModel(BodyPart::Helm, appearance.characterClass);
    }
    const bool withSkin = appearance.skin != 0;
    switch (appearance.characterClass & 7)
    {
    case 0:
    case 1:
        appearance.scale = withSkin ? 0.93f : 0.9f;
        return;
    case 2:
        appearance.scale = withSkin ? 0.86f : 0.88f;
        return;
    case 3:
        appearance.scale = 0.95f;
        return;
    case 4:
        appearance.scale = 0.92f;
        return;
    default:
        // Las clases 5 a 7 no las arma el original: queda la escala de antes.
        return;
    }
}
} // namespace

void RefreshAppearanceHeadAndScale(CharacterAppearance& appearance)
{
    FinishAppearance(appearance);
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
        const bool raven = (characterClass & 7) == RavenClass && left == RavenType;
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
} // namespace Client::GameLogic
