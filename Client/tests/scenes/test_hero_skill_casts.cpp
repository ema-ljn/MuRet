#include "Network/Xor32Packet.h"
#include "Skills/HeroSkillCasts.h"

#include "Crt/CrtRandom.h"
#include "Tables/SkillTable.h"
#include "Core/Expected.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Skills/HeroSkills.h"
#include "Items/InventoryMoves.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Audio/SoundId.h"

#include "support/TableFiles.h"
#include "Movement/GameMovementState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// What the class branches of 0x57cfc0 share (06 § Usar una skill): a skill's cost (0x551c70) and the path without mana
// (0x558110(3) and 0x5af520's cut).
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;

constexpr int EntryDraws = 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Skill.bmd with skill 0x2b's mana (+0x22) and stamina (+0x24).
Client::Tables::SkillTable ACostlySkill()
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    const std::size_t base = std::size_t{0x2b} * Client::Tables::SkillRecordSize;
    plain[base + 0x22] = std::byte{0x34};
    plain[base + 0x23] = std::byte{0x01};
    plain[base + 0x24] = std::byte{0x0a};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

struct Casting
{
    Client::Kernel::WorldState world;
    CrtRandom random;
    Client::Tables::SkillTable skills = ACostlySkill();

    HeroSkillContext Context()
    {
        return HeroSkillContext{.world = world, .random = random, .skills = &skills};
    }
};

// 0x551c70(skill, 1, 0, &mana, 0, &stamina): the WORDs +0x22 and +0x24, with the entry's 60 rand() when it closes.
TEST(HeroSkillCastsTest, TheCostReadsTheManaAndTheStaminaOfTheEntry)
{
    const auto tested = std::make_unique<Casting>();
    const CrtRandom expected = Advanced(tested->random, EntryDraws);
    const auto cost = SkillCostOf(0x2b, tested->Context(), "0x559b0b");
    ASSERT_TRUE(cost.HasValue()) << cost.Error();
    EXPECT_EQ(cost.Value().mana, 0x134);
    EXPECT_EQ(cost.Value().stamina, 0x0a);
    EXPECT_EQ(tested->random.State(), expected.State());
    // Opened from outside, it is not encrypted again.
    tested->world.Runtime().movement.skillDepths[0x2b] = 1;
    const CrtRandom open = tested->random;
    ASSERT_TRUE(SkillCostOf(0x2b, tested->Context(), "0x559b0b").HasValue());
    EXPECT_EQ(tested->random.State(), open.State());
    HeroSkillContext without = tested->Context();
    without.skills = nullptr;
    const auto missing = SkillCostOf(0x2b, without, "0x559b0b");
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x559b0b"), std::string::npos) << missing.Error();
}

// 0x559b29..0x559b41: without a potion, nothing; with one, 0x5af520 uses the real slot, without anticipating the GS
// reply.
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
    auto context = tested->Context();
    std::vector<Client::Protocol::Outgoing> sent;
    context.send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); };
    tested->world.Runtime().ui.field78abb26 = 7;
    const auto potion = UseAManaPotion(context, "0x559b41");
    ASSERT_TRUE(potion.HasValue()) << potion.Error();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Client::Network::Delivery::Encrypted);
    const auto packet = Client::Network::Xor32Decode(sent[0].packet);
    const auto bytes = packet.Bytes();
    ASSERT_EQ(bytes.size(), 6u);
    EXPECT_EQ(bytes[2], std::byte{0x26});
    EXPECT_EQ(bytes[3], std::byte{32});
    EXPECT_EQ(bytes[4], std::byte{0});
    EXPECT_EQ(bytes[5], std::byte{7});
    EXPECT_EQ(tested->world.Runtime().ui.field5767954, 10);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x20});
    EXPECT_EQ(cells[20].id, 0x1c06);
    EXPECT_EQ(tested->random.State(), Advanced(before, 3).State());
    // With the wait active, only the protected trade is checked: no other send and no other sound.
    ASSERT_TRUE(UseAManaPotion(context, "0x559b41").HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(tested->random.State(), Advanced(before, 4).State());
}

TEST(HeroSkillCastsTest, ABlockedManaPotionShowsTheNativeNoticeBeforeTheWait)
{
    auto tested = std::make_unique<Casting>();
    auto& potion = tested->world.Inventory().cells[12];
    potion.id = 0x1c04;
    potion.occupied = 1;
    tested->world.Runtime().panels.warehouse = 1;
    tested->world.Runtime().ui.field5767954 = 5;
    std::vector<std::size_t> notices;
    const Client::Items::InventoryServices inventory{
        .systemMessage = [&notices](std::size_t text, std::int32_t type) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(type, 2);
            notices.push_back(text);
            return {};
        }};
    auto context = tested->Context();
    context.inventory = &inventory;
    const auto before = tested->random.State();
    const auto used = UseAManaPotion(context, "0x589936");
    ASSERT_TRUE(used.HasValue()) << used.Error();
    EXPECT_EQ(notices, std::vector<std::size_t>{474});
    EXPECT_EQ(tested->world.Runtime().ui.field5767954, 5);
    EXPECT_EQ(tested->random.State(), before);
}
} // namespace
