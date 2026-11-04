#include "GameLogic/CharacterAppearance.h"

#include "Protocol/Character/Opcodes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Client::GameLogic::CharacterAppearance: de la clase y los 17 bytes del equipo de la lista a los
// modelos de cada parte, como 0x517270, 0x5139f0 y 0x512930 (06 § Los personajes de la lista).
namespace
{
using Client::GameLogic::AppearanceOf;
using Client::GameLogic::BodyPart;
using Client::GameLogic::CharacterAppearance;
using Client::GameLogic::ClassAppearance;
using Client::GameLogic::ClassPartModel;
using Client::GameLogic::ItemLevelOf;
using Client::Protocol::Character::EquipmentSize;

using EquipmentBytes = std::array<std::byte, EquipmentSize>;

// Los modelos de la ropa de la clase: HelmClass a BootClass.
constexpr std::array<std::uint16_t, 5> ClassBase = {0x2212, 0x221a, 0x2222, 0x222a, 0x2232};

// Un equipo sin nada: las manos en 0xfff, las cinco partes en 0x1ff, sin alas (3 con b[8] & 7 en
// 0) y sin mascota (3 sin los bits de b[9] y b[11]).
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

// Un modelo opcional armado desde un std::uint16_t: desde un int, la conversión cae adentro del
// constructor de std::optional y MSVC la avisa (C4242), aunque el valor entre.
std::optional<std::uint16_t> Model(std::uint16_t model)
{
    return model;
}

// 0x517270 fuera de la escena 2: casco a botas de la clase, en HelmClass a BootClass más
// (clase >> 3) · 5 + (clase & 7), y las manos, las alas y la mascota vacías.
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

// La segunda clase (el bit 3) viste las partes de su clase corridas 5.
TEST(CharacterAppearanceTest, TheSecondClassWearsItsOwnParts)
{
    EXPECT_EQ(ClassPartModel(BodyPart::Helm, 0x08), 0x2212 + 5);
    EXPECT_EQ(ClassPartModel(BodyPart::Boots, 0x0A), 0x2232 + 5 + 2);
}

// Un índice 0x1ff es "vacío": queda la parte de la clase. Con el equipo vacío, como la clase sola.
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

// Q-C102: el tipo de las manos es de 12 bits y el original compara el vacío con 0x1fff, así que
// nunca da vacío: queda el modelo 0x1211, que nadie carga. MuRet toma 0xfff como sin ítem
// (diferencia nombrada): se ve lo mismo.
TEST(CharacterAppearanceTest, AnEmptyHandIsNoItem)
{
    const CharacterAppearance appearance = AppearanceOf(0, 0, Naked());

    EXPECT_FALSE(appearance.rightHand.has_value());
    EXPECT_FALSE(appearance.leftHand.has_value());
}

// Cada byte del equipo, por separado, va a su parte (la tabla de 0x5139f0 en 06).
TEST(CharacterAppearanceTest, EachEquipmentByteGivesItsItem)
{
    EquipmentBytes bytes = Naked();
    // La mano derecha: el tipo ((b[11] & 0xf0) << 4) | b[0], el grupo 1 y el índice 0x23.
    bytes[0] = std::byte{0x23};
    bytes[11] = std::byte{0x20};
    // La izquierda: ((b[12] & 0xf0) << 4) | b[1], el grupo 2 y el índice 4.
    bytes[1] = std::byte{0x04};
    bytes[12] = std::byte{0x40};
    // El casco: ((b[8] >> 7) + (b[12] & 0xf) · 2) · 16 + (b[2] >> 4), con b[12] & 0xf en 0 y el bit
    // 7 de b[8] en 1: 16 + 3.
    // La armadura: (((b[8] >> 6) & 1) + (b[13] >> 4) · 2) · 16 + (b[2] & 0xf): 2 · 16 + 5.
    bytes[2] = std::byte{0x35};
    bytes[13] = std::byte{0x16};
    // El pantalón: (((b[8] >> 5) & 1) + (b[13] & 0xf) · 2) · 16 + (b[3] >> 4): (1 + 12) · 16 + 7.
    // Los guantes: (((b[8] >> 4) & 1) + (b[14] >> 4) · 2) · 16 + (b[3] & 0xf): 2 · 16 + 9.
    bytes[3] = std::byte{0x79};
    bytes[14] = std::byte{0x10};
    // Las botas: (((b[8] >> 3) & 1) + (b[14] & 0xf) · 2) · 16 + (b[4] >> 4): 16 + 2; las alas, el
    // 1 de los bits 2 y 3 de b[4], y la mascota, el 0 de los bits 0 y 1.
    bytes[4] = std::byte{0x24};
    bytes[8] = std::byte{0xA8};
    // Los niveles, 3 bits cada uno desde b[7]: la derecha 1 (3), la izquierda 2 (5), el casco 3 (7),
    // la armadura 4 (9), el pantalón 5 (11), los guantes 6 (12) y las botas 7 (13).
    const std::uint32_t levels = 1 | (2 << 3) | (3 << 6) | (4 << 9) | (5 << 12) | (6 << 15) | (7 << 18);
    bytes[5] = static_cast<std::byte>(levels >> 16);
    bytes[6] = static_cast<std::byte>((levels >> 8) & 0xFF);
    bytes[7] = static_cast<std::byte>(levels & 0xFF);
    // Excelente: el casco (bit 7) y la mano izquierda (bit 1).
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

// 0x513340: los 3 bits de cada parte, de 0 a 7, dan los niveles 0, 3, 5, 7, 9, 11, 12 y 13
// (la tabla de 0x51338c).
TEST(CharacterAppearanceTest, TheLevelBitsGoThroughTheTable)
{
    constexpr std::array<std::uint8_t, 8> Levels = {0, 3, 5, 7, 9, 11, 12, 13};
    for (std::uint8_t bits = 0; bits < Levels.size(); ++bits)
    {
        EXPECT_EQ(ItemLevelOf(bits), Levels[bits]) << int{bits};
    }
}

// Las alas: con los bits 2 y 3 de b[4] en 3, b[8] & 7 da ninguna (0), la capa 0x1c30 (5) o
// 0x1a14 más el valor.
TEST(CharacterAppearanceTest, TheWingsOfTheThirdValueComeFromByteEight)
{
    EquipmentBytes bytes = Naked();
    bytes[8] = std::byte{0xF8 | 5};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).wings, Model(0x1c30));
    bytes[8] = std::byte{0xF8 | 2};
    EXPECT_EQ(AppearanceOf(0, 0, bytes).wings, Model(0x1a14 + 2));
}

// La mascota: con los bits 0 y 1 de b[4] en 3, el bit 0 de b[9] da 0x1c15; después, el bit 0 de
// b[11] la pisa con 0x1c16, y el 2 con 0x1c37, en ese orden.
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

// La clase 4 con el tipo 0xa05 en la izquierda lleva 0x1c17 (0x420cf0).
TEST(CharacterAppearanceTest, ClassFourWithTheTypeA05HoldsTheRaven)
{
    EquipmentBytes bytes = Naked();
    bytes[1] = std::byte{0x05};
    bytes[12] = std::byte{0xAF};

    EXPECT_EQ(AppearanceOf(4, 0, bytes).leftHand->model, 0x1c17);
    EXPECT_EQ(AppearanceOf(3, 0, bytes).leftHand->model, 0x212 + 0xa05);
}

// 0x512930: 0,9 o 0,93 en las clases 0 y 1, 0,88 o 0,86 en la 2, 0,95 en la 3 y 0,92 en la 4,
// según la piel sea 0 o no.
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

// 0x512930: la cabeza de la clase va solo con los cascos 0x1012, 0x1014 y 0x101c a 0x101f.
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
