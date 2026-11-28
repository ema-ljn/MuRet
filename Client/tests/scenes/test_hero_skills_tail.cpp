#include "Network/Xor32Packet.h"
#include "Skills/HeroSkillsTail.h"

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
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Terrain/WorldTerrain.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The tail of 0x57cfc0 with skills 0x43 to 0x48 (0x5808da..0x599d5d), called directly and from 0x57cfc0, with an
// invented Skill.bmd.
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// The tail's head (0x5808ef..0x594002) without a delay: the own record and, with it open, the cost, delay (0x552c50)
// and energy (0x551f40) entries.
constexpr int TailHeadDraws = OwnDraws + 3 * EntryDraws;
// Without the mana or without the stamina, no delay.
constexpr int UnpaidHeadDraws = OwnDraws + 2 * EntryDraws;
// The hero's logical cell: the Y and the X (or the X and the Y).
constexpr int HeroCellDraws = 2 * CellDraws;
// The whole 0x43: the heading's cell, the 1E's, and the C3's padding and serial. 0x4f3f80 runs with the own record
// open, without rand().
constexpr int SternDraws = TailHeadDraws + 2 * HeroCellDraws + SendDraws;
constexpr int MagicAttackDraws = TailHeadDraws + SendDraws;
// The head of 0x57cfc0 up to each hand's skill: the own record with the slot's skill, the range entry, the +0x31 one
// (0x57d2f1) and the two logical cells (0x57d4d6, 0x57d4f4).
constexpr int AimDraws = OwnDraws + 2 * EntryDraws + 2 * CellDraws;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint32_t Ticks = 100000;
constexpr std::uint8_t StandingAction = 0x3f;
constexpr std::uint8_t CancelAction = 0x87;

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

std::uint8_t DefaultActionOf(std::uint8_t skill)
{
    return skill == 0x44 || skill == 0x47 ? CancelAction : StandingAction;
}

// The hero of class `heroClass` with the skill in slot 0 and the target 0x57cfc0 left it: +0x3a8 at the chosen one and
// 0x7758588 at its slot.
struct TailHero
{
    explicit TailHero(std::uint8_t used, std::int32_t map = 0, std::uint8_t heroClass = 5) : skill(used)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero.alive = true;
        hero.type = Client::Tables::PlayerSkeletonModel;
        hero.characterClass = heroClass;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Kernel::NoItemModel;
        hero.alpha = 1.0f;
        hero.animation.action = 1;
        hero.movement = 1;
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
        hero.attackPosition = chosen.position;
        runtime.selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
        runtime.movement.weaponSkill.target = static_cast<std::int32_t>(ChosenSlot);
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
    void CallIt(const std::string& name)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            Hero().name[i] = static_cast<std::byte>(name[i]);
    }
    HeroSkillContext Context()
    {
        return HeroSkillContext{.world = world,
                                .random = random,
                                .skills = &skills,
                                .playerSkeleton = &skeleton,
                                .tickCount = [this] { return ticks; },
                                .worldTime = 5000.0f,
                                .send = [this](Client::Protocol::Outgoing outgoing)
                                { sent.push_back(std::move(outgoing)); },
                                .playerKey = PlayerKey,
                                .terrain = &terrain};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return Client::Skills::UseTheSharedTail(Hero(), skill, Context());
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
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Tables::SkillTable skills = SkillsWith({{0x43}, {0x44}, {0x45}, {0x46}, {0x47}, {0x48}, {0x13}, {0x49}});
};

std::unique_ptr<TailHero> AHero(std::uint8_t skill, std::int32_t map = 0, std::uint8_t heroClass = 5)
{
    return std::make_unique<TailHero>(skill, map, heroClass);
}

// 0x5808da..0x5808e9: outside 0x43 to 0x48, nothing, not even rand().
TEST(HeroSkillsTailTest, OutsideTheTailNothing)
{
    for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x42}, std::uint8_t{0x49}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AHero(skill);
        const CrtRandom expected = tested->random;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
        EXPECT_EQ(tested->Hero().movement, 1);
    }
}

// 0x580916..0x580922: without a player (+2 at 0x20a), 0x551c70's cost and the close of the own record, nothing more.
TEST(HeroSkillsTailTest, OnlyAPlayerGoesOn)
{
    for (const std::uint8_t skill : {std::uint8_t{0x43}, std::uint8_t{0x46}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AHero(skill);
        tested->Hero().type = 0x20;
        const CrtRandom expected = Advanced(tested->random, OwnDraws + EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
        EXPECT_EQ(tested->Hero().movement, 1);
    }
}

// 0x580928..0x593fe0: without the mana, the potion (without it, it goes on); without the stamina, no delay; with them,
// 0x552c50's delay, whose result nobody checks; 0x551f40's energy, which nobody compares. None of it stops the skill
// (Q-C1226).
TEST(HeroSkillsTailTest, TheHeadNeverStopsTheSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHero(0x44, map);
        tested->skills = SkillsWith({{0x44, 5}});
        const CrtRandom expected = Advanced(tested->random, UnpaidHeadDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x44, 0x0a, 0x1b}));
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        tested->ticks += 1000;
        const auto potionExpected = Advanced(tested->random, UnpaidHeadDraws + 3 + SendDraws);
        const auto again = tested->Use();
        ASSERT_TRUE(again.HasValue()) << again.Error();
        ASSERT_EQ(tested->sent.size(), 3u);
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 6, 0x26, 24, 0, 0}));
        EXPECT_EQ(tested->Sent(2), tested->Sent(0));
        EXPECT_EQ(tested->random.State(), potionExpected.State());
    }
    {
        const auto tested = AHero(0x45);
        tested->skills = SkillsWith({{0x45, 0, 5}});
        const CrtRandom expected = Advanced(tested->random, UnpaidHeadDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    {
        // 10 · 5 · 4 / 100 + 20 = 22 energy, without it in the record.
        const auto tested = AHero(0x43);
        tested->skills = SkillsWith({{0x43, 0, 0, 10, 5}});
        const CrtRandom expected = Advanced(tested->random, SternDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    // With the delay running, 0x552c50 says no and the skill goes out anyway.
    const auto tested = AHero(0x48, 2);
    tested->skills = SkillsWith({{0x48, 0, 0, 0, 0, 1000}});
    ASSERT_TRUE(tested->Use().HasValue());
    tested->ticks += 1000;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 2u);
}

// 0x594009..0x594de9: 0x43 turns toward +0x3a8 (135°), sends the 1E with the logical cell (the X and the Y), the
// heading · 256/360, the target's cell from the logical cell (0x5ae300), 0x4e9920's from the position, 0x558eb0's key
// with 0x7758588 and the magic key 0 from 0x57cf90(0); 0x5767948 at 0x43, without the brake or the counter 0x77f3bad.
// Then 0x4f3f80 and action 0x3f, with +0x378 at 0.
TEST(HeroSkillsTailTest, TheSternSendsTheDurationMagic)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHero(0x43, map);
        tested->Movement().magicKeyCounter = 7;
        const CrtRandom expected = Advanced(tested->random, SternDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 182, 115, 0x60, 0xaa, 0xaa, 0x0a, 0x1b, 0}));
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0x43u);
        EXPECT_EQ(tested->Movement().lastActionTick, 0u);
        EXPECT_EQ(tested->Movement().magicKeyCounter, 7);
        EXPECT_EQ(tested->Hero().magicKey, 0);
        EXPECT_EQ(tested->Hero().animation.action, StandingAction);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    {
        // Byte 7 comes from the logical cell (+0x41c, here 180) and byte 8 from the position: when they differ, so do
        // the bytes.
        const auto tested = AHero(0x43);
        tested->Hero().cellX = 180;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 180, 115, 0x60, 0xca, 0xaa, 0x0a, 0x1b, 0}));
    }
    {
        // Lejos: 0x5ae300 recorta a 7 (0xf8); 0x4e9920 no recorta (0x94).
        const auto tested = AHero(0x43, 2);
        tested->Hero().attackPosition = {19450.0f, 11550.0f, 0.0f};
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0)[7], 0xf8);
        EXPECT_EQ(tested->Sent(0)[8], 0x94);
    }
    {
        // Without the weapon skill's target (0x7758588 at −1), the key 0xffff.
        const auto tested = AHero(0x43);
        tested->Movement().weaponSkill.target = -1;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0)[9], 0xff);
        EXPECT_EQ(tested->Sent(0)[10], 0xff);
    }
    // With "webzen", neither the 1E nor 0x5767948 (nor the second read of the cell), but the turn and the action.
    const auto tested = AHero(0x43, 2);
    tested->CallIt("awebzen");
    const CrtRandom expected = Advanced(tested->random, TailHeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
    EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
    EXPECT_EQ(tested->Hero().animation.action, StandingAction);
    EXPECT_EQ(tested->Hero().movement, 0);
}

// 0x594e42..0x599d1c: 0x44 to 0x48 send the 19 with the chosen one's key (+0x1f4 of 0x6c8e30) or, without one, the
// player's (0x57678bc), after the 300 ms brake; action 0x87 for 0x44 and 0x47, 0x3f for the rest, and +0x378 at 0.
TEST(HeroSkillsTailTest, TheMagicAttacksSendTheChosenKey)
{
    for (std::uint8_t skill = 0x44; skill <= 0x48; ++skill)
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AHero(skill, map);
            const CrtRandom expected = Advanced(tested->random, MagicAttackDraws);
            const auto result = tested->Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x0a, 0x1b}));
            EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
            EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
            EXPECT_EQ(tested->Hero().animation.action, DefaultActionOf(skill));
            EXPECT_EQ(tested->Hero().movement, 0);
        }
        SCOPED_TRACE(skill);
        const auto tested = AHero(skill);
        tested->world.Runtime().selectedTargets[2] = -1;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
    }
    {
        // The chosen one's key without checking whether it can be attacked: an ally too.
        const auto tested = AHero(0x46);
        tested->Chosen().byte88 = 1;
        tested->Chosen().byte370 = 0;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x46, 0x0a, 0x1b}));
    }
    // A chosen one outside the pool cuts where its key is read.
    const auto tested = AHero(0x44);
    tested->world.Runtime().selectedTargets[2] = 1000;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x595858")) << result.Error();
}

// The brake (300 ms or less since 0x576791c) and "webzen" leave 0x44 to 0x48 without the 19, but with the action.
TEST(HeroSkillsTailTest, TheBrakeAndTheSilentNameKeepTheAction)
{
    for (const std::uint8_t skill : {std::uint8_t{0x44}, std::uint8_t{0x48}})
    {
        SCOPED_TRACE(skill);
        const auto braked = AHero(skill);
        braked->Movement().lastActionTick = Ticks - 300;
        const CrtRandom expected = Advanced(braked->random, TailHeadDraws);
        ASSERT_TRUE(braked->Use().HasValue());
        EXPECT_EQ(braked->random.State(), expected.State());
        EXPECT_TRUE(braked->sent.empty());
        EXPECT_EQ(braked->Movement().lastActionTick, Ticks - 300);
        EXPECT_EQ(braked->Hero().animation.action, DefaultActionOf(skill));
        const auto named = AHero(skill, 2);
        named->world.Runtime().selectedTargets[2] = -1;
        named->CallIt("webzen");
        ASSERT_TRUE(named->Use().HasValue());
        EXPECT_TRUE(named->sent.empty());
        EXPECT_EQ(named->Movement().lastActionTick, 0u);
        EXPECT_EQ(named->Hero().animation.action, DefaultActionOf(skill));
        EXPECT_EQ(named->Hero().movement, 0);
    }
}

// 0x594dee..0x594e3d and 0x599d27..0x599d45: outside the safe zone, the dark horse 0x52, Uniria 0x40, Dinorant 0x41
// and Fenrir 0x5d (EBX); inside it, the action without a mount.
TEST(HeroSkillsTailTest, TheMountChoosesTheAction)
{
    struct Case
    {
        std::uint16_t mount;
        std::uint8_t action;
    };
    for (const std::uint8_t skill : {std::uint8_t{0x43}, std::uint8_t{0x44}, std::uint8_t{0x45}})
    {
        for (const Case& tried : {Case{0x1c16, 0x52}, Case{0x1c14, 0x40}, Case{0x1c15, 0x41}, Case{0x1c37, 0x5d},
                                  Case{0x1c13, DefaultActionOf(skill)}})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(tried.mount);
            const auto outside = AHero(skill);
            outside->Hero().pet.model = tried.mount;
            ASSERT_TRUE(outside->Use().HasValue());
            EXPECT_EQ(outside->Hero().animation.action, tried.action);
            EXPECT_EQ(outside->Hero().movement, 0);
            const auto inside = AHero(skill, 2);
            inside->Hero().pet.model = tried.mount;
            inside->Hero().safeZone = 1;
            ASSERT_TRUE(inside->Use().HasValue());
            EXPECT_EQ(inside->Hero().animation.action, DefaultActionOf(skill));
        }
    }
}

// A hero (of classes 5 to 7, no branch: 0x5897aa..0x5897b1) that uses the skill from 0x57cfc0 with the right button.
std::unique_ptr<TailHero> AHeroClicking(std::uint8_t skill, std::int32_t map, std::uint8_t heroClass = 6)
{
    auto tested = AHero(skill, map, heroClass);
    tested->Hero().attackPosition = {};
    tested->Movement().weaponSkill.target = -1;
    tested->world.Runtime().ui.rightClick = true;
    return tested;
}

// From 0x57cfc0: 0x57ce80's target (+0x3a8 at the chosen one) reaches the tail; 0x7758588 is set by the hand skill of
// classes 1, 3 and 4 (0x57d582..0x57d61b), and classes 5 to 7 send the −1 left there: the key 0xffff (Q-C1228).
// Outside the safe zone in Lorencia, Devias and the siege (30, where 0x5a5540 does not check these skills); inside it,
// only in Blood Castle (11 to 17) and Chaos Castle (18 to 23), and the mount does not count.
TEST(HeroSkillsTailTest, TheHeroSkillReachesTheTailInEveryMap)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x43, map);
        const CrtRandom expected = Advanced(tested->random, AimDraws + SternDraws);
        const auto result = Client::Skills::UseTheHeroSkill(tested->Hero(), tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 182, 115, 0x60, 0xaa, 0xaa, 0xff, 0xff, 0}));
        EXPECT_EQ(tested->Hero().animation.action, StandingAction);
    }
    {
        // The knight: each hand's skill (the own record twice) sets 0x7758588 to the chosen one, and its branch without
        // a right-hand weapon (the cost and the close) ends at 0x5808da.
        const auto tested = AHeroClicking(0x43, 2, 1);
        const CrtRandom expected =
            Advanced(tested->random, AimDraws + 2 * OwnDraws + EntryDraws + OwnDraws + SternDraws);
        const auto result = Client::Skills::UseTheHeroSkill(tested->Hero(), tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x43, 182, 115, 0x60, 0xaa, 0xaa, 0x0a, 0x1b, 0}));
    }
    for (const std::int32_t map : {11, 18})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x47, map);
        tested->Hero().safeZone = 1;
        tested->Hero().pet.model = 0x1c37;
        const auto result = Client::Skills::UseTheHeroSkill(tested->Hero(), tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x47, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, CancelAction);
    }
    // In Lorencia's safe zone, 0x57d33d lets nothing through: not even the tail.
    const auto tested = AHeroClicking(0x47, 0);
    tested->Hero().safeZone = 1;
    ASSERT_TRUE(Client::Skills::UseTheHeroSkill(tested->Hero(), tested->Context()).HasValue());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Hero().animation.action, 1);
}
} // namespace
