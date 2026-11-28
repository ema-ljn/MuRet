#include "Network/Xor32Packet.h"
#include "Skills/HeroSkillsWizard.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Pathfinding/Path.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill55cbc0.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Math/Ftol.h"
#include "Terrain/WorldTerrain.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The Wizard branch of 0x57cfc0 (0x5897aa..0x599d5d) and 0x55cbc0, called directly, with an invented Skill.bmd.
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// The head (0x5897b7..0x58a08a) without a delay: the own record and, with it open, the cost, the energy and the delay.
constexpr int HeadDraws = OwnDraws + 3 * EntryDraws;
// The hero's logical cell: the Y and the X.
constexpr int HeroCellDraws = 2 * CellDraws;
// 0x4f5290 on a player without a mount: 0x4f3f80 (the own record) and the action's rand().
constexpr int HitDraws = OwnDraws + 1;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint32_t Ticks = 100000;

enum HeroClass : std::uint8_t
{
    Wizard = 0,
    Knight = 1,
    Elf = 2,
    Gladiator = 3,
    Lord = 4,
};

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

struct WizardHero
{
    explicit WizardHero(std::uint8_t used, std::uint8_t heroClass = Wizard, std::int32_t map = 0) : skill(used)
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
    // The ally is named `name` and the party (0x77f3cd0, with 0x78aba48) has these names.
    void JoinTheParty(std::string_view name, std::initializer_list<std::string_view> members)
    {
        const auto write = [](std::span<std::byte> bytes, std::string_view text)
        {
            std::ranges::fill(bytes, std::byte{0});
            for (std::size_t at = 0; at < text.size(); ++at)
                bytes[at] = static_cast<std::byte>(text[at]);
        };
        write(Chosen().name, name);
        auto& party = world.Runtime().social.party;
        std::size_t at = 0;
        for (const std::string_view member : members)
            write(party.slots[at++].name, member);
        world.Runtime().hud.partyCount = static_cast<std::int32_t>(members.size());
    }
    // Without a chosen one, 0x57ce80 aims at the terrain, at (186, 119).
    void AimAtTheTerrain()
    {
        world.Runtime().selectedTargets[2] = -1;
        terrainPoint = glm::vec3{18650.0f, 11950.0f, 0.0f};
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
        if (!withTicks)
            context.tickCount = {};
        return context;
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return Client::Skills::UseTheWizardSkill(Hero(), skill, Context());
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
    bool withTicks = true;
    std::optional<glm::vec3> terrainPoint;
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Tables::SkillTable skills =
        SkillsWith({{1},    {5},    {6},    {8},    {9},    {0xa},  {0xc},  {0xd},  {0xe},  {0xf},
                    {0x10}, {0x13}, {0x26}, {0x27}, {0x28}, {0x2f}, {0x3a}, {0x43}, {0x49}, {0x4c}});
};

std::unique_ptr<WizardHero> AWizard(std::uint8_t skill, std::uint8_t heroClass = Wizard, std::int32_t map = 0)
{
    return std::make_unique<WizardHero>(skill, heroClass, map);
}

// 0x5897a7..0x5897b1: a class other than 0 and 3 goes straight to 0x5808da, without rand(); there, outside 0x43 to
// 0x48, nothing, and with them the tail (HeroSkillsTail).
TEST(HeroSkillsWizardTest, TheOtherClassesGoStraightToTheSharedTail)
{
    for (const std::uint8_t heroClass : {Knight, Elf, Lord})
    {
        SCOPED_TRACE(heroClass);
        {
            const auto tested = AWizard(0x13, heroClass);
            const CrtRandom expected = tested->random;
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_EQ(tested->Movement().targetX, 0u);
        }
        const auto tested = AWizard(0x44, heroClass);
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x44, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, 0x87);
    }
}

// 0x589885..0x5898a5: 0x551f40's energy (level · energy · 4 / 100 + 20) against the record's +0x1e + +0x30.
TEST(HeroSkillsWizardTest, TheEnergyStopsTheHead)
{
    const auto tested = AWizard(0x13);
    tested->skills = SkillsWith({{0x13, 0, 0, 10, 5}});
    const CrtRandom expected = Advanced(tested->random, OwnDraws + 2 * EntryDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().targetX, 0u);

    // 10 · 5 · 4 / 100 + 20 = 22.
    tested->world.OwnCharacter().energy = 12;
    tested->world.OwnCharacter().word30 = 10;
    const CrtRandom passed = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), passed.State());
    EXPECT_EQ(tested->Movement().targetX, 184u);
    EXPECT_EQ(tested->Movement().targetY, 117u);
}

// 0x58990f..0x589936: without the mana, the mana potion (without it, nothing).
TEST(HeroSkillsWizardTest, WithoutManaTheHeadLooksForAPotion)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x13, Wizard, map);
        tested->skills = SkillsWith({{0x13, 5}});
        const CrtRandom expected = Advanced(tested->random, OwnDraws + 2 * EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 0u);
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        const auto potionExpected = Advanced(tested->random, OwnDraws + 2 * EntryDraws + 3);
        const auto again = tested->Use();
        ASSERT_TRUE(again.HasValue()) << again.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 6, 0x26, 24, 0, 0}));
        EXPECT_EQ(tested->random.State(), potionExpected.State());
        EXPECT_EQ(tested->world.Runtime().ui.field5767954, 10);
        tested->world.OwnCharacter().mana = 5;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->Movement().targetX, 184u);
    }
}

// 0x58a036..0x58a06f: without the stamina, nothing; 0x3a also lets go of the right button. 0x28 needs no stamina, and
// 0x3a costs what 0x28 does.
TEST(HeroSkillsWizardTest, WithoutStaminaTheChargeLetsTheRightButtonGo)
{
    for (const std::uint8_t skill : {std::uint8_t{0x3a}, std::uint8_t{0x13}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AWizard(skill);
        tested->skills = SkillsWith({{0x28, 0, 5}, {0x13, 0, 5}, {0x3a}});
        auto& ui = tested->world.Runtime().ui;
        ui.rightReleased = ui.rightClick = ui.rightDown = true;
        ui.rightHoldFrames = 3;
        const CrtRandom expected = Advanced(tested->random, OwnDraws + 2 * EntryDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        const bool charged = skill == 0x3a;
        EXPECT_EQ(ui.rightReleased, !charged);
        EXPECT_EQ(ui.rightClick, !charged);
        EXPECT_EQ(ui.rightDown, !charged);
        EXPECT_EQ(ui.rightHoldFrames, charged ? 0 : 3);
    }
    const auto tested = AWizard(0x28);
    tested->skills = SkillsWith({{0x28, 0, 5}});
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 1u);
}

// 0x58a071..0x58a08a: with the slot's delay running (0x552c50), nothing; without it, it sets it.
TEST(HeroSkillsWizardTest, ADelayedSlotStopsTheHead)
{
    const auto tested = AWizard(0x13);
    tested->skills = SkillsWith({{0x13, 0, 0, 0, 0, 1000}});
    tested->world.OwnCharacter().skillDelays[0] = 500;
    const CrtRandom expected = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().targetX, 0u);

    tested->world.OwnCharacter().skillDelays[0] = 0;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->world.OwnCharacter().skillDelays[0], 1000);
    EXPECT_EQ(tested->Movement().targetX, 184u);
}

// 0x58a141..0x58a899: the released 0x3a sends the 19 with the player's key (the 300 ms brake), and always the speeds,
// action 0x44 and +0x378 at 0. Only a player; the Magic Gladiator too.
TEST(HeroSkillsWizardTest, TheReleasedChargeSendsTheMagicAttackWithThePlayerKey)
{
    for (const std::uint8_t heroClass : {Wizard, Gladiator})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(heroClass);
            SCOPED_TRACE(map);
            const auto tested = AWizard(0x3a, heroClass, map);
            tested->Hero().movement = 1;
            const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + OwnDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x3a, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 0x44);
            EXPECT_EQ(tested->Hero().movement, 0);
            EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
        }
    }
    {
        // With the brake, no 19.
        const auto tested = AWizard(0x3a);
        tested->Movement().lastActionTick = Ticks - 300;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 0x44);
    }
    {
        // With "webzen" in the published hero's name, neither the 19 nor the brake.
        const auto tested = AWizard(0x3a);
        const std::string name = "awebzen";
        for (std::size_t i = 0; i < name.size(); ++i)
            tested->Hero().name[i] = static_cast<std::byte>(name[i]);
        const CrtRandom expected = Advanced(tested->random, HeadDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().lastActionTick, 0u);
    }
    {
        // Another type: nothing after the head.
        const auto tested = AWizard(0x3a);
        tested->Hero().type = 0x20;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
    {
        // Without GetTickCount, the brake cuts.
        const auto tested = AWizard(0x3a);
        tested->withTicks = false;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x58a1be")) << result.Error();
    }
}

// 0x58a89a..0x58b025: 0x28 sends the 19 with 0x558eb0's key or, without it, the player's; the brake does not hold it.
TEST(HeroSkillsWizardTest, TheChargedShotUsesTheChosenKeyOrThePlayerKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x28, Wizard, map);
        tested->Movement().lastActionTick = Ticks - 10;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x28, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().animation.action, 0x45);
        EXPECT_EQ(tested->Movement().lastActionTick, Ticks);
    }
    const auto tested = AWizard(0x28);
    tested->Chosen().byte370 = 0;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x28, 0x12, 0x34}));
}

// 0x58c1b6.. (Hellfire, 0xa) and 0x58cf37.. (Inferno, 0xe): the 1E with the hero's cell, its heading and the key
// 0xffff; then the speeds, the action (0x96 and 0x95) and +0x378 at 0. Only a player.
TEST(HeroSkillsWizardTest, HellfireAndInfernoSendTheirCellAndDirection)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint8_t action;
    };
    for (const Case& tried : {Case{0xa, 0x96}, Case{0xe, 0x95}})
    {
        for (const std::uint8_t heroClass : {Wizard, Gladiator})
        {
            for (const std::int32_t map : {0, 2})
            {
                SCOPED_TRACE(tried.skill);
                SCOPED_TRACE(heroClass);
                SCOPED_TRACE(map);
                const auto tested = AWizard(tried.skill, heroClass, map);
                tested->Hero().angles.z = 90.0f;
                tested->Hero().movement = 1;
                const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + SendDraws + OwnDraws);
                ASSERT_TRUE(tested->Use().HasValue());
                EXPECT_EQ(tested->random.State(), expected.State());
                ASSERT_EQ(tested->sent.size(), 1u);
                EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
                EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, tried.skill, 182, 115, 0x40, 0,
                                                                      0, 0xff, 0xff, 0}));
                EXPECT_EQ(tested->world.Runtime().lastShotSkill, std::uint32_t{tried.skill});
                EXPECT_EQ(tested->Hero().animation.action, tried.action);
                EXPECT_EQ(tested->Hero().movement, 0);
            }
        }
    }
    const auto tested = AWizard(0xa);
    tested->Hero().type = 0x20;
    const CrtRandom expected = Advanced(tested->random, HeadDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
}

// 0x58c15e..0x58eecc: in reach and with an attackable chosen one, the weapon skill and 0x55cbc0; 8 and 9 send the 1E
// with the hero's cell and the magic key (0x77f3bad, which wraps from 0x32 to 1, in +0x1bd), and 0x4f5290.
TEST(HeroSkillsWizardTest, TheMagicKeySkillsCountTheirKey)
{
    for (const std::uint8_t skill : {std::uint8_t{8}, std::uint8_t{9}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AWizard(skill, Wizard, map);
            const CrtRandom expected =
                Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + HeroCellDraws + SendDraws + HitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            const auto& weapon = tested->Movement().weaponSkill;
            EXPECT_EQ(weapon.active, 1u);
            EXPECT_EQ(weapon.slot, 0u);
            EXPECT_EQ(weapon.target, static_cast<std::int32_t>(ChosenSlot));
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0),
                      (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, skill, 182, 115, 0x60, 0, 0, 0xff, 0xff, 1}));
            EXPECT_EQ(tested->Hero().magicKey, 1);
            const std::uint8_t action = tested->Hero().animation.action;
            EXPECT_TRUE(action == 0x8e || action == 0x8f) << int{action};
        }
    }
    const auto tested = AWizard(8);
    tested->Movement().magicKeyCounter = 0x32;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[11], 1);
    EXPECT_EQ(tested->Movement().magicKeyCounter, 1);
}

// 0x58fced..0x590b17: Aqua Beam like Hellfire, with action 0x94 and 0x77f3ba4 at 0.
TEST(HeroSkillsWizardTest, AquaBeamCastsAroundTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0xc, Wizard, map);
        tested->Movement().dword77f3ba4 = 7;
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + HeroCellDraws + SendDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0xc, 182, 115, 0x60, 0, 0, 0xff, 0xff, 0}));
        EXPECT_EQ(tested->Hero().animation.action, 0x94);
        EXPECT_EQ(tested->Movement().dword77f3ba4, 0);
    }
}

// 0x590b18..0x591858 (Flame, 5) and 0x591859..0x5925ad (0x26 and 0x27): the 1E with the cell of +0x3a8, which
// 0x55cbc0 left at the chosen one's; 0x26 and 0x27 with the key of the weapon skill's target.
TEST(HeroSkillsWizardTest, FlameAndTheWeaponTargetSkillsCastAtTheTargetCell)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint8_t keyHigh;
        std::uint8_t keyLow;
    };
    for (const Case& tried : {Case{5, 0xff, 0xff}, Case{0x26, 0x0a, 0x1b}, Case{0x27, 0x0a, 0x1b}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(tried.skill);
            SCOPED_TRACE(map);
            const auto tested = AWizard(tried.skill, Wizard, map);
            tested->Hero().movement = 1;
            const CrtRandom expected =
                Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + SendDraws + HitDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, tried.skill, 184, 117, 0x60, 0, 0,
                                                                  tried.keyHigh, tried.keyLow, 0}));
            EXPECT_EQ(tested->world.Runtime().lastShotSkill, std::uint32_t{tried.skill});
            EXPECT_EQ(tested->Hero().movement, tried.skill == 5 ? 1 : 0);
        }
    }
}

// 0x58dcc4..0x58eca3: 0x4c in reach sends the 1E with the relative position (0x4e9920), the chosen one's key and the
// magic key, after the delay (0x552990); +0x434 stays at the chosen one and the action is the hands' (0x4e9c50).
TEST(HeroSkillsWizardTest, TheSkill4cCastsWithThePositionAndTheMagicKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x4c, Wizard, map);
        tested->Hero().movement = 1;
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + OwnDraws + EntryDraws + HeroCellDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x4c, 182, 115, 0x60, 0, 0xaa, 0x0a, 0x1b, 1}));
        EXPECT_EQ(tested->Hero().skillTarget4c, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Hero().animation.action, 0x62);
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    }
    {
        // Out of reach, the path up to the reach · 1.2, without +0x379.
        const auto tested = AWizard(0x4c);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 0);
        EXPECT_GT(tested->Hero().path.count, 0);
    }
    {
        // Without a chosen one and out of reach, 0x6c8e38 at −1.
        const auto tested = AWizard(0x4c);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 1.0f;
        tested->world.Runtime().selectedTargets[4] = 2;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// 0x58ecff..0x58edff: out of reach with an attackable chosen one, the path up to the reach with +0x379 at 5; except
// for 0x43 to 0x47 and 0x4c, nothing more.
TEST(HeroSkillsWizardTest, OutOfReachTheSkillWalksToTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(1, Wizard, map);
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 5);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
    }
    {
        // A chosen one that cannot be attacked: neither a path nor a weapon skill target.
        const auto tested = AWizard(1);
        tested->ChooseAnAlly();
        tested->Movement().selectedSkillRange = 1.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
    }
    {
        // 0x48 walks and ends there.
        const auto tested = AWizard(0x48, Wizard, 2);
        tested->Movement().selectedSkillRange = 1.0f;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
    }
    // 0x43 goes on out of reach, after the path, to the tail (0x5808da, HeroSkillsTail): the 1E and +0x378 at 0, which
    // leaves the path built but not walked.
    const auto tested = AWizard(0x43);
    tested->Movement().selectedSkillRange = 1.0f;
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_FALSE(tested->sent.empty());
    const std::vector<std::uint8_t> last = tested->Sent(tested->sent.size() - 1);
    EXPECT_EQ(last[2], 0x1e);
    EXPECT_EQ(last[3], 0x43);
    EXPECT_EQ(tested->Hero().movement, 0);
    EXPECT_EQ(tested->Hero().movementType, 5);
    EXPECT_GT(tested->Hero().path.count, 0);
}

// P-C256: out of reach, a chosen one with its cell outside 0..255 (which PathFinding2 takes as a linear index) gets no
// path: after the reads of the hero's cell, the path is discarded without cutting, for 0x4c (0x58ec65) and for the
// rest (0x58edc6). The X left of the map or the Y above 255.
TEST(HeroSkillsWizardTest, AWalkToACellOffTheGridIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x4c}, std::uint8_t{1}})
        {
            for (const glm::vec3 position : {glm::vec3{-1000.0f, 11750.0f, 0.0f}, glm::vec3{18450.0f, 30000.0f, 0.0f}})
            {
                SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill) + " " + std::to_string(position.x));
                const auto tested = AWizard(skill, Wizard, map);
                tested->Chosen().position = position;
                const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
                const auto result = tested->Use();
                ASSERT_TRUE(result.HasValue()) << result.Error();
                EXPECT_EQ(tested->random.State(), expected.State());
                EXPECT_TRUE(tested->sent.empty());
                EXPECT_EQ(tested->Hero().movement, 0);
                EXPECT_EQ(tested->Hero().movementType, 0);
                EXPECT_EQ(tested->Hero().path.count, 0);
            }
        }
    }
    // 0x43 goes on to the line of sight (0x58eea8), which with the Y at 300 falls outside TerrainWall (0x55886d): MuRet
    // takes it as no line and the skill goes on without cutting.
    const auto tested = AWizard(0x43, Wizard, 2);
    tested->Chosen().position = glm::vec3{18450.0f, 30000.0f, 0.0f};
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->Hero().path.count, 0);
}

// 0x55cbc0 from 0x58eecc: 1 (on the 19's list) hits the chosen one with the 19, 0x4f5290 and the turn D3 with the
// published hero's logical cell; then the branch goes on to 0x5808da, which does nothing with 1.
TEST(HeroSkillsWizardTest, InReachTheChosenTakesTheWeaponSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(1, Wizard, map);
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x01, 0x0a, 0x1b}));
        EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Clear);
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x40}));
        EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
    }
    // With the brake, only the D3.
    const auto tested = AWizard(1);
    tested->Movement().lastActionTick = Ticks;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[2], 0xd3);
}

// 0x55e67e..0x55f7dd: 0xd sends the 1E with p1's +0x3a8 cell, p2's heading and the target's key.
TEST(HeroSkillsWizardTest, InReachTheSkill0dSendsItsDurationAtTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0xd, Wizard, map);
        const CrtRandom expected =
            Advanced(tested->random, HeadDraws + HeroCellDraws + OwnDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0xd, 184, 117, 0x60, 0, 0, 0x0a, 0x1b, 0}));
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x40}));
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0xdu);
    }
}

// 0x55cc9a..0x55cccc and 0x55da5b..: 0x49 needs the published hero's right hand from 0xc12 to 0xe11; with it, the 19,
// action 0x86 and the D3.
TEST(HeroSkillsWizardTest, TheThrustNeedsItsWeapon)
{
    for (const auto& [map, right] : {std::pair{0, std::uint16_t{0xc12}}, std::pair{2, std::uint16_t{0xc12}},
                                     std::pair{0, std::uint16_t{0xe11}}, std::pair{2, std::uint16_t{0xe12}}})
    {
        SCOPED_TRACE(std::to_string(map) + " " + std::to_string(right));
        const auto tested = AWizard(0x49, Wizard, map);
        tested->Hero().hands[0].model = right;
        tested->Movement().weaponSkill = {.active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
        const bool thrusts = right != 0xe12;
        const CrtRandom expected =
            Advanced(tested->random, OwnDraws + (thrusts ? SendDraws + OwnDraws + HeroCellDraws : 0));
        ASSERT_TRUE(
            Client::Skills::UseTheSkillOnItsTarget(tested->Hero(), tested->Hero(), tested->Context()).HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), thrusts ? 2u : 0u);
        if (thrusts)
        {
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x49, 0x0a, 0x1b}));
            EXPECT_EQ(tested->Hero().animation.action, 0x86);
            EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Clear);
            EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x40}));
        }
    }
}

// 0x55cbc0(p1, p2) with two different characters (the branch passes the same one twice): p1 (slot 8) takes the
// target's position in +0x3a8, 0x4f5290, the 1E's cell and the key through 0x558eb0(p1, target), which gives −1
// (0xffff) for a p1 that is not the published hero; p2 (slot 9) turns toward the target, gives the 1E's heading and
// does 0x49's 0x86. The D3 comes from the published hero (slot 0), which does not turn (angle 0: 0x10). With "webzen"
// in the published hero's name, the 19 and the 1E do not go out (nor +0x1bd for 0xd), but 0x4f5290 or the 0x86 and
// the D3 do (0x55cd47, 0x55d9da, 0x55e69c). 0xd does not check the 300 ms brake (0x55e70f does not read
// GetTickCount): it goes out with +0x3a8 just set.
TEST(HeroSkillsWizardTest, TheSkillOnItsTargetSplitsTheUserAndTheTurned)
{
    constexpr std::size_t UserSlot = 8;
    constexpr std::size_t TurnedSlot = 9;
    const auto direction = [](float angle)
    {
        const double scale = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f360b61}));
        const auto truncated = Client::Math::OriginalFtolLow(static_cast<double>(angle) * scale);
        return static_cast<std::uint8_t>(truncated.value_or(-1) & 0xff);
    };
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{1}, std::uint8_t{0x49}, std::uint8_t{0xd}})
        {
            for (const bool silent : {false, true})
            {
                SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill) + (silent ? " webzen" : ""));
                const auto tested = AWizard(skill, Wizard, map);
                tested->Hero().hands[0].model = 0xc12;
                if (silent)
                {
                    const std::string name = "webzen";
                    for (std::size_t i = 0; i < name.size(); ++i)
                        tested->Hero().name[i] = static_cast<std::byte>(name[i]);
                }
                auto& user = tested->world.Actors()[UserSlot];
                auto& turned = tested->world.Actors()[TurnedSlot];
                for (Client::Kernel::ActorState* actor : {&user, &turned})
                {
                    actor->alive = true;
                    actor->type = Client::Tables::PlayerSkeletonModel;
                    actor->hands[0].model = actor->hands[1].model = actor->wings.model = actor->pet.model =
                        Client::Kernel::NoItemModel;
                    actor->animation.action = 1;
                }
                user.position = {18250.0f, 11550.0f, 0.0f};
                turned.position = {18050.0f, 11950.0f, 0.0f};
                turned.angles.z = 7.0f;
                tested->Movement().weaponSkill = {
                    .active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
                // 0xd does not brake: with 0x1 or 0x49 this last use would brake them.
                tested->Movement().lastActionTick = skill == 0xd ? Ticks : 0;
                const bool sends = !silent;
                const int hit = skill == 0x49 ? OwnDraws : HitDraws;
                const CrtRandom expected =
                    Advanced(tested->random, OwnDraws + (sends ? SendDraws : 0) + hit + HeroCellDraws);
                const auto used = Client::Skills::UseTheSkillOnItsTarget(user, turned, tested->Context());
                ASSERT_TRUE(used.HasValue()) << used.Error();
                EXPECT_EQ(tested->random.State(), expected.State());

                EXPECT_EQ(user.attackPosition, tested->Chosen().position);
                EXPECT_EQ(turned.attackPosition, glm::vec3(0.0f));
                const auto toward = static_cast<float>(Client::Math::CreateAngle(
                    18050.0f, 11950.0f, tested->Chosen().position.x, tested->Chosen().position.y));
                EXPECT_EQ(turned.angles.z, toward);
                EXPECT_EQ(user.angles.z, 0.0f);
                EXPECT_EQ(tested->Hero().angles.z, 0.0f);
                if (skill == 0x49)
                {
                    EXPECT_EQ(turned.animation.action, 0x86);
                    EXPECT_EQ(user.animation.action, 1);
                }
                else
                {
                    EXPECT_GE(user.animation.action, 0x8e);
                    EXPECT_LE(user.animation.action, 0x8f);
                    EXPECT_EQ(turned.animation.action, 1);
                }
                EXPECT_EQ(tested->world.Runtime().lastShotSkill, skill == 0xd && sends ? 0xdu : 0u);

                ASSERT_EQ(tested->sent.size(), sends ? 2u : 1u);
                if (sends && skill == 0xd)
                {
                    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0xd, 184, 117,
                                                                          direction(toward), 0, 0, 0xff, 0xff, 0}));
                }
                else if (sends)
                {
                    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x0a, 0x1b}));
                }
                const std::size_t turn = tested->sent.size() - 1;
                EXPECT_EQ(tested->sent[turn].delivery, Client::Network::Delivery::Clear);
                EXPECT_EQ(tested->Sent(turn), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
            }
        }
    }
}

// 0x593051..0x593873: Teleport sends the 1C to the target's cell and sets 0x576794c and action 0x93, except with the
// cursor, 0x1000, the action, +0x80, the alpha, the wall (neither 0x20 nor 0x40), 0x576794c, 0x77f3bb0 or 3 s since
// the last one.
TEST(HeroSkillsWizardTest, TeleportSendsTheTargetCell)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(6, Wizard, map);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 6.0f;
        tested->terrain.attributes->cells[(119u << 8) + 186u] = 0x60;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x1c, 0x00, 186, 119}));
        EXPECT_EQ(tested->Movement().gateRequestBlocked, 1);
        EXPECT_EQ(tested->Hero().animation.action, 0x93);
    }
    struct Case
    {
        const char* what;
        void (*prepare)(WizardHero&);
    };
    const Case cases[] = {
        {"cursor", [](WizardHero& hero) { hero.world.Runtime().cursorItemCount = 1; }},
        {"0x1000", [](WizardHero& hero) { hero.Hero().flags78 = 0x1000; }},
        {"action", [](WizardHero& hero) { hero.Hero().animation.action = 0x93; }},
        {"0x80", [](WizardHero& hero) { hero.Hero().byte80 = 2; }},
        {"alpha", [](WizardHero& hero) { hero.Hero().alpha = 0.5f; }},
        {"wall", [](WizardHero& hero) { hero.terrain.attributes->cells[(119u << 8) + 186u] = 4; }},
        {"gate", [](WizardHero& hero) { hero.Movement().gateRequestBlocked = 1; }},
        {"warp", [](WizardHero& hero) { hero.Movement().warpPending = true; }},
        {"wait", [](WizardHero& hero) { hero.Movement().lastTeleportMs = Ticks - 2999; }},
        {"gladiator", [](WizardHero& hero) { hero.Hero().characterClass = Gladiator; }},
    };
    for (const Case& tried : cases)
    {
        SCOPED_TRACE(tried.what);
        const auto tested = AWizard(6);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 6.0f;
        tried.prepare(*tested);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        if (std::string(tried.what) != "action")
        {
            EXPECT_NE(tested->Hero().animation.action, 0x93);
        }
    }
    const auto tested = AWizard(6);
    tested->AimAtTheTerrain();
    tested->Movement().selectedSkillRange = 6.0f;
    tested->withTicks = false;
    const auto result = tested->Use();
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x59313a")) << result.Error();
}

// The neighbor cell that Teleport Ally picks (0x5927cb..0x5928f1) with the rand() of `random`: two rand() % 3 per
// round, skipping the center, and on map 30 the published hero's X and Y (8 rand()) for each cell that is not the
// center.
struct AllyCell
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    int draws = 0;
};
AllyCell FirstAllyCell(CrtRandom random)
{
    AllyCell cell;
    for (;;)
    {
        const int dx = random.Next() % 3;
        const int dy = random.Next() % 3;
        cell.draws += 2;
        if (dx == 1 && dy == 1)
            continue;
        cell.draws += HeroCellDraws;
        cell.x = 182 + dx - 1;
        cell.y = 115 + dy - 1;
        return cell;
    }
}

// 0x5925d9..0x593050: Teleport Ally. Without the siege, only a party member (0x5926ca..0x592766, 0x59272c's strncmp: a
// slot that starts with its name); in the siege (map 30), a neighbor cell without a wall, or any inside the castle, and
// the B0 with the chosen one's key in little endian; the chosen one faces its +0x3a8 and takes action 0x93.
TEST(HeroSkillsWizardTest, TeleportAllyMovesTheChosenNextToTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        for (const bool partied : {false, true})
        {
            SCOPED_TRACE(partied);
            const auto tested = AWizard(0xf, Wizard, map);
            tested->ChooseAnAlly();
            if (partied)
                tested->JoinTheParty("aliado", {"otro", "aliad"});
            const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
        }
        // Outside map 30 the neighbor cell does not read the published hero's: two rand() % 3 per round.
        const auto tested = AWizard(0xf, Wizard, map);
        tested->ChooseAnAlly();
        tested->JoinTheParty("aliado", {"otro", "aliado2"});
        CrtRandom probe = Advanced(tested->random, HeadDraws + HeroCellDraws);
        int dx = 1;
        int dy = 1;
        while (dx == 1 && dy == 1)
        {
            dx = probe.Next() % 3;
            dy = probe.Next() % 3;
        }
        const CrtRandom expected = Advanced(probe, SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x07, 0xb0, 0x1b, 0x0a, static_cast<std::uint8_t>(182 + dx - 1),
                                             static_cast<std::uint8_t>(115 + dy - 1)}));
        EXPECT_EQ(tested->Chosen().animation.action, 0x93);
    }
    for (const bool castle : {true, false})
    {
        SCOPED_TRACE(castle);
        const auto tested = AWizard(0xf, Wizard, 30);
        tested->world.Runtime().castleSiege.siegeActive = true;
        tested->ChooseAnAlly();
        if (castle)
            tested->Hero().cellY = 0xc0;
        const CrtRandom before = Advanced(tested->random, HeadDraws + HeroCellDraws);
        const AllyCell cell = FirstAllyCell(before);
        const CrtRandom expected = Advanced(before, cell.draws + SendDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0),
                  (std::vector<std::uint8_t>{0xc1, 0x07, 0xb0, 0x1b, 0x0a, static_cast<std::uint8_t>(cell.x),
                                             static_cast<std::uint8_t>(cell.y)}));
        EXPECT_EQ(tested->Movement().targetX, static_cast<std::uint32_t>(cell.x));
        EXPECT_EQ(tested->Movement().targetY, static_cast<std::uint32_t>(cell.y));
        EXPECT_EQ(tested->Movement().gateRequestBlocked, 1);
        EXPECT_EQ(tested->Chosen().animation.action, 0x93);
    }
    {
        // With 0x576794c, it clears it and nothing else.
        const auto tested = AWizard(0xf, Wizard, 30);
        tested->world.Runtime().castleSiege.siegeActive = true;
        tested->ChooseAnAlly();
        tested->Movement().gateRequestBlocked = 1;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().gateRequestBlocked, 0);
    }
    {
        // With the eight neighbor cells blocked outside the castle, nothing after 11 blocked cells.
        const auto tested = AWizard(0xf, Wizard, 30);
        tested->world.Runtime().castleSiege.siegeActive = true;
        tested->ChooseAnAlly();
        auto& cells = tested->terrain.attributes->cells;
        for (std::uint32_t y = 114; y <= 116; ++y)
            for (std::uint32_t x = 181; x <= 183; ++x)
                cells[(y << 8) + x] = 4;
        CrtRandom probe = Advanced(tested->random, HeadDraws + HeroCellDraws);
        int walls = 0;
        while (walls <= 10)
        {
            const int dx = probe.Next() % 3;
            const int dy = probe.Next() % 3;
            if (dx == 1 && dy == 1)
                continue;
            for (int i = 0; i < HeroCellDraws; ++i)
                static_cast<void>(probe.Next());
            ++walls;
        }
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), probe.State());
        EXPECT_TRUE(tested->sent.empty());
    }
}

// 0x58b026..0x58c15b: Soul Barrier. Without a chosen one, the 19 with the player's key, 0x4f5290 and it goes on
// through the reach; with a chosen one that is not a player, 0x6c8e38 at −1; with a player outside the party
// (0x59e680), nothing.
TEST(HeroSkillsWizardTest, SoulBarrierWithoutAChosenGoesOnTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x10, Wizard, map);
        tested->AimAtTheTerrain();
        tested->Movement().selectedSkillRange = 6.0f;
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x10, 0x12, 0x34}));
    }
    {
        const auto tested = AWizard(0x10);
        tested->world.Runtime().selectedTargets[4] = 2;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_TRUE(tested->sent.empty());
    }
    for (const bool partied : {false, true})
    {
        SCOPED_TRACE(partied);
        const auto tested = AWizard(0x10);
        tested->ChooseAnAlly();
        if (partied)
            tested->JoinTheParty("aliado", {"otro", "aliad"});
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    }
}

// 0x58b07e..0x58b50d: Soul Barrier on a party member: +0x3a4 and 0x7758580..88 with the chosen one. In reach, the 19
// with its key, 0x4f5290 and it goes on through the reach (which writes 0x7758588 again: the ally is not attacked);
// out of it, the path up to the reach from the hero's cell (0x58b1d1: +0x378 at 1 and +0x379 at 5) and nothing more.
TEST(HeroSkillsWizardTest, SoulBarrierOnAPartyMember)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AWizard(0x10, Wizard, map);
        tested->ChooseAnAlly();
        tested->JoinTheParty("aliado", {"otro", "aliado"});
        const CrtRandom expected = Advanced(tested->random, HeadDraws + SendDraws + HitDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x10, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().attackTarget, ChosenSlot);
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
    }
    const auto tested = AWizard(0x10);
    tested->ChooseAnAlly();
    tested->JoinTheParty("aliado", {"aliado"});
    tested->Movement().selectedSkillRange = 2.0f;
    const CrtRandom expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Hero().attackTarget, ChosenSlot);
    EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
    EXPECT_EQ(tested->Movement().weaponSkill.slot, 0u);
    EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
    EXPECT_EQ(tested->Hero().movement, 1);
    EXPECT_EQ(tested->Hero().movementType, 5);
}

// 0x58a13a: 0x12 to 0x17 and 0x2f do nothing after aiming.
TEST(HeroSkillsWizardTest, SomeSkillsDoNothingAfterAiming)
{
    for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x2f}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AWizard(skill);
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 184u);
        EXPECT_EQ(tested->Hero().attackPosition, tested->Chosen().position);
        EXPECT_TRUE(tested->sent.empty());
    }
}
} // namespace

// 0x58ecff..0x58edff out of reach on map 34, with Crywolf at stage 3 and an altar (0x206) in 0x6c8e54: PathFinding2
// sets the A* flag (0x4eac0a..0x4eac41), which does not count with a nonzero distance (0x4eadd4). The path, the
// rand(), +0x378 and +0x379 are the same as with another NPC (0x203) chosen.
TEST(HeroSkillsWizardTest, TheApproachWalksTheSameWithACrywolfAltar)
{
    const auto approach = [](std::uint16_t chosenType)
    {
        auto tested = AWizard(1, Wizard, 34);
        tested->Movement().selectedSkillRange = 1.0f;
        tested->world.Runtime().crywolf.stage = 3;
        tested->world.Runtime().chosenNpc.index = 30;
        tested->world.Actors()[30].type = chosenType;
        const auto expected = Advanced(tested->random, HeadDraws + HeroCellDraws);
        const auto used = tested->Use();
        EXPECT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        return tested;
    };
    const auto altar = approach(0x206);
    const auto other = approach(0x203);
    EXPECT_EQ(altar->Hero().movement, 1);
    EXPECT_EQ(altar->Hero().movementType, 5);
    ASSERT_GT(altar->Hero().path.count, 0);
    EXPECT_EQ(altar->Hero().path.count, other->Hero().path.count);
    for (std::size_t cell = 0; cell < altar->Hero().path.count; ++cell)
    {
        EXPECT_EQ(altar->Hero().path.cells[cell].x, other->Hero().path.cells[cell].x);
        EXPECT_EQ(altar->Hero().path.cells[cell].y, other->Hero().path.cells[cell].y);
    }
    EXPECT_TRUE(altar->sent.empty());
}
