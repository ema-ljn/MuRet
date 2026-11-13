#include "Scenes/HeroSkillCasts.h"

#include "Data/Tables/SkillTable.h"
#include "Render/Models/Cloth.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/WorldState.h"

#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Lo que comparten las ramas de las clases de 0x57cfc0 (CL9 hito 4, fase 2c; 06 § Usar una skill): el costo de una
// skill (0x551c70) y el camino sin maná (0x558110(3) y el corte de 0x5af520).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;

constexpr int EntryDraws = 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Skill.bmd con el maná (+0x22) y la stamina (+0x24) de la skill 0x2b.
Client::Data::Tables::SkillTable ACostlySkill()
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    const std::size_t base = std::size_t{0x2b} * Client::Data::Tables::SkillRecordSize;
    plain[base + 0x22] = std::byte{0x34};
    plain[base + 0x23] = std::byte{0x01};
    plain[base + 0x24] = std::byte{0x0a};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

struct Casting
{
    Client::Scenes::WorldState world;
    CrtRandom random;
    Client::Data::Tables::SkillTable skills = ACostlySkill();

    HeroSkillContext Context()
    {
        return HeroSkillContext{.world = world, .random = random, .skills = &skills};
    }
};

// 0x551c70(skill, 1, 0, &maná, 0, &stamina): los WORD +0x22 y +0x24, con los 60 rand() de la entrada al cerrarla.
TEST(HeroSkillCastsTest, TheCostReadsTheManaAndTheStaminaOfTheEntry)
{
    const auto tested = std::make_unique<Casting>();
    const CrtRandom expected = Advanced(tested->random, EntryDraws);
    const auto cost = SkillCostOf(0x2b, tested->Context(), "0x559b0b");
    ASSERT_TRUE(cost.HasValue()) << cost.Error();
    EXPECT_EQ(cost.Value().mana, 0x134);
    EXPECT_EQ(cost.Value().stamina, 0x0a);
    EXPECT_EQ(tested->random.State(), expected.State());
    // Abierta por fuera, no se vuelve a cifrar.
    tested->world.Runtime().movement.skillDepths[0x2b] = 1;
    const CrtRandom open = tested->random;
    ASSERT_TRUE(SkillCostOf(0x2b, tested->Context(), "0x559b0b").HasValue());
    EXPECT_EQ(tested->random.State(), open.State());
    // Sin Skill.bmd, la dirección.
    HeroSkillContext without = tested->Context();
    without.skills = nullptr;
    const auto missing = SkillCostOf(0x2b, without, "0x559b0b");
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x559b0b"), std::string::npos) << missing.Error();
}

// 0x559b29..0x559b41: sin una poción de maná en el inventario, nada (ni rand()); con una, 0x5af520 y el C1 26 cortan
// con la dirección del que llama y la casilla. Las de vida no cuentan.
TEST(HeroSkillCastsTest, WithoutManaTheSkillLooksForAPotion)
{
    const auto tested = std::make_unique<Casting>();
    auto& cells = tested->world.Inventory().cells;
    cells[9].id = 0x1c03;
    cells[9].occupied = 1;
    const CrtRandom before = tested->random;
    ASSERT_TRUE(UseAManaPotion(tested->Context(), "0x559b41").HasValue());
    EXPECT_EQ(tested->random.State(), before.State());
    cells[20].id = 0x1c06;
    cells[20].occupied = 1;
    const auto potion = UseAManaPotion(tested->Context(), "0x559b41");
    ASSERT_FALSE(potion.HasValue());
    EXPECT_NE(potion.Error().find("0x559b41"), std::string::npos) << potion.Error();
    EXPECT_NE(potion.Error().find("20"), std::string::npos) << potion.Error();
    EXPECT_NE(potion.Error().find("hito 5"), std::string::npos) << potion.Error();
}
} // namespace
