#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Skills/HeroSkills.h"
#include "Movement/GameMovement.h"
#include "Session/HeroMovementHooks.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/MouseState.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkSpirit.h"
#include "Movement/GameMovementState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"
#include "CastleSiege/CastleSiegeState.h"

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

// The head of 0x57cfc0 (06 § Usar una skill): the hero in slot 0, a chosen one in slot 5 and an invented Skill.bmd.
// The branch of classes 1, 3 and 4 (HeroSkillsKnight, with its tests) ends without a right-hand weapon for the skills
// not on its list; the other branches still cut with their address.
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;
using Client::Skills::UseTheHeroSkill;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
// One read of +0x38e: one rand() when it closes at 0.
constexpr int Byte38eDraws = 1;
// The own record with the slot's skill and the range entry (0x5526f0): what every round with a use spends.
constexpr int HeadDraws = OwnDraws + EntryDraws;
// Plus the +0x31 entry (0x57d2f1).
constexpr int PartyDraws = HeadDraws + EntryDraws;
// Plus the two logical cells (0x57d4d6, 0x57d4f4).
constexpr int AimDraws = PartyDraws + 2 * CellDraws;
// The branch of classes 1, 3 and 4 (HeroSkillsKnight) without a right-hand weapon and with a skill off its list: the
// mana and stamina of 0x551c70 and the close of the own record, open since 0x581894.
constexpr int KnightBranchDraws = EntryDraws + OwnDraws;
constexpr std::size_t ChosenSlot = 5;
// The head of the Wizard branch (0x5897b7..0x58a08a) without a delay: the own record and, with it open, the cost,
// energy and delay entries.
constexpr int WizardHeadDraws = OwnDraws + 3 * EntryDraws;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

struct InventedSkill
{
    std::uint8_t skill = 0;
    std::uint8_t range = 0;
    std::uint8_t party = 0;
    std::uint8_t mana = 0;
};

Client::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Tables::SkillRecordSize;
        plain[base + 0x22] = static_cast<std::byte>(invented.mana);
        plain[base + 0x26] = static_cast<std::byte>(invented.range);
        plain[base + 0x31] = static_cast<std::byte>(invented.party);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

enum HeroClass : std::uint8_t
{
    Wizard = 0,
    Knight = 1,
    Elf = 2,
    Gladiator = 3,
    Lord = 4,
};

// Player.bmd's skeleton for any model: a transformed hero's 0x4f4a10 (0x4ea030) asks for its actions.
class AnyModels final : public Client::Models::IModelLibrary
{
public:
    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId /*model*/) const override
    {
        return &skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }

    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

// A player hero with `skill` in slot 0 and action 1, which 0x57d45d lets through.
struct SkillHero
{
    explicit SkillHero(std::uint8_t skill, std::uint8_t heroClass = Knight, std::int32_t map = 0)
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
        world.OwnCharacter().SetSkillAt(0, skill);
        auto& chosen = world.Actors()[ChosenSlot];
        chosen.alive = true;
        chosen.byte88 = 2; // a monster: 0x558bb0 lets it be attacked
        chosen.position = {18450.0f, 11750.0f, 0.0f};
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Ui::GameUiState& Ui()
    {
        return world.Runtime().ui;
    }
    Client::Kernel::MouseState& Mouse()
    {
        return world.Runtime().mouse;
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
                                .models = models,
                                .tickCount = [this] { return ticks; },
                                .worldTime = worldTime,
                                .send = [this](Client::Protocol::Outgoing outgoing)
                                { sent.push_back(std::move(outgoing)); },
                                .terrain = &terrain};
    }
    Client::Core::Expected<void, std::string> Use()
    {
        return UseTheHeroSkill(Hero(), Context());
    }

    Client::Kernel::WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    float worldTime = 5000.0f;
    std::uint32_t ticks = 100000;
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    // nullptr: a transformed hero's 0x4f4a10 cuts (0x4ea030).
    const Client::Models::IModelLibrary* models = nullptr;
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Tables::SkillTable skills = SkillsWith({{0x11, 3},
                                                    {0x13, 4},
                                                    {0x16, 6},
                                                    {0x18, 6},
                                                    {0x1a, 2},
                                                    {0x28, 7},
                                                    {0x2c, 3},
                                                    {0x2f, 2},
                                                    {0x30, 2},
                                                    {0x31, 3},
                                                    {0x3a, 7},
                                                    {0x36, 3},
                                                    {0x3c, 3},
                                                    {0x3d, 3, 0, 0x40},
                                                    {0x43, 3},
                                                    {0x4b, 3},
                                                    {0x1d, 5, 2}});
};

std::unique_ptr<SkillHero> AHeroClicking(std::uint8_t skill, std::uint8_t heroClass = Knight, std::int32_t map = 0)
{
    auto tested = std::make_unique<SkillHero>(skill, heroClass, map);
    tested->Ui().rightClick = true;
    tested->world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
    return tested;
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

// The Wizard branch aimed (0x58a118) at the chosen one in slot 5.
void ExpectTheWizardAimed(SkillHero& tested)
{
    EXPECT_EQ(tested.Movement().targetX, 184u);
    EXPECT_EQ(tested.Movement().targetY, 117u);
}

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

// 0x57cfe2..0x57d00f: with the editor mode, the mouse on the UI or 0x1000 in +0x78, nothing, not even rand().
TEST(HeroSkillsTest, TheGuardsDoNothing)
{
    for (int guard = 0; guard < 3; ++guard)
    {
        SCOPED_TRACE(guard);
        const auto tested = AHeroClicking(0x11);
        if (guard == 0)
            tested->Movement().inputBlocked = true;
        else if (guard == 1)
            tested->Ui().mouseOnUi = 1;
        else
            tested->Hero().flags78 = 0x1000;
        tested->Hero().byte80 = 1;
        const auto before = tested->random.State();
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), before);
        EXPECT_TRUE(tested->Ui().rightClick);
        EXPECT_FLOAT_EQ(tested->Hero().alpha, 1.0f);
    }
}

// 0x57d23c..0x57d260: without the right button or the auto attack, the clock starts again after the own record and the
// range entry; the range stays in the branches' local.
TEST(HeroSkillsTest, WithoutAUseTheClockStartsAgain)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Knight, map);
        tested->Movement().actionInputTime = 10.0f;
        tested->Movement().actionInputElapsed = 7.0f;
        tested->Movement().followTarget = 3;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputElapsed, 0.0f);
        EXPECT_FLOAT_EQ(tested->Movement().selectedSkillRange, 4.0f);
        EXPECT_EQ(tested->Movement().followTarget, 3);
    }
}

// 0x5526f0: mounted on the 0x1c16, the range adds 2.
TEST(HeroSkillsTest, TheThirdMountAddsTwoToTheRange)
{
    auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13});
    tested->Hero().pet.model = 0x1c16;
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(tested->Movement().selectedSkillRange, 6.0f);
}

// 0x57d02b..0x57d04a: with +0x80 at 1, the fade drops 0.1 per round and, below 0.1, +0x80 becomes 2.
TEST(HeroSkillsTest, TheTeleportFadeLowersTheAlpha)
{
    auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13});
    tested->Hero().byte80 = 1;
    tested->Hero().alpha = 0.5f;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_FLOAT_EQ(tested->Hero().alpha, 0.4f);
    EXPECT_EQ(tested->Hero().byte80, 1);
    tested->Hero().alpha = 0.15f;
    ASSERT_TRUE(tested->Use().HasValue());
    EXPECT_NEAR(tested->Hero().alpha, 0.05f, 1e-6f);
    EXPECT_EQ(tested->Hero().byte80, 2);
}

// 0x57d179..0x57d1b8: the right button on 0x28 charges it as 0x3a (the frames at 1 and the published one's +0x1bc at
// 0) if +0x80 is 0 or 3; with another value, it leaves without spending the right button.
TEST(HeroSkillsTest, TheRightClickChargesTheSkill28As3a)
{
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().byte1bc = 9;
        tested->Hero().byte80 = 3;
        tested->Ui().autoAttack = true;
        const auto result = tested->Use();
        // The Wizard branch releases it in the same step: 0x3a's 19 (0x58a141).
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(std::to_integer<int>(tested->sent[0].packet.Bytes()[2]), 0x19);
        EXPECT_FALSE(tested->Ui().rightClick);
        EXPECT_EQ(tested->Ui().rightHoldFrames, 1);
        EXPECT_EQ(tested->Hero().byte1bc, 0);
        // 0x57d419: the Wizard's is already 0x3a; the range is 0x28's (0x57d114, before charging it).
        EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
        EXPECT_FLOAT_EQ(tested->Movement().selectedSkillRange, 7.0f);
    }
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().byte80 = 2;
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->Ui().rightClick);
        EXPECT_EQ(tested->Ui().rightHoldFrames, 0);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 0.0f);
        EXPECT_EQ(tested->random.State(), expected.State());
    }
}

// 0x57d12c..0x57d165: while charging, the frames count until the release, which clears the right button and uses it.
TEST(HeroSkillsTest, WhileChargingTheFramesCountUntilTheRelease)
{
    const auto tested = AHeroClicking(0x3a, Wizard);
    tested->Ui().rightHoldFrames = 4;
    tested->Movement().actionInputTime = 1000.0f;
    const auto held = tested->Use();
    ASSERT_TRUE(held.HasValue()) << held.Error();
    EXPECT_EQ(tested->Ui().rightHoldFrames, 5);
    EXPECT_TRUE(tested->Ui().rightClick);
    EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);

    tested->Ui().rightReleased = true;
    tested->Ui().rightDown = true;
    const auto released = tested->Use();
    ASSERT_TRUE(released.HasValue()) << released.Error();
    EXPECT_EQ(tested->sent.size(), 1u);
    EXPECT_FALSE(tested->Ui().rightReleased);
    EXPECT_FALSE(tested->Ui().rightClick);
    EXPECT_FALSE(tested->Ui().rightDown);
    EXPECT_EQ(tested->Ui().rightHoldFrames, 0);
    EXPECT_FLOAT_EQ(tested->Movement().actionInputElapsed, 0.0f);
}

// 0x57d1bc..0x57d1ef: the auto attack uses the skill without the right button with 0x6c8e38 at 2 and a chosen one,
// except on map 6 and in Chaos Castle.
TEST(HeroSkillsTest, TheAutoAttackUsesTheSkillWithTheOtherChosenInTwo)
{
    for (const std::int32_t map : {0, 2, 6, 18, 23})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Wizard, map);
        tested->Ui().autoAttack = true;
        auto& targets = tested->world.Runtime().selectedTargets;
        targets[2] = static_cast<std::int32_t>(ChosenSlot);
        targets[4] = 2;
        const auto result = tested->Use();
        const bool here = map != 6 && map != 18 && map != 23;
        ASSERT_TRUE(result.HasValue()) << result.Error();
        if (here)
        {
            ExpectTheWizardAimed(*tested);
        }
        else
        {
            EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 5000.0f);
            EXPECT_EQ(tested->Movement().targetX, 0u);
        }
    }
    {
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Wizard);
        tested->Ui().autoAttack = true;
        tested->world.Runtime().selectedTargets[4] = 2;
        ASSERT_TRUE(tested->Use().HasValue());
    }
}

// 0x57d1f8..0x57d22d: the time since the last round without a use, in s; at 3600 it lets go of the right button and
// leaves.
TEST(HeroSkillsTest, AfterAnHourTheRightButtonIsLetGo)
{
    {
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Ui().rightClick = false;
        tested->Ui().rightDown = true;
        tested->Movement().followTarget = 7;
        tested->worldTime = 3600000.0f;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(tested->Ui().rightDown);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputElapsed, 3600.0002f);
        // 0x6c8e50 is only touched at 0x57d294.
        EXPECT_EQ(tested->Movement().followTarget, 7);
    }
    {
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Movement().followTarget = 7;
        tested->worldTime = 3599000.0f;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectTheWizardAimed(*tested);
        EXPECT_NEAR(tested->Movement().actionInputElapsed, 3599.0f, 1e-3f);
        EXPECT_EQ(tested->Movement().followTarget, -1);
    }
}

// 0x57d261..0x57d282: with the movement blocked (0x77f3bac), only the left click (which is spent) or the button down;
// the right one neither counts nor is spent and the clock is not checked.
TEST(HeroSkillsTest, ABlockedHeroUsesTheSkillWithTheLeftButton)
{
    {
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Movement().movementBlocked = true;
        tested->Movement().actionInputTime = 1.0f;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->Ui().rightClick);
        EXPECT_FLOAT_EQ(tested->Movement().actionInputTime, 1.0f);
    }
    for (const bool click : {true, false})
    {
        SCOPED_TRACE(click);
        auto tested = std::make_unique<SkillHero>(std::uint8_t{0x13}, Wizard);
        tested->Movement().movementBlocked = true;
        tested->Mouse().pendingClick = click;
        tested->Mouse().leftHeld = !click;
        const auto result = tested->Use();
        // Without a chosen one, the Wizard branch aims at the terrain (0x57ceea), which the context does not carry.
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x57ceea")) << result.Error();
        EXPECT_FALSE(tested->Mouse().pendingClick);
    }
}

// 0x5a5540: the siege skills only on map 30 with the siege on; otherwise it leaves before the +0x31 entry.
TEST(HeroSkillsTest, TheSiegeSkillsNeedTheSiege)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x2c, Wizard, map);
        const CrtRandom expected = Advanced(tested->random, HeadDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().followTarget, -1);
    }
    const auto tested = AHeroClicking(0x2c, Wizard, 30);
    tested->world.Runtime().castleSiege.siegeActive = true;
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectTheWizardAimed(*tested);
    // 0x58c16e: 0x2c goes through the range and asks for the weapon skill with the chosen one.
    EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
}

// 0x57d30c..0x57d31e: with +0x31 at 2, the skill needs 0x200 in the hero's +0x78.
TEST(HeroSkillsTest, APartySkillNeedsThe200Flag)
{
    {
        const auto tested = AHeroClicking(0x1d, Wizard);
        const CrtRandom expected = Advanced(tested->random, PartyDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
    }
    const auto tested = AHeroClicking(0x1d, Wizard);
    tested->Hero().flags78 = 0x200;
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectTheWizardAimed(*tested);
}

// 0x57d324..0x57d337: with +0x389, nothing after the entry.
TEST(HeroSkillsTest, ADyingHeroStopsAfterTheEntry)
{
    const auto tested = AHeroClicking(0x13, Wizard);
    tested->Hero().dying = 1;
    const CrtRandom expected = Advanced(tested->random, PartyDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x57d33d..0x57d3a5: in the safe zone, nothing in Lorencia or Devias; in Blood Castle and Chaos Castle, only the
// skills of the list.
TEST(HeroSkillsTest, TheSafeZoneOnlyLetsTheListInTheEventMaps)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t skill;
        bool goesOn;
    };
    for (const Case& tried : {Case{0, 0x1a, false}, Case{2, 0x1a, false}, Case{11, 0x1a, true}, Case{17, 0x13, false},
                              Case{18, 0x4b, true}, Case{18, 0x43, true}, Case{23, 0x11, false}, Case{24, 0x1a, false}})
    {
        SCOPED_TRACE(tried.map);
        SCOPED_TRACE(tried.skill);
        const auto tested = AHeroClicking(tried.skill, Wizard, tried.map);
        tested->Hero().safeZone = 1;
        const CrtRandom expected = Advanced(tested->random, PartyDraws);
        const auto result = tested->Use();
        if (tried.goesOn)
        {
            ExpectTheWizardAimed(*tested);
        }
        else
        {
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_EQ(tested->Movement().targetX, 0u);
        }
    }
}

// 0x57d3ab..0x57d458: with the auto attack, 0x6c8e38 by the class and the skill.
TEST(HeroSkillsTest, TheAutoAttackChoosesTheOtherTargetByClassAndSkill)
{
    struct Case
    {
        std::uint8_t heroClass;
        std::uint8_t skill;
        std::int32_t target;
    };
    for (const Case& tried : {Case{Elf, 0x18, 2}, Case{Elf, 0x13, -1}, Case{Elf, 0x43, -1}, Case{Knight, 0x30, -1},
                              Case{Knight, 0x13, 2}, Case{Lord, 0x40, -1}, Case{Lord, 0x3f, -1}, Case{Lord, 0x30, 2},
                              Case{Wizard, 0x3a, -1}, Case{Wizard, 0x13, 2}, Case{Gladiator, 0x30, 2},
                              Case{Gladiator, 0x4b, -1}, Case{Gladiator, 0x48, -1}, Case{5, 0x3a, 2}})
    {
        SCOPED_TRACE(tried.heroClass);
        SCOPED_TRACE(tried.skill);
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(tried.skill, tried.heroClass, map);
            tested->Ui().autoAttack = true;
            static_cast<void>(tested->Use());
            EXPECT_EQ(tested->world.Runtime().selectedTargets[4], tried.target);
        }
    }
    // In Chaos Castle nothing is chosen.
    const auto tested = AHeroClicking(0x13, Wizard, 18);
    tested->Ui().autoAttack = true;
    static_cast<void>(tested->Use());
    EXPECT_EQ(tested->world.Runtime().selectedTargets[4], -1);
}

// 0x57d45d..0x57d4b0: a player with an action off the list does not use it and forgets the right button's frames (the
// charge that just started); another type, with action 2 or more, does not use it and keeps them.
TEST(HeroSkillsTest, TheActionOfTheHeroGatesTheUse)
{
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().animation.action = 0x15;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Ui().rightHoldFrames, 0);
        EXPECT_FALSE(tested->Ui().rightClick);
    }
    {
        const auto tested = AHeroClicking(0x28, Wizard);
        tested->Hero().type = 0x1f;
        tested->Hero().animation.action = 2;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Ui().rightHoldFrames, 1);
    }
    for (const std::uint8_t action : {std::uint8_t{0xc}, std::uint8_t{0x8a}, std::uint8_t{0x44}, std::uint8_t{0x48},
                                      std::uint8_t{0x4a}, std::uint8_t{0x76}, std::uint8_t{0x79}})
    {
        SCOPED_TRACE(action);
        const auto tested = AHeroClicking(0x13, Wizard);
        tested->Hero().animation.action = action;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectTheWizardAimed(*tested);
    }
}

// 0x57d4b6: the Wizard neither aims nor checks the weapons: it goes straight to its branch (HeroSkillsWizard), which
// aims after its head; 0x13 does nothing more (0x58a13a).
TEST(HeroSkillsTest, TheWizardGoesStraightToItsBranch)
{
    const auto tested = AHeroClicking(0x13, Wizard);
    tested->Movement().targetX = 9;
    const CrtRandom expected = Advanced(tested->random, PartyDraws + WizardHeadDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    ExpectTheWizardAimed(*tested);
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    EXPECT_TRUE(tested->sent.empty());
}

// 0x57ce80 and 0x57d532..0x57d61b: the knight aims at the chosen one (its cell and its position) and, for each hand
// without the skill's option, asks for the weapon's with the slot and the chosen one; the record opened in each hand
// closes with 3848 rand(). Without a right-hand weapon, the branch (0x581894) ends without 0x13; the Magic Gladiator
// goes on into the Wizard's (0x5897b7), which with 0x13 does nothing after its head.
TEST(HeroSkillsTest, AKnightAimsAtTheChosenAndAsksForTheWeaponSkill)
{
    for (const std::uint8_t heroClass : {Knight, Gladiator, Lord})
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(heroClass);
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(0x13, heroClass, map);
            const int wizard = heroClass == Gladiator ? WizardHeadDraws : 0;
            const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws + wizard);
            const auto result = tested->Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            const auto& movement = tested->Movement();
            EXPECT_EQ(movement.targetX, 184u);
            EXPECT_EQ(movement.targetY, 117u);
            EXPECT_EQ(tested->Hero().attackPosition, glm::vec3(18450.0f, 11750.0f, 0.0f));
            EXPECT_EQ(movement.weaponSkill.active, 1u);
            EXPECT_EQ(movement.weaponSkill.slot, 0u);
            EXPECT_EQ(movement.weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
            EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
        }
    }
    // 0x57d617: a chosen one that 0x558bb0 does not let be attacked (an NPC) goes as −1.
    const auto tested = AHeroClicking(0x13, Knight);
    tested->world.Actors()[ChosenSlot].byte88 = 4;
    static_cast<void>(tested->Use());
    EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
}

// 0x5897a7..0x5897b1: the Magic Gladiator goes through the branch of classes 1, 3 and 4 (without a right-hand weapon,
// 0xa is not on its list) and on into the Wizard's: Hellfire (0x58c1b6) sends the 1E with its logical cell (the Y and
// the X), the heading and the key 0xffff, and sets action 0x96. The knight and the Dark Lord end at 0x5808da with
// nothing.
TEST(HeroSkillsTest, TheMagicGladiatorGoesOnThroughTheKnightIntoTheWizardBranch)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0xa, Gladiator, map);
        tested->skills = SkillsWith({{0xa}});
        tested->Hero().movement = 1;
        constexpr int SendDraws = 2;
        // 0x4f3f80 (the speeds) opens and closes the own record.
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws +
                                                                WizardHeadDraws + 2 * CellDraws + SendDraws + OwnDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x0a, 182, 115, 0, 0, 0, 0xff, 0xff, 0}));
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0xau);
        EXPECT_EQ(tested->Hero().animation.action, 0x96);
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    for (const std::uint8_t heroClass : {Knight, Lord})
    {
        SCOPED_TRACE(heroClass);
        const auto tested = AHeroClicking(0xa, heroClass);
        tested->skills = SkillsWith({{0xa}});
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
    }
}

// 0x57d564..0x57d57b: mounted, the table 0x599d70 skips the weapons for the skills it does not allow (0x30) and checks
// them for the rest (0x2f). 0x30 goes on without a right-hand weapon: the branch pays for it (0x551c70, 0x552c50 and
// 0x551f40), draws the C3's two rand() without the session and does 0x3f.
TEST(HeroSkillsTest, AMountedHeroSkipsTheWeaponsForSomeSkills)
{
    {
        const auto tested = AHeroClicking(0x30, Knight);
        tested->Hero().pet.model = 0x1c14;
        const CrtRandom expected = Advanced(tested->random, AimDraws + 3 * EntryDraws + 2 + OwnDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
        EXPECT_EQ(tested->Hero().animation.action, 0x3f);
    }
    {
        // 0x5599f2..0x559ad3: with 0x2f mounted and without a right-hand weapon, 0x559890 gives 0 before the options.
        const auto tested = AHeroClicking(0x2f, Lord);
        tested->Hero().pet.model = 0x1c37;
        const CrtRandom expected = Advanced(tested->random, AimDraws + 2 * OwnDraws + KnightBranchDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
    }
}

// 0x5599db..0x559afc: the weapon's special skills go to 0x559afc and the weapon's options to 0x55a368; those that reach
// 0x55b6f0 release the shine 0x5fe (0x55be23).
TEST(HeroSkillsTest, TheWeaponSkillsGoToTheirTails)
{
    {
        const auto tested = AHeroClicking(0x31, Knight);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->world.Particles().All()[0].type, 0x5fe);
    }
    {
        // 0x559a84..0x559a9f: 0x39 with a right-hand weapon from 0x212 to 0x411; its range of 0 leaves the chosen one
        // far and 0x559510 builds the path (case 5 of the arrival).
        const auto tested = AHeroClicking(0x13, Knight, 30);
        tested->world.Runtime().castleSiege.siegeActive = true;
        tested->world.OwnCharacter().SetSkillAt(0, 0x39);
        tested->Hero().hands[0].model = 0x300;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Hero().movementType, 5);
        EXPECT_EQ(tested->Hero().movement, 1);
    }
    {
        // 0x559ad5..0x559afa: 0x2b with a right-hand weapon from 0xc12 to 0xe11 gives 0 and goes on to the branch,
        // which ends with the shield.
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.OwnCharacter().SetSkillAt(0, 0x2b);
        tested->Hero().hands[0].model = 0xd00;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
    }
    {
        // 0x55a340..0x55a362: the second hand with the skill's option, which 0x559510 casts on the chosen one.
        const auto tested = AHeroClicking(0x13, Knight);
        auto& item = tested->world.OwnCharacter().equipment[1];
        item.optionCount = 2;
        item.optionBytes[0] = 0x12;
        item.optionBytes[1] = 0x13;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->world.Particles().All()[0].type, 0x5fe);
        // The D3, the 19 and, with 0x13, the DF with the target's cell.
        ASSERT_EQ(tested->sent.size(), 3u);
        EXPECT_EQ(ClearBytesOf(tested->sent[1])[3], 0x13);
        EXPECT_EQ(ClearBytesOf(tested->sent[2]), (std::vector<std::uint8_t>{0xc1, 0x05, 0xdf, 184, 117}));
    }
    {
        // 0x561f60: the elf's, without the list; except for 0x18 and 0x36 it does not shoot and goes on to the class
        // branch (0x57d790), which sends nothing with 0x13.
        const auto tested = AHeroClicking(0x13, Elf);
        auto& item = tested->world.OwnCharacter().equipment[0];
        item.optionCount = 1;
        item.optionBytes[0] = 0x13;
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->sent.empty());
    }
}

// The elf checks both hands with 0x561f60 and, without the option, goes to its branch (0x57d790): the head (the own
// record and the cost, delay and energy entries), the target and, in reach, the weapon skill with the chosen one and
// the logical cell for the line of sight; 0x18 is not one of those that send there (HeroSkillsElf, with its tests).
TEST(HeroSkillsTest, AnElfLooksAtBothHandsAndGoesToItsBranch)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x18, Elf, map);
        const CrtRandom expected =
            Advanced(tested->random, AimDraws + 2 * OwnDraws + OwnDraws + 3 * EntryDraws + 2 * CellDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().weaponSkill.active, 1u);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
    }
}

// 0x57d51f and 0x57d760..0x57d774: a wall between the hero and the target leaves the weapon skill at 0 and the chosen
// one at −1.
TEST(HeroSkillsTest, AWallBeforeTheTargetClearsTheWeaponSkill)
{
    const auto tested = AHeroClicking(0x13, Knight);
    tested->terrain.attributes->cells[Client::Data::Terrain::CellIndex(183, 116)] = 4;
    tested->Movement().weaponSkill = {.active = 1, .slot = 3, .target = 7};
    const CrtRandom expected = Advanced(tested->random, AimDraws + KnightBranchDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Movement().weaponSkill.active, 0u);
    EXPECT_EQ(tested->Movement().weaponSkill.slot, 0u);
    EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
}

// 0x57ceea: without a chosen one, the terrain point under the mouse; if it misses, the target stays as it was.
TEST(HeroSkillsTest, WithoutAChosenTheSkillAimsAtTheTerrain)
{
    {
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.Runtime().selectedTargets[2] = -1;
        auto context = tested->Context();
        context.pickTerrainPoint = []() -> Client::Core::Expected<std::optional<glm::vec3>, std::string>
        { return std::optional<glm::vec3>(glm::vec3(18399.0f, 11401.0f, 30.0f)); };
        const auto result = UseTheHeroSkill(tested->Hero(), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Movement().targetX, 183u);
        EXPECT_EQ(tested->Movement().targetY, 114u);
        EXPECT_EQ(tested->Hero().attackPosition, glm::vec3(18399.0f, 11401.0f, 30.0f));
        EXPECT_EQ(tested->Movement().weaponSkill.target, -1);
    }
    {
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.Runtime().selectedTargets[2] = -1;
        tested->Movement().targetX = 182;
        tested->Movement().targetY = 115;
        auto context = tested->Context();
        context.pickTerrainPoint = []() -> Client::Core::Expected<std::optional<glm::vec3>, std::string>
        { return std::optional<glm::vec3>(); };
        const auto result = UseTheHeroSkill(tested->Hero(), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->Movement().targetX, 182u);
    }
    {
        const auto tested = AHeroClicking(0x13, Knight);
        tested->world.Runtime().selectedTargets[2] = -1;
        const auto result = tested->Use();
        ASSERT_FALSE(result.HasValue());
        EXPECT_TRUE(Contains(result.Error(), "0x57ceea")) << result.Error();
    }
}

// 0x5897aa and 0x5808da: classes 5 to 7 (no branch) aim and go on only with 0x43 to 0x48 (HeroSkillsTail): 0x43, with
// the own record, the cost, delay and energy entries, the cell twice and the 1E.
TEST(HeroSkillsTest, TheOtherClassesOnlyGoOnWith43To48)
{
    {
        const auto tested = AHeroClicking(0x13, 5);
        const CrtRandom expected = Advanced(tested->random, AimDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 184u);
    }
    const auto tested = AHeroClicking(0x43, 7);
    constexpr int SendDraws = 2;
    const CrtRandom expected =
        Advanced(tested->random, AimDraws + OwnDraws + 3 * EntryDraws + 4 * CellDraws + SendDraws);
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(ClearBytesOf(tested->sent[0])[2], 0x1e);
    EXPECT_EQ(ClearBytesOf(tested->sent[0])[3], 0x43);
    EXPECT_EQ(tested->Hero().animation.action, 0x3f);
}

struct RavenHero
{
    explicit RavenHero(std::uint8_t slot, std::int32_t map = 0) : tested(AHeroClicking(0x13, Lord, map))
    {
        tested->Ui().rightDown = true;
        tested->Ui().rightReleased = true;
        tested->Hero().darkSpirit.emplace();
        tested->Hero().skillIndex = slot;
        tested->world.OwnCharacter().SetSkillAt(slot, 0x13);
        tested->world.Actors()[ChosenSlot].key = 0x0a1b;
    }
    Client::Core::Expected<void, std::string> Use()
    {
        auto context = tested->Context();
        context.send = [this](Client::Protocol::Outgoing outgoing) { sent.push_back(std::move(outgoing)); };
        return UseTheHeroSkill(tested->Hero(), context);
    }
    std::unique_ptr<SkillHero> tested;
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x420f70 (0x421599..0x421b20): slots 0x78 to 0x7a send the A7 with the slot's order and the key 0xffff, through
// send() with the XOR32; the right button is let go and the skill does not go on (not even the +0x31 entry). In
// Lorencia and in Devias.
TEST(HeroSkillsTest, TheRavenOrdersSendTheirA7)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t slot : {std::uint8_t{0x78}, std::uint8_t{0x79}, std::uint8_t{0x7a}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(slot);
            RavenHero raven(slot, map);
            raven.tested->Ui().rightHoldFrames = 0;
            const CrtRandom expected = Advanced(raven.tested->random, HeadDraws);
            const auto result = raven.Use();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(raven.tested->random.State(), expected.State());
            ASSERT_EQ(raven.sent.size(), 1u);
            EXPECT_EQ(raven.sent[0].delivery, Client::Network::Delivery::Clear);
            const std::vector<std::uint8_t> bytes{0xc1, 0x07, 0xa7, 0x00, static_cast<std::uint8_t>(slot - 0x78),
                                                  0xff, 0xff};
            EXPECT_EQ(ClearBytesOf(raven.sent[0]), bytes);
            const auto& ui = raven.tested->Ui();
            EXPECT_FALSE(ui.rightReleased);
            EXPECT_FALSE(ui.rightClick);
            EXPECT_FALSE(ui.rightDown);
            EXPECT_EQ(ui.rightHoldFrames, 0);
            EXPECT_EQ(raven.tested->Movement().followTarget, -1);
            EXPECT_EQ(raven.tested->Movement().targetX, 0u);
        }
    }
}

// 0x420fd6..0x421594: the 0x7b attacks the chosen one if 0x558bb0 allows it and it is a monster or a player: order 3
// with its key. Otherwise nothing goes out, but the right button is let go all the same and the skill does not go on.
TEST(HeroSkillsTest, TheRavenAttackOrderNeedsAnAttackableChosen)
{
    {
        RavenHero raven(0x7b);
        const auto result = raven.Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(raven.sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(raven.sent[0]), (std::vector<std::uint8_t>{0xc1, 0x07, 0xa7, 0x00, 0x03, 0x0a, 0x1b}));
    }
    for (const std::int32_t chosen : {static_cast<std::int32_t>(ChosenSlot), -1})
    {
        SCOPED_TRACE(chosen);
        RavenHero raven(0x7b);
        raven.tested->world.Actors()[ChosenSlot].byte88 = 4;
        raven.tested->world.Runtime().selectedTargets[2] = chosen;
        const auto result = raven.Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(raven.sent.empty());
        EXPECT_FALSE(raven.tested->Ui().rightDown);
    }
    {
        // Without the session (the socket at −1), nothing goes out either and the right button is let go.
        const auto tested = AHeroClicking(0x13, Lord);
        tested->Ui().rightDown = true;
        tested->Hero().darkSpirit.emplace();
        tested->Hero().skillIndex = 0x7b;
        tested->world.OwnCharacter().SetSkillAt(0x7b, 0x13);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(tested->Ui().rightDown);
    }
}

// 0x57d29e..0x57d2c3 and 0x420f9a..0x420fc5: without the raven, with 0x4000 on the published one, with a slot outside
// 0x78 to 0x7b or with only the click (already spent at 0x57d1b1), there is no order and the skill goes on to the class
// branch.
TEST(HeroSkillsTest, WithoutTheRavenOrTheButtonTheSkillGoesOn)
{
    for (int variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        RavenHero raven(variant == 2 ? std::uint8_t{0x7c} : std::uint8_t{0x79});
        if (variant == 0)
            raven.tested->Hero().darkSpirit.reset();
        else if (variant == 1)
            raven.tested->Hero().flags78 = 0x4000;
        else if (variant == 3)
            raven.tested->Ui().rightDown = false;
        raven.tested->Ui().rightReleased = false;
        const auto result = raven.Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(raven.sent.empty());
    }
}
} // namespace

// What 0x559afc spends up to 0x55b6f0 with 0x3c in reach: the head and the target (AimDraws), the cost (0x559b0b) and
// the delay (0x552c50) with their entries, the published hero's cells for the D3, the C3 19's two, 0x559510's entry
// when it closes and the hand's own record.
constexpr int ListedCastDraws = AimDraws + 2 * EntryDraws + 2 * CellDraws + 2 + EntryDraws + OwnDraws;

// 0x559afc → 0x559510 → 0x55b6f0 with 0x3c (without the particle 0x5fe): the turn D3 with the hero's cell and its
// heading, the C3 19 with the chosen one's key, the target in +0x3a8, the heading toward it and +0x381; the DF does not
// go out with 0x3c. In Lorencia and in Devias.
TEST(HeroSkillsTest, AListedWeaponSkillCastsAtTheChosen)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x3c, Knight, map);
        tested->world.Actors()[ChosenSlot].key = 0x0a1b;
        const CrtRandom expected = Advanced(tested->random, ListedCastDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Clear);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
        EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(ClearBytesOf(tested->sent[1]), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x3c, 0x0a, 0x1b}));
        EXPECT_EQ(tested->Hero().attackPosition, tested->world.Actors()[ChosenSlot].position);
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->Hero().byte381, 1);
        EXPECT_EQ(tested->Movement().weaponSkill.slot, 0x3cu);
        EXPECT_EQ(tested->Movement().weaponSkill.target, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->Movement().lastActionTick, tested->ticks);
    }
}

// 0x55be0d..0x55be49: 0x31 (in Lorencia, Devias and Noria, map 8 for action 0x41) releases the shine 0x5fe over the
// hero, with its owner in the pool and at the right hand's bone (without drawing, the matrix at zero: the model's
// origin, which becomes the hero's position), and the sound rand() % 2 + 0x3c, its only rand(); then the 19 and no DF.
TEST(HeroSkillsTest, AWeaponSkillShinesOnTheHero)
{
    for (const std::int32_t map : {0, 2, 8})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x31, Knight, map);
        tested->world.Actors()[ChosenSlot].key = 0x0c2d;
        CrtRandom expected = Advanced(tested->random, ListedCastDraws - 2);
        const int sound = expected.Next() % 2 + 0x3c;
        expected = Advanced(expected, 2);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{sound});
        const auto& shine = tested->world.Particles().All()[0];
        EXPECT_TRUE(shine.alive);
        EXPECT_EQ(shine.type, 0x5fe);
        EXPECT_EQ(shine.life, 18);
        EXPECT_EQ(shine.typedOwner.kind, Client::Particles::ParticleOwnerKind::Actor);
        EXPECT_EQ(shine.typedOwner.identity, 0u);
        EXPECT_EQ(shine.position, tested->Hero().position);
        EXPECT_EQ(tested->world.PlayerModel().origin, tested->Hero().position);
        EXPECT_FALSE(tested->world.Particles().All()[1].alive);
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(ClearBytesOf(tested->sent[1]), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x31, 0x0c, 0x2d}));
        EXPECT_EQ(tested->Hero().animation.action, map == 8 ? 0x41 : 0x40);
    }
}

// P-C358: the shine 0x5fe of a transformed hero (a model other than 0x20a) cuts at 0x53fdb0 before it is created: its
// scale comes from that type's shared model, which MuRet does not have yet (the transformation rings). Before, the D3
// and the model's 0x4f4a10 (with its skeleton) run as always.
TEST(HeroSkillsTest, TheShineOfATransformedHeroUsesItsSharedModel)
{
    for (const bool full : {false, true})
    {
        const AnyModels library;
        auto tested = AHeroClicking(0x31, Knight);
        tested->models = &library;
        auto& hero = tested->Hero();
        hero.type = 0x28;
        hero.bodyPose.assign(1, Client::Math::BoneTransform(1.0f));
        hero.hands[0].bone = 0;
        hero.bodyPose[0][3] = {3.0f, 4.0f, 5.0f};
        tested->world.PlaceModel(hero.type, {.scale = 2.5f, .origin = {8000.0f, 9000.0f, 100.0f}});
        if (full)
            for (auto& particle : tested->world.Particles().All())
                particle.alive = true;
        const auto used = tested->Use();
        ASSERT_TRUE(used.HasValue()) << used.Error();
        ASSERT_EQ(tested->sent.size(), 2u);
        EXPECT_EQ(ClearBytesOf(tested->sent[1])[2], 0x19);
        EXPECT_TRUE(tested->world.Runtime().lastSoundId == Client::Audio::SoundId{0x3c} ||
                    tested->world.Runtime().lastSoundId == Client::Audio::SoundId{0x3d});
        if (!full)
        {
            const auto& shine = tested->world.Particles().All()[0];
            EXPECT_TRUE(shine.alive);
            EXPECT_EQ(shine.type, 0x5fe);
            EXPECT_EQ(shine.position, hero.position + glm::vec3(7.5f, -290.0f, 12.5f));
            EXPECT_EQ(tested->world.ModelPlacement(hero.type).origin, hero.position);
            EXPECT_EQ(tested->world.ModelPlacement(hero.type).scale, 2.5f);
        }
    }
}

// 0x55bf0c..0x55bf29: within 300 ms of the last send the 19 does not go out, but the D3 and the rest do; with "webzen"
// in the published hero's name, it does not either (0x55be9a).
TEST(HeroSkillsTest, TheBrakeAndTheNameHoldTheMagicAttack)
{
    {
        const auto tested = AHeroClicking(0x3c, Knight);
        tested->Movement().lastActionTick = tested->ticks - 300;
        const CrtRandom expected = Advanced(tested->random, ListedCastDraws - 2);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0])[2], 0xd3);
        EXPECT_EQ(tested->Hero().byte381, 1);
    }
    {
        const auto tested = AHeroClicking(0x3c, Knight, 2);
        const char name[] = "xwebzenx";
        for (std::size_t i = 0; i + 1 < sizeof(name); ++i)
            tested->Hero().name[i] = static_cast<std::byte>(name[i]);
        ASSERT_TRUE(tested->Use().HasValue());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->Movement().lastActionTick, 0u);
    }
}

// 0x559b29..0x559b3b: without mana and without a mana potion, 0x559890 gives 0 and the skill goes on to the class
// branch; with a potion, 0x5af520 sends the C1 26 and the class branch goes on without mana.
TEST(HeroSkillsTest, WithoutManaTheListedSkillLooksForAPotion)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x3d, Knight, map);
        // The knight branch (0x581894) ends: without a right-hand weapon, 0x3d does not go on.
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->sent.empty());
        auto& potion = tested->world.Inventory().cells[12];
        potion.id = 0x1c05;
        potion.occupied = 1;
        tested->Ui().rightClick = true;
        const auto again = tested->Use();
        ASSERT_TRUE(again.HasValue()) << again.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 6, 0x26, 24, 0, 0}));
    }
    // With the own record's mana, 0x3d reaches the 19.
    const auto tested = AHeroClicking(0x3d, Knight);
    tested->world.OwnCharacter().mana = 0x40;
    ASSERT_TRUE(tested->Use().HasValue());
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(ClearBytesOf(tested->sent[1])[3], 0x3d);
}

// 0x5596f2..0x559822: out of reach, PathFinding2's path up to the reach from the hero's logical cells (the Y before the
// X), +0x378 at 1 and +0x379 at 5; nothing goes out. In Blood Castle (0x55969e) 0x13 to 0x17 have a reach of 1.8.
TEST(HeroSkillsTest, AFarChosenGetsAPathForTheSkill)
{
    {
        const auto tested = AHeroClicking(0x3c, Knight, 2);
        tested->world.Actors()[ChosenSlot].position = {18950.0f, 11550.0f, 0.0f};
        const CrtRandom expected =
            Advanced(tested->random, AimDraws + 2 * EntryDraws + 2 * CellDraws + EntryDraws + OwnDraws);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(tested->Hero().movementType, 5);
        EXPECT_GT(tested->Hero().path.count, 0);
    }
    {
        // 0x13 from a weapon option: 4 · 1.2 in Lorencia leaves it in reach (with the shine and the 19), and 1.8 in
        // Blood Castle does not reach the chosen one's 283.
        for (const std::int32_t map : {0, 11})
        {
            SCOPED_TRACE(map);
            const auto tested = AHeroClicking(0x13, Knight, map);
            auto& item = tested->world.OwnCharacter().equipment[0];
            item.optionCount = 1;
            item.optionBytes[0] = 0x13;
            const auto result = tested->Use();
            if (map == 0)
            {
                ASSERT_TRUE(result.HasValue()) << result.Error();
                EXPECT_EQ(tested->Hero().movementType, 0);
                EXPECT_EQ(tested->world.Particles().All()[0].type, 0x5fe);
                continue;
            }
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->Hero().movementType, 5);
        }
    }
}

// 0x55abfa..0x55b45a: the weapon's option 0x12 sets action 0x98 and sends the C3 19 with the published hero's key; if
// 0x558bb0 does not let the chosen one be attacked, the listed 0x3c also goes to the options (0x55a2c0).
TEST(HeroSkillsTest, TheOption12CastsOnTheHeroItself)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClicking(0x12, Knight, map);
        tested->Hero().key = 0x0123;
        auto& item = tested->world.OwnCharacter().equipment[0];
        item.optionCount = 1;
        item.optionBytes[0] = 0x12;
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->Hero().animation.action, 0x98);
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x12, 0x01, 0x23}));
    }
    const auto tested = AHeroClicking(0x3c, Knight);
    tested->world.Actors()[ChosenSlot].byte88 = 0;
    // Then the knight branch (0x581894), which without a right-hand weapon ends without 0x3c.
    const auto result = tested->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested->sent.empty());
}

namespace
{
// An elf with the option `skill` on the right-hand weapon, 0x7758588's chosen one in ChosenSlot and its key.
std::unique_ptr<SkillHero> AnElfWithTheOption(std::uint8_t skill, std::int32_t map = 0)
{
    auto tested = AHeroClicking(skill, Elf, map);
    auto& item = tested->world.OwnCharacter().equipment[0];
    item.optionCount = 1;
    item.optionBytes[0] = skill;
    tested->Movement().weaponSkill.target = static_cast<std::int32_t>(ChosenSlot);
    auto& chosen = tested->world.Actors()[ChosenSlot];
    chosen.key = 0x0a1b;
    chosen.byte370 = 2;
    return tested;
}

// The cost, the delay and the reach (one entry each), the logical cell of the 1E and the C3; the hand's record.
constexpr int ElfShotDraws = AimDraws + 3 * EntryDraws + 2 * CellDraws + 2 + OwnDraws;
} // namespace

// 0x563df6..0x564f37: 0x18 in reach turns the hero toward +0x3a8 and sends the 1E with the logical cell, the heading ·
// 256/360, two zeros and 0x558eb0's key with 0x7758588; 0x5767948 stays on the skill and it was used (0x57df46).
TEST(HeroSkillsTest, TheElfTripleShotSendsA1E)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElfWithTheOption(0x18, map);
        const CrtRandom expected = Advanced(tested->random, ElfShotDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(tested->sent[0].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x18, 182, 115, 0x60, 0, 0, 0x0a, 0x1b, 0}));
        EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0x18u);
    }
}

// 0x56291f..0x563b8f: 0x36 adds the logical cell toward the target (2 and 2: 0xaa) and the back (315 · 255/360); the
// heading is · 255/360. Without a chosen one in 0x7758588, the key is 0xffff.
TEST(HeroSkillsTest, TheElfPenetrationSendsWhereTheTargetIs)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElfWithTheOption(0x36, map);
        tested->Movement().weaponSkill.target = -1;
        const CrtRandom expected = Advanced(tested->random, ElfShotDraws + 2 * CellDraws);
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->random.State(), expected.State());
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]),
                  (std::vector<std::uint8_t>{0xc1, 0x0c, 0x1e, 0x36, 182, 115, 0x5f, 0xaa, 0xdf, 0xff, 0xff, 0}));
    }
}

// 0x562a8a and 0x563eae: with "webzen" in the name the 1E does not go out (and there is no brake), but it was used; out
// of reach (0x56298f, 0x563e66), neither the 1E nor the heading, and it goes on to the class branch, which does not
// walk with a monster.
TEST(HeroSkillsTest, TheElfShotNeedsTheNameAndTheReach)
{
    {
        const auto tested = AnElfWithTheOption(0x18);
        const char name[] = "webzen";
        for (std::size_t i = 0; i + 1 < sizeof(name); ++i)
            tested->Hero().name[i] = static_cast<std::byte>(name[i]);
        const CrtRandom expected = Advanced(tested->random, ElfShotDraws - 2 * CellDraws - 2);
        ASSERT_TRUE(tested->Use().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
    }
    {
        const auto tested = AnElfWithTheOption(0x36, 2);
        tested->world.Actors()[ChosenSlot].position = {18850.0f, 11550.0f, 0.0f};
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// 0x563b8f..0x564f37 with a transformed hero (not the 0x20a; +0x88 at 0): 0x4f4a10 through its model (action 1, the
// 0x28's sound 0x10 and the rand() of the model's sound), +0x38e (one rand() when it closes; the own record stays open
// since 0x561f60) and the arrow of 0x53e2b0(c, c, 0, slot, flag, 0) (P-C269 c): with 0x18 the flag at 1, three shots of
// 0x53d9a0 and the volley's count; with 0x36, flag 2 (0x563df1) and one shot. The shots leave [0x5767948] at skill 0;
// without a bow in the own record, no arrow. Without the model library, the 0x4f4a10 cuts (0x4ea030).
TEST(HeroSkillsTest, TheTransformedElfShootsThroughTheArrowOf53e2b0)
{
    struct Case
    {
        std::uint8_t skill;
        int extraCells;
        std::uint8_t volleys;
    };
    constexpr std::uint16_t TransformedModel = 0x28;
    // 0x4f51f1: the rand() % 2 of the model's attack sound (DrawModelSound), which the 0x20a does not draw.
    constexpr int ModelSoundDraws = 1;
    for (const std::int32_t map : {0, 2})
    {
        for (const Case& probe : {Case{0x18, 0, 1}, Case{0x36, 2, 0}})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", skill " << static_cast<int>(probe.skill));
            const AnyModels library;
            const auto tested = AnElfWithTheOption(probe.skill, map);
            tested->models = &library;
            tested->Hero().type = TransformedModel;
            if (probe.skill == 0x36)
                tested->Movement().weaponSkill.target = -1;
            const CrtRandom expected = Advanced(tested->random, ElfShotDraws + (probe.extraCells * CellDraws) +
                                                                    ModelSoundDraws + Byte38eDraws);

            const auto result = tested->Use();

            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            ASSERT_EQ(tested->sent.size(), 1u);
            EXPECT_EQ(tested->Hero().animation.action, 1);
            EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
            EXPECT_EQ(tested->world.Runtime().lastShotSkill, 0u);
            EXPECT_EQ(tested->world.OwnCharacter().bytef04, probe.volleys);
            // The three shots of flag 1 turn the heading by 15, −30 and 15: it returns to the one of 0x5629b6.
            EXPECT_EQ(tested->Hero().angles.z, 135.0f);
        }
    }
    const auto cut = AnElfWithTheOption(0x36);
    cut->Hero().type = TransformedModel;
    cut->Hero().animation.action = 0;
    const auto result = cut->Use();
    ASSERT_TRUE(result.HasValue()) << result.Error();
    // H3 keeps the no-op of 0x4ea030 when the model record has no actions.
    EXPECT_EQ(cut->Hero().animation.action, 0);
}

// 0x56205f..0x562092: without mana and without a potion, the next option (and the class branch, which without mana or
// potion does not go on, 0x57d821); with one, 0x5af520.
TEST(HeroSkillsTest, TheElfShotWithoutMana)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AnElfWithTheOption(0x18, map);
        tested->skills = SkillsWith({{0x18, 6, 0, 0x10}});
        const auto result = tested->Use();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        auto& potion = tested->world.Inventory().cells[30];
        potion.id = 0x1c04;
        potion.occupied = 1;
        tested->Ui().rightClick = true;
        const auto again = tested->Use();
        ASSERT_TRUE(again.HasValue()) << again.Error();
        ASSERT_EQ(tested->sent.size(), 1u);
        EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 6, 0x26, 42, 0, 0}));
        EXPECT_EQ(tested->world.Runtime().ui.field5767954, 10);
    }
}

// 0x55cdc8 → 0x4f5290 → 0x4ea030: a transformation queries the actions of its shared model.
TEST(HeroSkillsTest, ATransformedWizardUsesItsModelForTheReachedSkill)
{
    auto tested = AHeroClicking(0x11, Wizard);
    AnyModels models;
    tested->models = &models;
    tested->Hero().type = 0x28;
    const auto used = tested->Use();
    ASSERT_TRUE(used.HasValue()) << used.Error();
    EXPECT_EQ(tested->Hero().animation.action, 3);
    EXPECT_EQ(tested->Hero().attackCount, 1);
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(ClearBytesOf(tested->sent[0]), (std::vector<std::uint8_t>{0xc1, 6, 0x19, 0x11, 0, 0}));
    EXPECT_EQ(tested->sent[1].packet.Opcode(), Client::Network::Opcode{0xd3});
}

// MoveHero (0x59e255) keeps the same model library when it enters 0x57cfc0 and 0x564f2f.
TEST(HeroSkillsTest, MovementForwardsTheModelOfTheTransformedElfShot)
{
    auto tested = AnElfWithTheOption(0x18);
    AnyModels models;
    tested->Hero().type = 0x28;
    tested->Hero().animation.action = 0;
    const Client::Session::HeroMovementHooks hooks{};
    Client::Movement::HeroMovementContext context{.world = tested->world, .random = tested->random};
    context.hooks = &hooks;
    context.input.ticksMs = tested->ticks;
    context.input.worldTime = tested->worldTime;
    context.playerSkeleton = &tested->skeleton;
    context.skills = &tested->skills;
    context.sendSkill = [&tested](Client::Protocol::Outgoing packet) { tested->sent.push_back(std::move(packet)); };
    context.models = &models;
    context.terrain = &tested->terrain;
    const auto used = Client::Movement::StepHeroMovement(context);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    EXPECT_EQ(tested->Hero().animation.action, 1);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
    EXPECT_EQ(tested->world.OwnCharacter().bytef04, 1);
    ASSERT_EQ(tested->sent.size(), 1u);
    EXPECT_EQ(tested->sent[0].packet.Opcode(), Client::Network::Opcode{0x1e});
}

// 0x55980f: with Crywolf at stage 3, on map 34 and an altar (0x206) in 0x6c8e54, PathFinding2 sets the A* flag
// (0x4eac0a..0x4eac41), which does not count with a nonzero reach (0x4eadd4): the path, the rand() and +0x378 are the
// same as with another NPC (0x203) chosen.
TEST(HeroSkillsTest, TheWeaponApproachWalksTheSameWithACrywolfAltar)
{
    const auto approach = [](std::uint16_t chosenType)
    {
        auto tested = AHeroClicking(0x3c, Knight, 34);
        tested->world.Actors()[ChosenSlot].position = {19250.0f, 11550.0f, 0.0f};
        tested->world.Runtime().crywolf.stage = 3;
        tested->world.Runtime().chosenNpc.index = 30;
        tested->world.Actors()[30].type = chosenType;
        const auto used = tested->Use();
        EXPECT_TRUE(used.HasValue()) << used.Error();
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
    EXPECT_EQ(altar->random.State(), other->random.State());
    EXPECT_TRUE(altar->sent.empty());
}
