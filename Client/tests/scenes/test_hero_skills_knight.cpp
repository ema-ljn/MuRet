#include "Network/Xor32Packet.h"
#include "Skills/HeroSkillsKnight.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/CharacterClass.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill4e9920.h"
#include "Skills/Skill4e9c50.h"
#include "Skills/Skill551f40.h"
#include "Skills/Skill57cf90.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The branch of classes 1, 3 and 4 of 0x57cfc0 (06 § Las skills del caballero), with an invented key and Skill.bmd.
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;
using Client::Skills::UseTheKnightSkill;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int C3Draws = 2;
// 0x551c70, 0x552c50 (its entry; the record is already open) and 0x551f40, and the close of the own record.
constexpr int PaidDraws = 3 * EntryDraws + OwnDraws;
// Without going through 0x552c50 (when it cannot be used).
constexpr int UnpaidDraws = 2 * EntryDraws + OwnDraws;
// The hero's two logical cells, the Y before the X.
constexpr int CellsDraws = 2 * CellDraws;
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0a1b;
constexpr std::uint16_t PlayerKey = 0x1234;
constexpr std::uint16_t Sword = 0x213;
constexpr std::uint8_t CellX = 0xb6; // 182
constexpr std::uint8_t CellY = 0x73; // 115

enum HeroClass : std::uint8_t
{
    Knight = 1,
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
    std::uint8_t range = 4;
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
    std::uint8_t level = 0;
    std::uint8_t energy = 0;
    std::uint16_t leadership = 0;
};

void PutWord(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8);
}

Client::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Tables::SkillRecordSize;
        plain[base + 0x20] = static_cast<std::byte>(invented.level);
        PutWord(plain, base + 0x22, invented.mana);
        PutWord(plain, base + 0x24, invented.stamina);
        plain[base + 0x26] = static_cast<std::byte>(invented.range);
        plain[base + 0x2c] = static_cast<std::byte>(invented.energy);
        PutWord(plain, base + 0x2e, invented.leadership);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

void Rename(Client::Kernel::ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t index = 0; index < name.size() && index + 1 < actor.name.size(); ++index)
        actor.name[index] = static_cast<std::byte>(name[index]);
}

// A player hero with a sword in the right hand that already aimed (0x57ce80) at the chosen one; the own record with
// 100 mana, stamina and energy.
struct KnightHero
{
    explicit KnightHero(std::uint8_t used, std::uint8_t heroClass = Knight, std::int32_t map = 0) : skill(used)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero = Client::Tests::UnarmedPlayer(heroClass);
        hero.alive = true;
        hero.cellX = CellX;
        hero.cellY = CellY;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = Sword;
        hero.animation.action = 1;
        hero.attackPosition = {18450.0f, 11750.0f, 0.0f};
        Rename(hero, "Arandu");
        world.OwnCharacter().SetSkillAt(0, skill);
        auto& own = world.OwnCharacter();
        own.mana = 100;
        own.bp = 100;
        own.energy = 100;
        auto& chosen = world.Actors()[ChosenSlot];
        chosen.alive = true;
        chosen.byte88 = 2;
        chosen.byte370 = 2;
        chosen.key = ChosenKey;
        chosen.position = {18450.0f, 11750.0f, 0.0f};
        auto& movement = Movement();
        movement.targetX = 184;
        movement.targetY = 117;
        movement.selectedSkillRange = 4.0f;
        movement.weaponSkill = {.active = 1, .slot = 0, .target = static_cast<std::int32_t>(ChosenSlot)};
        movement.lastActionTick = 0;
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Movement::GameMovementState& Movement()
    {
        return world.Runtime().movement;
    }
    HeroSkillContext Context()
    {
        return HeroSkillContext{.world = world,
                                .random = random,
                                .skills = &skills,
                                .playerSkeleton = &skeleton,
                                .tickCount = [this] { return ticks; },
                                .send = [this](Client::Protocol::Outgoing outgoing)
                                { sent.push_back(std::move(outgoing)); },
                                .playerKey = PlayerKey,
                                .terrain = &terrain};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return UseTheKnightSkill(Hero(), Client::Kernel::BaseClassOf(Hero().characterClass), skill, Context());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        const Client::Network::Packet clear = Client::Network::Xor32Decode(sent.at(index).packet);
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : clear.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        return bytes;
    }

    std::uint8_t skill;
    Client::Kernel::WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    std::uint32_t ticks = 10'000;
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Tables::SkillTable skills = SkillsWith({{.skill = 0x29},
                                                    {.skill = 0x2a},
                                                    {.skill = 0x30},
                                                    {.skill = 0x37},
                                                    {.skill = 0x38},
                                                    {.skill = 0x3e},
                                                    {.skill = 0x3f},
                                                    {.skill = 0x40},
                                                    {.skill = 0x41},
                                                    {.skill = 0x4b},
                                                    {.skill = 0x4c},
                                                    {.skill = 0x13},
                                                    {.skill = 0x31}});
    std::vector<Client::Protocol::Outgoing> sent;
};

std::unique_ptr<KnightHero> AKnight(std::uint8_t skill, std::uint8_t heroClass = Knight, std::int32_t map = 0)
{
    return std::make_unique<KnightHero>(skill, heroClass, map);
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

// The heading toward the target (0x4e93f0 in float) and its _ftol: ((+0x24 + `offset`) · `first`) · `second`, each
// product rounded to double as the x87 does with PC53.
std::uint8_t ExpectedDirection(double first, double second = 1.0, double offset = 0.0)
{
    const auto angle = static_cast<float>(Client::Math::CreateAngle(18250.0f, 11550.0f, 18450.0f, 11750.0f));
    const auto value = Client::Math::OriginalFtolLow((static_cast<double>(angle) + offset) * first * second);
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value.value_or(-1)));
}

const double TurnsOf256 = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f360b61}));
const double TurnFraction = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3b360b61}));

std::vector<std::uint8_t> DurationMagic(std::uint8_t skill, std::uint8_t direction, std::uint8_t byte7,
                                        std::uint8_t byte8, std::uint16_t key, std::uint8_t byte11 = 0)
{
    return {0xc1,
            0x0c,
            0x1e,
            skill,
            CellX,
            CellY,
            direction,
            byte7,
            byte8,
            static_cast<std::uint8_t>(key >> 8),
            static_cast<std::uint8_t>(key & 0xffu),
            byte11};
}

// 0x582393..0x5830dc: 0x29 sends the 1E with the logical cells, the heading · 256/360 and the chosen one's key; the
// speeds, action 0x3d and +0x378 at 0. In Lorencia and in Devias.
TEST(HeroSkillsKnightTest, Skill29SendsThe1E)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x29, Knight, map);
        tested->Hero().movement = 1;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws + C3Draws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x29, ExpectedDirection(TurnsOf256), 0, 0, ChosenKey));
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0x29u);
        EXPECT_EQ(tested->Hero().animation.action, 0x3d);
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z,
                        static_cast<float>(Client::Math::CreateAngle(18250.0f, 11550.0f, 18450.0f, 11750.0f)));
    }
}

// 0x5823b1..0x5823ce: without a chosen one for the weapon skill, the key 0xffff; with a chosen one that 0x558eb0 does
// not let be attacked (+0x370 neither 2 nor 8, without PK or Ctrl), too.
TEST(HeroSkillsKnightTest, WithoutAnAttackableChosenTheKeyIsFfff)
{
    for (int variant = 0; variant < 2; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x29);
        if (variant == 0)
            tested->Movement().weaponSkill.target = -1;
        else
            tested->world.Actors()[ChosenSlot].byte370 = 0;
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x29, ExpectedDirection(TurnsOf256), 0, 0, 0xffff));
    }
}

// The inline strstr with "webzen" in the published hero's name: it neither sends nor reads the cells, but does the
// rest. Without the session, the C3 still draws its two rand().
TEST(HeroSkillsKnightTest, TheGameMasterAndTheClosedSocketSendNothing)
{
    {
        const auto tested = AKnight(0x29);
        Rename(tested->Hero(), "gmwebzen");
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
        EXPECT_EQ(tested->Hero().animation.action, 0x3d);
    }
    {
        const auto tested = AKnight(0x29);
        auto context = tested->Context();
        context.send = nullptr;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws + C3Draws);
        ASSERT_TRUE(
            UseTheKnightSkill(tested->Hero(), Client::Kernel::CharacterClass::DarkKnight, 0x29, context).HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0x29u);
    }
}

// 0x587a8d..0x58886f: 0x2a in reach sends 0x4e9920's cell in byte 8 and does 0x3e, without 0x4f3f80; out of reach, the
// other chosen one (0x6c8e38) at −1 and nothing more.
TEST(HeroSkillsKnightTest, Skill2aSendsTheRelativeCellOrForgetsTheTarget)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x2a, Knight, map);
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x2a, ExpectedDirection(TurnsOf256), 0, 0xaa, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x3e);
    }
    const auto tested = AKnight(0x2a);
    tested->Movement().selectedSkillRange = 2.0f;
    tested->world.Runtime().selectedTargets[4] = 2;
    const CrtRandom expected = Advanced(tested->random, PaidDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
    EXPECT_EQ(tested->Hero().animation.action, 1);
}

// 0x5830e1..0x583ece: 0x37 reads the cells twice and sends the target's position (each side between −8 and 7) in byte
// 7, without checking the reach.
TEST(HeroSkillsKnightTest, Skill37SendsTheClampedTargetPosition)
{
    struct Case
    {
        std::uint32_t targetX;
        std::uint32_t targetY;
        std::uint8_t position;
    };
    for (const Case& tried : {Case{184, 117, 0xaa}, Case{200, 100, 0xf0}, Case{100, 200, 0x0f}, Case{182, 115, 0x88}})
    {
        SCOPED_TRACE(tried.targetX);
        const auto tested = AKnight(0x37);
        tested->Movement().targetX = tried.targetX;
        tested->Movement().targetY = tried.targetY;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x37, ExpectedDirection(TurnsOf256), tried.position, 0, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x3d);
    }
}

// 0x583ed3..0x584d6e: 0x38, on foot and in reach, sends the heading on the 255 scale and the opposite in byte 8 and
// does 0x8d. Mounted (Uniria, Dinorant, the dark horse or Fenrir) or out of reach, nothing.
TEST(HeroSkillsKnightTest, Skill38OnlyOnFootAndInRange)
{
    {
        const auto tested = AKnight(0x38);
        const CrtRandom expected = Advanced(tested->random, PaidDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        const double byteScale = 255.0;
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x38, ExpectedDirection(TurnFraction, byteScale), 0xaa,
                                                 ExpectedDirection(TurnFraction, byteScale, 180.0), ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x8d);
    }
    for (const std::uint16_t pet :
         {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}, std::uint16_t{0x1c16}, std::uint16_t{0x1c37}})
    {
        SCOPED_TRACE(pet);
        const auto tested = AKnight(0x38);
        tested->Hero().pet.model = pet;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
    const auto tested = AKnight(0x38);
    tested->Movement().selectedSkillRange = 2.0f;
    tested->world.Runtime().selectedTargets[4] = 2;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], 2);
}

// 0x586436..0x587317: 0x41 sends the target's cell against the hero's in byte 7 and 0x4e9920's in byte 8; the action
// by the mount outside the safe zone: 0x51 on the dark horse, 0x5b on Fenrir and otherwise 0x94.
TEST(HeroSkillsKnightTest, Skill41ActsByTheMount)
{
    struct Case
    {
        std::uint16_t pet;
        std::uint8_t safeZone;
        std::uint8_t action;
    };
    for (const Case& tried : {Case{Client::Kernel::NoItemModel, 0, 0x94}, Case{0x1c16, 0, 0x51}, Case{0x1c37, 0, 0x5b},
                              Case{0x1c16, 1, 0x94}, Case{0x1c37, 1, 0x94}, Case{0x1c14, 0, 0x94}})
    {
        SCOPED_TRACE(tried.pet);
        SCOPED_TRACE(tried.safeZone);
        const auto tested = AKnight(0x41);
        tested->Hero().pet.model = tried.pet;
        tested->Hero().safeZone = tried.safeZone;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x41, ExpectedDirection(TurnsOf256), 0xaa, 0xaa, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, tried.action);
        EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    }
    // The target's cell comes from +0x3a8/+0x3ac, not from 0x77e1ba8: byte 7 follows it; 0x4e9920 lets the nibble be
    // overwritten.
    const auto tested = AKnight(0x41);
    tested->Hero().attackPosition = {19450.0f, 11850.0f, 0.0f};
    tested->Movement().selectedSkillRange = 20.0f;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->Sent(0)[7], 0xfb);
    EXPECT_EQ(tested->Sent(0)[8], 0xb4);
}

// 0x586419..0x587346: 0x3e, on the dark horse and outside the safe zone, does 0x53 with sound 0x1ce. Without the horse,
// in the safe zone, without durability (+0xdc6) or in Chaos Castle, nothing.
TEST(HeroSkillsKnightTest, Skill3eNeedsTheDarkHorse)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x3e, Lord, map);
        tested->Hero().pet.model = 0x1c16;
        tested->world.OwnCharacter().equipment[8].durability = 30;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + EntryDraws + 2 * CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x3e, ExpectedDirection(TurnsOf256), 0xaa, 0xaa, ChosenKey));
        EXPECT_EQ(tested->Hero().animation.action, 0x53);
        EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1ce});
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    for (int variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x3e, Knight, variant == 3 ? 18 : 0);
        tested->Hero().pet.model = variant == 0 ? std::uint16_t{0x1c37} : std::uint16_t{0x1c16};
        tested->Hero().safeZone = variant == 1 ? 1 : 0;
        tested->world.OwnCharacter().equipment[8].durability = variant == 2 ? 0 : 30;
        const CrtRandom expected = Advanced(tested->random, variant >= 2 ? UnpaidDraws : PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 1);
    }
}

// 0x58734b..0x587a88: 0x30 sends the 19 with the player's key if it passes the 300 ms brake and, sent or not, does
// 0x3f.
TEST(HeroSkillsKnightTest, Skill30SendsThe19AndActs)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x30, Knight, map);
        tested->Hero().movement = 1;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x30, 0x12, 0x34}));
        EXPECT_EQ(tested->Movement().lastActionTick, 10'000u);
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
        EXPECT_EQ(tested->Hero().animation.action, 0x3f);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    for (int variant = 0; variant < 2; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x30);
        if (variant == 0)
            tested->Movement().lastActionTick = 9'800;
        else
            Rename(tested->Hero(), "webzen");
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().animation.action, 0x3f);
    }
    // Without GetTickCount, the brake cuts.
    const auto tested = AKnight(0x30);
    auto context = tested->Context();
    context.tickCount = nullptr;
    const auto result = UseTheKnightSkill(tested->Hero(), Client::Kernel::CharacterClass::DarkKnight, 0x30, context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(Contains(result.Error(), "0x5873ba")) << result.Error();
}

// 0x584d73.., 0x5854fd.. and 0x585c8a..: 0x3f, 0x40 and 0x4b send the 19 and leave +0x378 at 0, without an action. 0x3f
// needs someone in the party (0x78aba48). The three go on without a right-hand weapon.
TEST(HeroSkillsKnightTest, TheLordSkillsSendThe19)
{
    for (const std::uint8_t skill : {std::uint8_t{0x3f}, std::uint8_t{0x40}, std::uint8_t{0x4b}})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(skill);
            SCOPED_TRACE(map);
            const auto tested = AKnight(skill, Lord, map);
            tested->Hero().hands[0].model = Client::Kernel::NoItemModel;
            tested->world.Runtime().hud.partyCount = 2;
            tested->Hero().movement = 1;
            const CrtRandom expected = Advanced(tested->random, PaidDraws + EntryDraws + C3Draws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, skill, 0x12, 0x34}));
            EXPECT_EQ(tested->Hero().animation.action, 1);
            EXPECT_EQ(tested->Hero().movement, 0);
        }
    }
    const auto tested = AKnight(0x3f, Lord);
    const CrtRandom expected = Advanced(tested->random, UnpaidDraws + EntryDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
}

// 0x588874..0x5896da: 0x4c, with Fenrir on the published hero. In reach and with a chosen one: the delay, +0x434, the
// 1E with 0x4e9920's cell in byte 8 and the magic key (0x57cf90) in byte 11 and in +0x1bd, +0x378 at 0 and the hands'
// action (0x4e9c50: only the right, 0x64).
TEST(HeroSkillsKnightTest, Skill4cSendsTheMagicKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x4c, Knight, map);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().magicKeyCounter = 0x32;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + EntryDraws + CellsDraws + C3Draws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), DurationMagic(0x4c, ExpectedDirection(TurnsOf256), 0, 0xaa, ChosenKey, 1));
        EXPECT_EQ(tested->Hero().magicKey, 1);
        EXPECT_EQ(tested->Movement().magicKeyCounter, 1);
        EXPECT_EQ(tested->Hero().skillTarget4c, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Hero().animation.action, 0x64);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    {
        // Without Fenrir, or in Chaos Castle, it cannot be used.
        for (const std::int32_t map : {0, 18})
        {
            const auto tested = AKnight(0x4c, Knight, map);
            tested->Hero().pet.model = map == 0 ? std::uint16_t{0x1c16} : std::uint16_t{0x1c37};
            const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
        }
    }
    {
        // In reach without a chosen one: +0x434 at −1, without the delay or +0x378.
        const auto tested = AKnight(0x4c);
        tested->Hero().pet.model = 0x1c37;
        tested->Hero().movement = 1;
        tested->Movement().weaponSkill.target = -1;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().skillTarget4c, -1);
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_TRUE(tested->sent.empty());
    }
}

// 0x5896ea..0x589779: 0x4c out of range walks to the reach · 1.2 of the target (with the cells read) and sets +0x378 to
// 1; without a chosen one, the other chosen one at −1.
TEST(HeroSkillsKnightTest, Skill4cOutOfRangeWalks)
{
    {
        const auto tested = AKnight(0x4c);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().selectedSkillRange = 2.0f;
        tested->Hero().attackPosition = {19050.0f, 11550.0f, 0.0f};
        tested->Movement().targetX = 190;
        tested->Movement().targetY = 115;
        const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        ASSERT_GT(tested->Hero().path.count, 0);
        const auto last = tested->Hero().path.cells[tested->Hero().path.count - 1u];
        EXPECT_GE(last.x, 188);
        EXPECT_EQ(last.y, 115);
    }
    {
        const auto tested = AKnight(0x4c);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().selectedSkillRange = 2.0f;
        tested->Movement().weaponSkill.target = -1;
        tested->world.Runtime().selectedTargets[4] = 2;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// P-C256: 0x4c out of range with the target's cell outside 0..255 (which PathFinding2 takes as a linear index at
// 0x58975f): after the reads of the cells, the path is discarded without cutting.
TEST(HeroSkillsKnightTest, Skill4cDiscardsAWalkOffTheGrid)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const glm::vec3 position : {glm::vec3{-1000.0f, 11550.0f, 0.0f}, glm::vec3{18450.0f, 30000.0f, 0.0f}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(position.x));
            const auto tested = AKnight(0x4c, Knight, map);
            tested->Hero().pet.model = 0x1c37;
            tested->Movement().selectedSkillRange = 2.0f;
            tested->Hero().attackPosition = position;
            tested->Movement().targetX = 190;
            tested->Movement().targetY = 115;
            const CrtRandom expected = Advanced(tested->random, PaidDraws + CellsDraws);
            const auto result = tested->Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
            EXPECT_EQ(tested->Hero().movement, 0);
            EXPECT_EQ(tested->Hero().path.count, 0);
        }
    }
}

// 0x581912..0x58197a: without a right-hand weapon, or with a shield in either hand, 0x29 does not go on: only the mana
// and the record. Another type that is not a player does not either.
TEST(HeroSkillsKnightTest, TheHandsAndTheTypeGateTheSkill)
{
    for (int variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        const auto tested = AKnight(0x29);
        if (variant == 0)
            tested->Hero().hands[0].model = Client::Kernel::NoItemModel;
        else if (variant == 1)
            tested->Hero().hands[1].model = 0xd00;
        else if (variant == 2)
            tested->Hero().hands[0].model = 0xc12;
        else
            tested->Hero().type = 0x1f;
        const CrtRandom expected = Advanced(tested->random, EntryDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
    // 0xe12 is no longer a shield.
    const auto tested = AKnight(0x29);
    tested->Hero().hands[1].model = 0xe12;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->sent.size(), 1u);
}

// 0x581b70..0x58236f: without the mana, 0x558110(3) looks for the potion and the skill is not used (with it, 0x5af520
// cuts: milestone 5); without the stamina or the energy, it is not used (no 0x552c50 with the stamina); the Dark Lord
// without the leadership ends.
TEST(HeroSkillsKnightTest, TheCostsGateTheSkill)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AKnight(0x29, Knight, map);
        tested->skills = SkillsWith({{.skill = 0x29, .mana = 101}});
        const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        const auto potionExpected = Advanced(tested->random, UnpaidDraws + 3);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 6, 0x26, 24, 0, 0}));
        EXPECT_EQ(tested->random.State(), potionExpected.State());
        EXPECT_EQ(tested->world.Runtime().ui.field5767954, 10);
        EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x20});
        EXPECT_EQ(tested->world.OwnCharacter().mana, 100);
    }
    {
        const auto tested = AKnight(0x29);
        tested->skills = SkillsWith({{.skill = 0x29, .mana = 100, .stamina = 101}});
        const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
    {
        // 0x551f40: 10 · 70 · 4 / 100 + 20 = 48, contra +0x30 + +0x1e.
        const auto tested = AKnight(0x29);
        tested->skills = SkillsWith({{.skill = 0x29, .level = 10, .energy = 70}});
        tested->world.OwnCharacter().energy = 40;
        tested->world.OwnCharacter().word30 = 7;
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        tested->world.OwnCharacter().word30 = 8;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->sent.size(), 1u);
    }
    {
        // 0x5521f0: the Dark Lord with +0x2e above +0x36 + +0x20; the knight does not check it.
        for (const std::uint8_t heroClass : {Lord, Knight})
        {
            SCOPED_TRACE(heroClass);
            const auto tested = AKnight(0x29, heroClass);
            tested->skills = SkillsWith({{.skill = 0x29, .leadership = 30}});
            tested->world.OwnCharacter().leadership = 29;
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->sent.size(), heroClass == Lord ? 0u : 1u);
        }
    }
}

// 0x58238c: off the table (0x13) or with one that jumps to 0x5854be (0x31), nothing after paying. In Chaos Castle
// (0x581a71..0x581ab1, maps 18 to 23) 0x31 and the raven's orders (0x78 to 0x7b) cannot even be used: they are not
// paid; on the outer edges (17 and 24), in Lorencia and in Devias, they are paid and go on to 0x5854be.
TEST(HeroSkillsKnightTest, TheOtherSkillsDoNothing)
{
    for (const std::int32_t map : {0, 2, 17, 18, 23, 24})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x31}, std::uint8_t{0x78}, std::uint8_t{0x7b}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            const auto tested = AKnight(skill, Knight, map);
            const bool chaosCastle = map >= 18 && map <= 23;
            const CrtRandom expected = Advanced(tested->random, chaosCastle ? UnpaidDraws : PaidDraws);
            ASSERT_TRUE(tested->Use().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
            EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
        }
    }
    for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x31}})
    {
        SCOPED_TRACE(skill);
        const auto tested = AKnight(skill);
        const CrtRandom expected = Advanced(tested->random, PaidDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
    }
    const auto tested = AKnight(0x31, Knight, 23);
    const CrtRandom expected = Advanced(tested->random, UnpaidDraws);
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x4e9920: high ((y) − (targetY) + 8) << 4 and low (x) − (targetX) + 8, without masking the low one (Q-C1232).
TEST(HeroSkillsKnightHelpersTest, TheRelativeCellBleedsIntoTheHighNibble)
{
    EXPECT_EQ(Client::Skills::RelativeTargetCell(18250.0f, 11550.0f, 18450, 11750), 0xaa);
    EXPECT_EQ(Client::Skills::RelativeTargetCell(18250.0f, 11550.0f, 18250, 11550), 0x88);
    // X − targetX = +9 (in negative cells): the low 0x11 sets bit 4 of the high 0x80.
    EXPECT_EQ(Client::Skills::RelativeTargetCell(17350.0f, 11550.0f, 18250, 11550), 0x91);
}

// 0x4e9c50: both hands 0x63, the right 0x64, the left 0x65 and none 0x62.
TEST(HeroSkillsKnightHelpersTest, TheHandsChooseTheAction)
{
    Client::Kernel::ActorState actor = Client::Tests::UnarmedPlayer(1);
    EXPECT_EQ(Client::Skills::ActionByTheHands(actor), 0x62);
    actor.hands[1].model = 0xc12;
    EXPECT_EQ(Client::Skills::ActionByTheHands(actor), 0x65);
    actor.hands[0].model = Sword;
    EXPECT_EQ(Client::Skills::ActionByTheHands(actor), 0x63);
    actor.hands[1].model = Client::Kernel::NoItemModel;
    EXPECT_EQ(Client::Skills::ActionByTheHands(actor), 0x64);
}

// 0x57cf90: from 1 to 0x32 and back to 1.
TEST(HeroSkillsKnightHelpersTest, TheMagicKeyWrapsAfter32)
{
    Client::Movement::GameMovementState state;
    std::uint8_t key = 0;
    EXPECT_EQ(Client::Skills::NextMagicKey(state, key), 1);
    EXPECT_EQ(key, 1);
    state.magicKeyCounter = 0x31;
    EXPECT_EQ(Client::Skills::NextMagicKey(state, key), 0x32);
    EXPECT_EQ(Client::Skills::NextMagicKey(state, key), 1);
    EXPECT_EQ(state.magicKeyCounter, 1);
}

// 0x551f40: the energy, 0 with +0x2c at 0 and otherwise (+0x20 · +0x2c · 4) / 100 + 20. It opens and closes the entry:
// 60 rand().
TEST(HeroSkillsKnightHelpersTest, TheEnergyComesFromSkillBmd)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    CrtRandom random;
    const auto skills = SkillsWith(
        {{.skill = 0x29, .range = 3, .mana = 0x123, .stamina = 0x45, .level = 7, .energy = 9}, {.skill = 0x2a}});
    const Client::Skills::SkillDelayContext context{.world = *world, .random = random, .skills = &skills};
    const CrtRandom expected = Advanced(random, 2 * EntryDraws);
    const auto energy = Client::Skills::SkillEnergyRequirement(0x29, context);
    ASSERT_TRUE(energy.HasValue()) << energy.Error();
    EXPECT_EQ(energy.Value(), 7 * 9 * 4 / 100 + 20);
    const auto none = Client::Skills::SkillEnergyRequirement(0x2a, context);
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_EQ(none.Value(), 0);
    EXPECT_EQ(random.State(), expected.State());
    const Client::Skills::SkillDelayContext missing{.world = *world, .random = random};
    EXPECT_FALSE(Client::Skills::SkillEnergyRequirement(0x29, missing).HasValue());
}
} // namespace

// 0x58975f: 0x4c out of reach on map 34, with Crywolf at stage 3 and an altar (0x206) in 0x6c8e54. PathFinding2 sets
// the A* flag (0x4eac0a..0x4eac41), which does not count with a nonzero reach (0x4eadd4): the path, the rand() and
// +0x378 are the same as with another NPC (0x203) chosen.
TEST(HeroSkillsKnightTest, TheApproachWalksTheSameWithACrywolfAltar)
{
    const auto approach = [](std::uint16_t chosenType)
    {
        auto tested = AKnight(0x4c, Knight, 34);
        tested->Hero().pet.model = 0x1c37;
        tested->Movement().selectedSkillRange = 2.0f;
        tested->Hero().attackPosition = {19050.0f, 11550.0f, 0.0f};
        tested->Movement().targetX = 190;
        tested->Movement().targetY = 115;
        tested->world.Runtime().crywolf.stage = 3;
        tested->world.Runtime().chosenNpc.index = 30;
        tested->world.Actors()[30].type = chosenType;
        const auto expected = Advanced(tested->random, PaidDraws + CellsDraws);
        const auto used = tested->Use();
        EXPECT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        return tested;
    };
    const auto altar = approach(0x206);
    const auto other = approach(0x203);
    EXPECT_EQ(altar->Hero().movement, 1);
    ASSERT_GT(altar->Hero().path.count, 0);
    EXPECT_EQ(altar->Hero().path.count, other->Hero().path.count);
    for (std::size_t cell = 0; cell < altar->Hero().path.count; ++cell)
    {
        EXPECT_EQ(altar->Hero().path.cells[cell].x, other->Hero().path.cells[cell].x);
        EXPECT_EQ(altar->Hero().path.cells[cell].y, other->Hero().path.cells[cell].y);
    }
    EXPECT_TRUE(altar->sent.empty());
}
