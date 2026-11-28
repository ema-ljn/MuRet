#include "Network/Xor32Packet.h"
#include "Skills/HeroSkillsElf.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill55faa0.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The elf branch of 0x57cfc0 (0x57d790..0x58187c) and 0x55faa0, called directly, with an invented Skill.bmd.
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// The head (0x57d790..0x57e12a) without a delay: the own record and, with it open, the cost, the delay and the energy.
constexpr int HeadDraws = OwnDraws + 3 * EntryDraws;
// The hero's logical cell: the Y and the X.
constexpr int HeroCellDraws = 2 * CellDraws;
// 0x4f5290 on the elf: 0x4f3f80 (the own record) and action 0x92, without rand().
constexpr int ElfHitDraws = OwnDraws;
// 0x55faa0 before its case: the published hero's cell for the D3 (the X and the Y) and the own record with the skill.
constexpr int ReachedDraws = HeroCellDraws + OwnDraws;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint32_t Ticks = 100000;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint16_t BowModel = 0xa12; // a bow (0x800) in the left hand

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

struct InventedSkill
{
    std::uint8_t skill = 0;
    std::uint8_t mana = 0;
    std::uint8_t stamina = 0;
    std::uint8_t level = 0;
    std::uint8_t energy = 0;
    std::uint16_t delay = 0;
    std::uint8_t range = 4;
};

Client::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Tables::SkillRecordSize;
        plain[base + 0x20] = static_cast<std::byte>(invented.level);
        plain[base + 0x22] = static_cast<std::byte>(invented.mana);
        plain[base + 0x24] = static_cast<std::byte>(invented.stamina);
        plain[base + 0x26] = static_cast<std::byte>(invented.range);
        plain[base + 0x28] = static_cast<std::byte>(invented.delay & 0xff);
        plain[base + 0x29] = static_cast<std::byte>(invented.delay >> 8);
        plain[base + 0x2c] = static_cast<std::byte>(invented.energy);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

struct ElfHero
{
    explicit ElfHero(std::uint8_t used, std::int32_t map = 0) : skill(used)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero.alive = true;
        hero.type = Client::Tables::PlayerSkeletonModel;
        hero.characterClass = ElfClass;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Kernel::NoItemModel;
        hero.alpha = 1.0f;
        hero.animation.action = 1;
        hero.skillIndex = 0;
        world.OwnCharacter().SetSkillAt(0, used);
        auto& chosen = Chosen();
        chosen.alive = true;
        chosen.type = 0x20;
        chosen.byte88 = 2;  // a monster: 0x558bb0 lets it be attacked
        chosen.byte370 = 2; // and 0x558eb0 gives its key
        chosen.key = ChosenKey;
        chosen.alpha = 1.0f;
        chosen.position = {18450.0f, 11750.0f, 0.0f};
        runtime.selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
        runtime.movement.selectedSkillRange = 4.0f;
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Kernel::ActorState& Chosen()
    {
        return world.Actors()[ChosenSlot];
    }
    Client::Movement::GameMovementState& Movement()
    {
        return world.Runtime().movement;
    }
    // An ally: a player that 0x558bb0 does not let be attacked.
    void ChooseAnAlly()
    {
        auto& chosen = Chosen();
        chosen.type = Client::Tables::PlayerSkeletonModel;
        chosen.byte88 = 1;
        chosen.byte370 = 0;
        chosen.hands[0].model = chosen.hands[1].model = Client::Kernel::NoItemModel;
    }
    // Without a chosen one, 0x57ce80 aims at the terrain, at (183, 116).
    void AimAtTheTerrain()
    {
        world.Runtime().selectedTargets[2] = -1;
        terrainPoint = glm::vec3{18350.0f, 11650.0f, 0.0f};
    }
    void CallIt(const std::string& name)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            Hero().name[i] = static_cast<std::byte>(name[i]);
    }
    HeroSkillContext Context()
    {
        HeroSkillContext context{.world = world,
                                 .random = random,
                                 .skills = &skills,
                                 .playerSkeleton = &skeleton,
                                 .tickCount = [this] { return ticks; },
                                 .worldTime = 5000.0f,
                                 .send = [this](Client::Protocol::Outgoing outgoing)
                                 { sent.push_back(std::move(outgoing)); },
                                 .playerKey = PlayerKey,
                                 .terrain = &terrain};
        if (terrainPoint)
            context.pickTerrainPoint = [this]() -> Client::Core::Expected<std::optional<glm::vec3>, std::string>
            { return terrainPoint; };
        return context;
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return Client::Skills::UseTheElfSkill(Hero(), skill, Context());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        return ClearBytesOf(sent.at(index));
    }

    std::uint8_t skill;
    Client::Kernel::WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    std::uint32_t ticks = Ticks;
    std::optional<glm::vec3> terrainPoint;
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Tables::SkillTable skills =
        SkillsWith({{0x13}, {0x1a}, {0x1b}, {0x1c}, {0x1e}, {0x24}, {0x2e}, {0x33}, {0x34}, {0x35}, {0x43}, {0x4c}});
};

std::unique_ptr<ElfHero> AnElf(std::uint8_t skill, std::int32_t map = 0)
{
    return std::make_unique<ElfHero>(skill, map);
}

// 0x57d808..0x57d854: without the mana, the mana potion (without it, nothing); with the stamina above +0x38 or
// 0x551f40's energy above +0x1e + +0x30, nothing.
TEST(HeroSkillsElfTest, TheHeadPaysForTheSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x1a, map);
        tested->skills = SkillsWith({{0x1a, 5}});
        const CrtRandom expected = Advanced(tested->random, OwnDraws + EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().targetX, 0u);
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        const auto potionExpected = Advanced(tested->random, OwnDraws + EntryDraws + 3);
        const auto again = tested->Use();
        ASSERT_TRUE(again.HasValue()) << again.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 6, 0x26, 24, 0, 0}));
        EXPECT_EQ(tested->random.State(), potionExpected.State());
    }
    {
        const auto tested = AnElf(0x1a);
        tested->skills = SkillsWith({{0x1a, 0, 5}});
        const CrtRandom expected = Advanced(tested->random, OwnDraws + EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        tested->world.OwnCharacter().bp = 5;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    // 10 · 5 · 4 / 100 + 20 = 22.
    const auto tested = AnElf(0x1a);
    tested->skills = SkillsWith({{0x1a, 0, 0, 10, 5}});
    const CrtRandom expected = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    tested->world.OwnCharacter().energy = 12;
    tested->world.OwnCharacter().word30 = 10;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 1u);
}

// 0x57f252..0x57f9af: the summons (0x1e to 0x24) send the 19 with the player's key and set action 0x92 (the elf's
// 0x4f5290), without checking the reach. On map 10 and in Chaos Castle (18 to 23), nothing after the head.
TEST(HeroSkillsElfTest, TheSummonsSendTheMagicAttackWithThePlayerKey)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1e}, std::uint8_t{0x24}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AnElf(skill, map);
            tested->Movement().selectedSkillRange = 1.0f;
            const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + ElfHitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 0x92);
            EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
            EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
            EXPECT_EQ(tested->Movement().targetX, 184u);
        }
    }
    for (const std::int32_t map : {10, 18, 23})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x1e, map);
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
    {
        // With the brake or with "webzen" in the name, no 19, but the action.
        const auto braked = AnElf(0x1e);
        braked->Movement().lastActionTick = Ticks - 300;
        ASSERT_TRUE(braked->Use().HasValue());
        EXPECT_TRUE(braked->sent.empty());
        EXPECT_EQ(braked->Hero().animation.action, 0x92);
        const auto named = AnElf(0x1e, 2);
        named->CallIt("awebzen");
        ASSERT_TRUE(named->Use().HasValue());
        EXPECT_TRUE(named->sent.empty());
        EXPECT_EQ(named->Movement().lastActionTick, 0u);
        EXPECT_EQ(named->Hero().animation.action, 0x92);
    }
}

// 0x57e14d..0x57f005: 0x34 with the published hero's bow, arrows (with no missile weapon in the own record, 0x561c50
// gives 1) and the target's cell in reach: the heading toward the target (135°), the 1E with the logical cell, the
// heading · 256/360, two zeros and 0x558eb0's key with 0x7758588, and 0x4f4a10. Then it goes on through the reach
// (0x57f008), which with a monster and 0x34 does nothing more.
TEST(HeroSkillsElfTest, Skill34ShootsAtTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x34, map);
        tested->Hero().hands[1].model = BowModel;
        tested->Movement().weaponSkill.target = static_cast<std::int32_t>(ChosenSlot);
        // 0x561c50 and 0x4f3f80 (in 0x4f4a10) open and close the own record; the line of sight, the cell again.
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + OwnDraws + HeroCellDraws + SendDraws + OwnDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x34, 182, 115, 0x60, 0, 0, 0x0a, 0x1b, 0}));
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0x34u);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
    }
    {
        // Without the weapon skill's target, the key 0xffff.
        const auto tested = AnElf(0x34);
        tested->Hero().hands[1].model = BowModel;
        tested->Movement().weaponSkill.target = -1;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0)[9], 0xff);
        EXPECT_EQ(tested->Sent(0)[10], 0xff);
    }
    {
        // Without a missile weapon or out of reach, no 1E: it goes on through the reach.
        const auto unarmed = AnElf(0x34);
        const CrtRandom expected = Advanced(unarmed->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(unarmed->Use().HasValue());
        EXPECT_EQ(unarmed->random.State(), expected.State());
        EXPECT_TRUE(unarmed->sent.empty());
        const auto far = AnElf(0x34, 2);
        far->Hero().hands[1].model = BowModel;
        far->Movement().selectedSkillRange = 1.0f;
        ASSERT_TRUE(far->Use().HasValue());
        EXPECT_TRUE(far->sent.empty());
        EXPECT_FLOAT_EQ(far->Hero().angles.z, 0.0f);
    }
    {
        // With "webzen", neither the 1E nor 0x5767948, but 0x4f4a10.
        const auto tested = AnElf(0x34);
        tested->Hero().hands[1].model = BowModel;
        tested->CallIt("webzen");
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
    }
}

// 0x57fa46..0x580187 and 0x580188..0x5808d3: 0x1a to 0x1c without a chosen player (on the terrain or a monster) and
// 0x35 send the 19 with the player's key and set 0x4f5290; 0x35 also sets +0x378 to 0.
TEST(HeroSkillsElfTest, TheSupportSkillsSendTheMagicAttackWithThePlayerKey)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1a}, std::uint8_t{0x1c}, std::uint8_t{0x35}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AnElf(skill, map);
            tested->Hero().movement = 1;
            const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + SendDraws + ElfHitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 0x92);
            EXPECT_EQ(tested->Hero().movement, skill == 0x35 ? 0 : 1);
            EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
        }
    }
    const auto tested = AnElf(0x1b);
    tested->AimAtTheTerrain();
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x1b, 0x12, 0x34}));
    EXPECT_EQ(tested->Movement().targetX, 183u);
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
}

// 0x57f203..0x57f251 → 0x55faa0: 0x1a to 0x1c with a chosen player in sight: the turn D3 first (with the previous
// angle, 0: 0x10), the 19 with the chosen one's key and 0x4f5290; nothing more.
TEST(HeroSkillsElfTest, TheSupportSkillsOnAnAllyGoThroughTheTarget)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x1b, map);
        tested->ChooseAnAlly();
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + ReachedDraws + SendDraws + ElfHitDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Clear);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x1b, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Hero().animation.action, 0x92);
    }
    // Out of reach, the path to the ally with +0x379 at 5 (0x57f0e3..0x57f15e), and nothing more.
    const auto tested = AnElf(0x1a, 2);
    tested->ChooseAnAlly();
    tested->Movement().selectedSkillRange = 1.0f;
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Hero().movement, 1);
    EXPECT_EQ(tested->Hero().movementType, 5);
    EXPECT_GT(tested->Hero().path.count, 0);
}

// 0x57f9b0..0x57fa25 → 0x55faa0: a player's 0x33 and 0x2e with the bow, on an attackable one in sight: the D3, the 19
// with the chosen one's key and the action (0x4f4a10 for 0x33; 0x4f48e0 for 0x2e: 0x7e without wings, 0x80 with
// them); then the branch goes on to 0x5808da, which does nothing with them.
TEST(HeroSkillsElfTest, TheShotsOfTheWeaponGoThroughTheTarget)
{
    for (const std::uint8_t skill : {std::uint8_t{0x33}, std::uint8_t{0x2e}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AnElf(skill, map);
            tested->Hero().hands[1].model = BowModel;
            // 0x561c50 and 0x4f3f80 open and close the own record.
            const CrtRandom expected =
                Advanced(tested->random, HeadDraws + HeroCellDraws + ReachedDraws + OwnDraws + SendDraws + OwnDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 2u);
            EXPECT_EQ(tested->Sent(0)[2], 0xd3);
            EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x0a, 0x1b}));
            if (skill == 0x2e)
            {
                EXPECT_EQ(tested->Hero().animation.action, 0x7e);
            }
        }
    }
    {
        const auto tested = AnElf(0x2e);
        tested->Hero().hands[1].model = BowModel;
        tested->Hero().wings.model = 0xc12;
        const std::uint8_t before = tested->Hero().attackCount;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->Hero().animation.action, 0x80);
        EXPECT_EQ(tested->Hero().attackCount, before + 1);
    }
    // Without the bow, 0x55faa0 does not run: neither the D3 nor the 19.
    const auto tested = AnElf(0x33);
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
}

// 0x58097a..0x5817d6: 0x4c in reach with an attackable chosen one: the 1E with the logical cell, the heading, the cell
// of 0x4e9920 in byte 8, 0x558eb0's key and the magic key; +0x434 at the chosen one, +0x378 at 0 and the action by the
// hands (0x62, none). Out of reach, the path up to the reach · 1.2 without +0x379; without a chosen one, 0x6c8e38 at
// −1.
TEST(HeroSkillsElfTest, TheSkill4cCastsWithThePositionAndTheMagicKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x4c, map);
        tested->Hero().movement = 1;
        // The line of sight (the cell), 0x552990's delay (the record and the entry) and the cell again in the 1E.
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + EntryDraws + HeroCellDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x4c, 182, 115, 0x60, 0, 0xaa, 0x0a, 0x1b, 1}));
        EXPECT_EQ(tested->Hero().skillTarget4c, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Hero().magicKey, 1);
        EXPECT_EQ(tested->Hero().animation.action, 0x62);
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0x4cu);
    }
    {
        const auto tested = AnElf(0x4c, 2);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 0);
        EXPECT_GT(tested->Hero().path.count, 0);
    }
    const auto tested = AnElf(0x4c);
    tested->AimAtTheTerrain();
    tested->Movement().selectedSkillRange = 1.0f;
    tested->terrainPoint = glm::vec3{18850.0f, 11550.0f, 0.0f};
    tested->world.Runtime().selectedTargets[4] = 2;
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
    EXPECT_EQ(tested->Hero().movement, 0);
}

// P-C256: out of reach, a target with its cell outside 0..255 (which PathFinding2 takes as a linear index) gets no
// path: after the reads of the hero's cell, the path is discarded without cutting, for 0x4c (0x581853, with the cell
// read before) and toward an ally (0x57f148).
TEST(HeroSkillsElfTest, AWalkToACellOffTheGridIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const glm::vec3 position : {glm::vec3{-1000.0f, 11750.0f, 0.0f}, glm::vec3{18450.0f, 30000.0f, 0.0f}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(position.x));
            const auto skill4c = AnElf(0x4c, map);
            skill4c->Chosen().position = position;
            const CrtRandom walked = Advanced(skill4c->random, HeadDraws + HeroCellDraws + HeroCellDraws);
            const auto result = skill4c->Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(skill4c->random.State(), walked.State());
            EXPECT_TRUE(skill4c->sent.empty());
            EXPECT_EQ(skill4c->Hero().movement, 0);
            EXPECT_EQ(skill4c->Hero().path.count, 0);

            const auto ally = AnElf(0x1a, map);
            ally->ChooseAnAlly();
            ally->Chosen().position = position;
            const CrtRandom expected = Advanced(ally->random, HeadDraws + HeroCellDraws);
            const auto supported = ally->Use();
            ASSERT_TRUE(supported.HasValue()) << supported.Error();
            EXPECT_EQ(ally->random.State(), expected.State());
            EXPECT_TRUE(ally->sent.empty());
            EXPECT_EQ(ally->Hero().movement, 0);
            EXPECT_EQ(ally->Hero().movementType, 0);
        }
    }
    // 0x47 toward an ally goes on to the line of sight (0x57f1e5), which with the Y at 300 falls outside TerrainWall
    // (0x55886d): MuRet takes it as no line and the skill goes on without cutting.
    const auto tested = AnElf(0x47, 2);
    tested->ChooseAnAlly();
    tested->Chosen().position = glm::vec3{18450.0f, 30000.0f, 0.0f};
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->Hero().path.count, 0);
}

// 0x57f098..0x57f184: out of reach, a monster gets no path (only a player, +0x88 at 1); the other skills end there.
// 0x43 to 0x47 go on through the line of sight to the tail (0x5808da, HeroSkillsTail); 0x48, only in reach.
TEST(HeroSkillsElfTest, OutOfReachOnlyAPlayerIsWalkedTo)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElf(0x13, map);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
    }
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        // 0x47: the line of sight and, in the tail, the 19 with the chosen one's key and action 0x87.
        const auto tested = AnElf(0x47, map);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + HeadDraws + SendDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x47, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, 0x87);
    }
    {
        // 0x48 out of reach ends with the head.
        const auto tested = AnElf(0x48);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
    // In reach, 0x48 gets to the tail.
    const auto tested = AnElf(0x48, 2);
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + HeadDraws + SendDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x48, 0x0a, 0x1b}));
    EXPECT_EQ(tested->Hero().animation.action, 0x3f);
}

// 0x55faa0 with the other skills: the D3 and the angle, and nothing more.
TEST(HeroSkillsElfTest, TheTargetOfTheElfDoesNothingWithOtherSkills)
{
    const auto tested = AnElf(0x13);
    tested->Movement().weaponSkill = {.active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
    const CrtRandom expected = Advanced(tested->random, ReachedDraws);
    ASSERT_TRUE(
        Client::Skills::UseTheElfSkillOnItsTarget(tested->Hero(), tested->Hero(), tested->Context()).HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[2], 0xd3);
    EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
    tested->Movement().weaponSkill.target = -1;
    const auto result = Client::Skills::UseTheElfSkillOnItsTarget(tested->Hero(), tested->Hero(), tested->Context());
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x560080")) << result.Error();
}
} // namespace

// 0x4c (Fenrir) out of reach on map 34, with Crywolf at stage 3 and an altar (0x206) in 0x6c8e54: PathFinding2 sets
// the A* flag (0x4eac0a..0x4eac41), which does not count with a nonzero distance (0x4eadd4). The path, the rand() and
// +0x378 are the same as with another NPC (0x203) chosen.
TEST(HeroSkillsElfTest, TheApproachWalksTheSameWithACrywolfAltar)
{
    const auto approach = [](std::uint16_t chosenType)
    {
        auto tested = AnElf(0x4c, 34);
        tested->Movement().selectedSkillRange = 1.0f;
        tested->world.Runtime().crywolf.stage = 3;
        tested->world.Runtime().chosenNpc.index = 30;
        tested->world.Actors()[30].type = chosenType;
        const auto expected = Advanced(tested->random, HeadDraws + 2 * HeroCellDraws);
        const auto used = tested->Use();
        EXPECT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        return tested;
    };
    const auto altar = approach(0x206);
    const auto other = approach(0x203);
    EXPECT_EQ(altar->Hero().movement, 1);
    EXPECT_EQ(altar->Hero().movementType, 0);
    ASSERT_GT(altar->Hero().path.count, 0);
    EXPECT_EQ(altar->Hero().path.count, other->Hero().path.count);
    for (std::size_t cell = 0; cell < altar->Hero().path.count; ++cell)
    {
        EXPECT_EQ(altar->Hero().path.cells[cell].x, other->Hero().path.cells[cell].x);
        EXPECT_EQ(altar->Hero().path.cells[cell].y, other->Hero().path.cells[cell].y);
    }
    EXPECT_TRUE(altar->sent.empty());
}
