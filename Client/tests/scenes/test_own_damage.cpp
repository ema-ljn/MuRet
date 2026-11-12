#include "Scenes/OwnDamage.h"

#include "Data/Tables/SkillTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnStatHelpers.h"
#include "World/ItemState.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Los daños del personaje propio de la ventana del personaje (CL8 hito 5; 06 § La ventana del personaje): el ataque
// de 0x5b8b80 y la magia de 0x5513d0, con sus accesos y sus rand(). Los valores salen del ASM cotejado con el PE
// c32bbd5f; el registro, la lista de los sets y Skill.bmd son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::AttackDamageOf;
using Client::Scenes::ItemSetOptionList;
using Client::Scenes::MagicDamageOf;
using Client::Scenes::OwnAttackDamage;
using Client::Scenes::OwnCharacterRecord;
using Client::Scenes::OwnEquipmentOverride;

constexpr std::uint32_t Seed = 0x2468aceu;
constexpr std::uint32_t WizardClass = 0;
constexpr std::uint32_t KnightClass = 1;
constexpr std::uint32_t ElfClass = 2;
constexpr std::uint16_t EmptyType = 0xffff;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// El registro con los dos pares del ataque y las manos vacías.
OwnCharacterRecord Record()
{
    OwnCharacterRecord own;
    for (Client::World::ItemState& item : own.equipment)
        item.id = EmptyType;
    own.word50 = 100;
    own.word52 = 150;
    own.word54 = 30;
    own.word56 = 45;
    return own;
}

OwnAttackDamage Attack(const OwnCharacterRecord& own, std::uint32_t heroClass, const OwnEquipmentOverride& held = {})
{
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);
    const OwnAttackDamage damage = AttackDamageOf(own, heroClass, held, depth, random);
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(random.State(), expected.State());
    return damage;
}

// 0x5b8cb2..0x5b8d32: sin nada en la derecha, el segundo par; con un arco de la primera franja, el primero; con la
// izquierda de 0x800 a 0x806 y la derecha fuera de las franjas, el segundo. Cada llamada cierra el registro.
TEST(OwnDamageTest, TheHandsChooseThePairOfTheAttack)
{
    OwnCharacterRecord own = Record();
    OwnAttackDamage damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 30u);
    EXPECT_EQ(damage.max, 45u);
    EXPECT_FALSE(damage.highlighted);

    own.equipment[0].id = 0x808;
    damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 100u);
    EXPECT_EQ(damage.max, 150u);

    own.equipment[0].id = 0x0003;
    own.equipment[1].id = 0x0805;
    damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 30u);

    own.equipment[1].id = 0x0807;
    damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 100u);
}

// Las clases 1 y 3 con un arma en cada mano: el 55 % de cada par, sin signo y truncando, y el renglón en naranja. El
// bit 0x02 de +0x3c suma 15 a los dos.
TEST(OwnDamageTest, TwoWeaponsTakeFiftyFivePercentOfEachPair)
{
    OwnCharacterRecord own = Record();
    own.equipment[0].id = 0x0001;
    own.equipment[1].id = 0x0402;
    own.speedAdjustBits = 0x02;

    const OwnAttackDamage damage = Attack(own, KnightClass);

    EXPECT_TRUE(damage.highlighted);
    EXPECT_EQ(damage.min, 100u * 55u / 100u + 30u * 55u / 100u + 15u);
    EXPECT_EQ(damage.max, 150u * 55u / 100u + 45u * 55u / 100u + 15u);
}

// La clase 2 con las dos manos de 0x800 a 0x9ff: el 0x80f de la derecha o el 0x807 de la izquierda con nivel (el +4 &
// 0x78 en 8 o más) prende el naranja, sin cambiar el daño.
TEST(OwnDamageTest, TheElfLightsTheLineWithLeveledArrowsOrBolts)
{
    OwnCharacterRecord own = Record();
    own.equipment[0].id = 0x80f;
    own.equipment[1].id = 0x0803;
    EXPECT_FALSE(Attack(own, ElfClass).highlighted);
    own.equipment[0].scalar1Low = 0x08;
    EXPECT_TRUE(Attack(own, ElfClass).highlighted);
    EXPECT_FALSE(Attack(own, WizardClass).highlighted);

    own = Record();
    own.equipment[0].id = 0x0804;
    own.equipment[1].id = 0x807;
    own.equipment[1].scalar1Low = 0x07;
    EXPECT_FALSE(Attack(own, ElfClass).highlighted);
    own.equipment[1].scalar1Low = 0x10;
    EXPECT_TRUE(Attack(own, ElfClass).highlighted);
}

// El ítem del cursor ([0x7879f80]) toma el lugar de la mano de [0x78a6758] con [0x7879fbc] > 0 y la grilla del
// inventario; con el registro ya abierto, el cierre no saca nada.
TEST(OwnDamageTest, TheCursorItemReplacesAHandAndANestedAccessSpendsNothing)
{
    OwnCharacterRecord own = Record();
    OwnEquipmentOverride held{.dword7879fbc = 1, .window78ab9b8Is78a9bb0 = true, .slot78a6758 = 0, .item7879f80 = {}};
    held.item7879f80.id = 0x808;
    EXPECT_EQ(Attack(own, WizardClass, held).min, 100u);
    held.window78ab9b8Is78a9bb0 = false;
    EXPECT_EQ(Attack(own, WizardClass, held).min, 30u);

    std::uint8_t depth = 1;
    CrtRandom random{Seed};
    const CrtRandom before = random;
    static_cast<void>(AttackDamageOf(own, WizardClass, held, depth, random));
    EXPECT_EQ(depth, 1);
    EXPECT_EQ(random.State(), before.State());
}

// Skill.bmd inventado: la skill `skill` con sus bytes +0x21 y +0x30.
Client::Data::Tables::SkillTable SkillsWith(std::uint8_t skill, std::uint8_t damage, std::uint8_t setSelector)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    const std::size_t base = std::size_t{skill} * Client::Data::Tables::SkillRecordSize;
    plain[base + 0x21] = std::byte{damage};
    plain[base + 0x30] = std::byte{setSelector};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

// 0x5513d0: +0x5a + s y +0x5c + s + s / 2, más p, que junta la suma de 0x40d8c0 (32 bits) y la de 0x40d690 con el
// selector 0x9f sobre sus 16 bits de abajo. Las marcas empiezan en 0, y la entrada de la skill saca 60 rand() al
// cerrarse.
TEST(OwnDamageTest, TheMagicAddsTheSkillAndTheSetOptions)
{
    OwnCharacterRecord own = Record();
    own.word5a = 10;
    own.word5c = 20;
    const auto skills = SkillsWith(5, 31, 3);
    ItemSetOptionList list;
    list.count9348 = 3;
    list.entries[0] = {0x20, 1, 0};
    list.values[0] = 7;
    list.entries[1] = {0x13, 0, 0};
    list.values[1] = 70000;
    list.entries[2] = {0x20, 1, 0};
    list.values[2] = 2;
    list.used[2] = 1;
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom expected = Advanced(random, static_cast<int>(Client::Data::Tables::SkillRecordSize));

    const auto damage = MagicDamageOf(own, 5, &skills, depth, random, list);

    ASSERT_TRUE(damage.HasValue()) << damage.Error();
    // p = 7 + 2 por 0x40d8c0 (la marca de la tercera se borró antes) y sus 16 bits de abajo más 70.000.
    const std::int32_t bonus = (7 + 2 + 70000) & 0xffff;
    EXPECT_EQ(damage.Value().min, 10 + 31 + bonus);
    EXPECT_EQ(damage.Value().max, 20 + 31 + 15 + bonus);
    EXPECT_EQ(list.used[0], 1);
    EXPECT_EQ(list.used[1], 1);
    EXPECT_EQ(list.used[2], 1);
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(random.State(), expected.State());
}

// Sin Skill.bmd o sin la skill, error antes de abrir la entrada; con más de 16 entradas en la lista, el de 0x40d8c0.
TEST(OwnDamageTest, TheMagicStopsWithoutItsTables)
{
    const OwnCharacterRecord own = Record();
    ItemSetOptionList list;
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom before = random;

    const auto missing = MagicDamageOf(own, 5, nullptr, depth, random, list);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5513d0"), std::string::npos);
    EXPECT_EQ(random.State(), before.State());

    const auto skills = SkillsWith(5, 1, 0);
    list.count9348 = 17;
    const auto overflow = MagicDamageOf(own, 5, &skills, depth, random, list);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x40d8c0"), std::string::npos);
    EXPECT_EQ(depth, 0);
}
} // namespace
