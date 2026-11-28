#include "Pets/PetStats.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Protocol/Pets/PetInfo.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkSpirit.h"
#include "Actors/FullSetScan.h"
#include "Kernel/DrawTailState.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Player/OwnStatHelpers.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"

#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The incoming pets A9 (0x4e4ca1 → 0x421db0, 0x4221d0, 0x417f30 and 0x420cd0; CL9 milestone 4, 06 § El A9 de las
// mascotas), with invented levels and experience. The rand() counts come from the ASM: each own record access that
// closes it at 0 draws 3848; 0x550d10's part is measured separately on the same equipment.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::PetStats;
using Client::Models::OwnRecordCloseRandoms;
using Client::Pets::DarkSpirit;
using Client::Protocol::Pets::PetInfoReply;

constexpr std::uint32_t Seed = 0x2468ace0u;
constexpr std::size_t HeroIndex = 6;
// +0x36 + +0x20 of the own record: the leadership with what is added.
constexpr std::uint16_t Leadership = 900;
constexpr std::uint16_t Word36 = 100;
constexpr std::uint8_t RavenDurability = 200;
constexpr std::uint16_t DarkRavenItem = 0x1a05;
constexpr std::uint16_t DarkHorseItem = 0x1a04;
constexpr std::uint8_t HorseDurability = 150;

int DrawsBetween(CrtRandom from, std::uint32_t to)
{
    for (int draws = 0; draws <= 200'000; ++draws)
    {
        if (from.State() == to)
            return draws;
        static_cast<void>(from.Next());
    }
    return -1;
}

Client::Tables::ItemTable EmptyItems()
{
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(
        std::vector<std::byte>(Client::Tables::ItemTableSize), Client::Tables::ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

Client::Tables::ItemSetTables EmptySets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// A published Dark Lord with its raven, the empty equipment with the raven's and the horse's durabilities, nothing in
// the tooltip and what the last 0x505820 left.
struct Pets
{
    Pets()
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.hero = HeroIndex;
        runtime.fullSetScan = Client::Actors::Scan505820{};
        runtime.hoveredItemCopy.id = 0xffff;
        ActorState lord = Client::Tests::UnarmedPlayer(4);
        lord.alive = true;
        lord.darkSpirit.emplace();
        world.Actors()[HeroIndex] = lord;
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        Client::Kernel::ItemState empty;
        empty.id = 0xffff;
        own.equipment.fill(empty);
        own.equipment[1].id = DarkRavenItem;
        own.equipment[1].durability = RavenDurability;
        own.equipment[8].id = DarkHorseItem;
        own.equipment[8].durability = HorseDurability;
        own.leadership = Leadership;
        own.word36 = Word36;
        own.strength = 300;
        own.word2a = 20;
        own.agility = 60;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = sets,
                .log = log};
    }
    Client::Core::Expected<void, std::string> Reply(const PetInfoReply& reply)
    {
        return Client::Pets::ApplyPetInfo(reply, Resources());
    }
    DarkSpirit& Raven()
    {
        return *world.Actors()[HeroIndex].darkSpirit;
    }
    PetStats& Pet(std::size_t index)
    {
        return world.Runtime().pets[index];
    }
    int Draws() const
    {
        return DrawsBetween(CrtRandom(Seed), cloth.random.State());
    }
    // 0x4e4cbd..0x4e4ce7, what is not the A9's: the access of 0x4e79a0/0x40f370 and 0x550d10 (ported and tested
    // separately), measured on this same equipment.
    int RecalculationDraws()
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        const CrtRandom before = cloth.random;
        {
            const Client::Models::LogicalAccess access(runtime.ownRecordDepth, cloth.random, OwnRecordCloseRandoms);
            const auto recalculated = Client::Player::RecalculateOwnStats(
                world.OwnCharacter(), Client::Player::OwnStatsContext{.itemSetTables = sets,
                                                                      .itemSets = &world.ItemSets(),
                                                                      .items = &items,
                                                                      .publishedHero = world.Hero(),
                                                                      .ownRecordAccesses = runtime.ownRecordDepth,
                                                                      .random = cloth.random,
                                                                      .held = Client::Player::HeldItemOf(runtime),
                                                                      .scan505820 = runtime.fullSetScan});
            EXPECT_TRUE(recalculated.HasValue()) << recalculated.Error();
        }
        return DrawsBetween(before, cloth.random.State());
    }

    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::FrontEnd::SingleModelLibrary models;
    const Client::Tables::ItemTable items = EmptyItems();
    const Client::Tables::ItemSetTables setTables = EmptySets();
    const Client::Tables::ItemSetTables* sets = &setTables;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// 0x417f30's values with level 7 and leadership 1000: (7 + 11) · 8³ · 100, (7 · 3 + 36) · 5 + 1000 / 8,
// 1000 / 4 + 200 + 7 · 15, 20 + 1000 / 50 + 28 / 5 and 7 · 16 + 1000.
void ExpectTheRavenOfLevelSeven(const DarkSpirit& raven, std::uint32_t experience)
{
    EXPECT_EQ(raven.word1e2, 7);
    EXPECT_EQ(raven.level, 7);
    EXPECT_EQ(raven.experience, experience);
    EXPECT_EQ(raven.nextExperience, 921'600u);
    EXPECT_EQ(raven.durability, RavenDurability);
    EXPECT_EQ(raven.word454, 410);
    EXPECT_EQ(raven.word456, 555);
    EXPECT_EQ(raven.word458, 45);
    EXPECT_EQ(raven.word45a, 1112);
}

// 0x421ef2..0x421fa1: with the window closed and nothing in the tooltip, the table does not change level and 0x4221d0
// recalculates it with 0; the raven's class 0 in slot 1 fills the hero's raven (0x417f30). Three accesses and 0x550d10.
TEST(PetStatsTest, TheRavenReplyFillsTheRavenOfTheHero)
{
    auto tested = std::make_unique<Pets>();
    const auto applied = tested->Reply({.pet = 0, .kind = 0, .slot = 1, .level = 7, .experience = 5000});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ExpectTheRavenOfLevelSeven(tested->Raven(), 5000);
    const PetStats& table = tested->Pet(0);
    EXPECT_EQ(table.level, 0);
    EXPECT_EQ(table.experience, 0u);
    EXPECT_EQ(table.nextExperience, 1100u);
    EXPECT_EQ(table.word0c, 12 * 15 + 125);
    EXPECT_EQ(table.word0e, 250 + 200);
    EXPECT_EQ(table.word10, 40);
    EXPECT_EQ(table.word12, 1000);
    EXPECT_EQ(tested->Draws(), 3 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

// 0x421f62..0x421f6a: the hero's raven only with slot 1, and a class other than 0, 0xfe and 0xff does not touch it.
TEST(PetStatsTest, TheRavenOfTheHeroNeedsItsSlotAndAPlainKind)
{
    for (const PetInfoReply reply : {PetInfoReply{.pet = 0, .kind = 0, .slot = 8, .level = 7, .experience = 1},
                                     PetInfoReply{.pet = 0, .kind = 1, .slot = 1, .level = 7, .experience = 1}})
    {
        auto tested = std::make_unique<Pets>();
        ASSERT_TRUE(tested->Reply(reply).HasValue());
        EXPECT_EQ(tested->Raven().level, 0);
        EXPECT_EQ(tested->Draws(), OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
    }
}

// 0x421ef2..0x421f40: with an item in the tooltip, the table takes its level, durability and experience, except with
// the trainer open and an equipment slot (below 12); type 2 writes the third and 0x4221d0 does not calculate it.
TEST(PetStatsTest, TheHoveredItemWritesTheTableUnlessTheTrainerHasTheEquipment)
{
    for (const std::uint8_t slot : {std::uint8_t{1}, std::uint8_t{12}})
    {
        for (const std::int32_t trainer : {0, 1})
        {
            auto tested = std::make_unique<Pets>();
            tested->world.Runtime().hoveredItemCopy.id = 0x1a05;
            tested->world.Runtime().hoveredItemCopy.durability = 77;
            tested->world.Runtime().panels.delegated.trainer = trainer;
            ASSERT_TRUE(tested->Reply({.pet = 0, .kind = 9, .slot = slot, .level = 7, .experience = 42}).HasValue());
            const bool written = trainer == 0 || slot >= 12;
            const PetStats& table = tested->Pet(0);
            EXPECT_EQ(table.level, written ? 7 : 0) << int{slot} << " " << trainer;
            EXPECT_EQ(table.durability, written ? 77 : 0);
            EXPECT_EQ(table.experience, written ? 42u : 0u);
            EXPECT_EQ(table.nextExperience, written ? 921'600u : 1100u);
            EXPECT_EQ(table.word0c, written ? 410 : 305);
        }
    }
    auto third = std::make_unique<Pets>();
    third->world.Runtime().hoveredItemCopy.id = 0x1a04;
    ASSERT_TRUE(third->Reply({.pet = 2, .kind = 0, .slot = 20, .level = 3, .experience = 9}).HasValue());
    EXPECT_EQ(third->Pet(2).level, 3);
    EXPECT_EQ(third->Pet(2).nextExperience, 0u);
}

// Q-C1208: a type of 3 or more that the original writes outside 0x723960 (0x421dfd with the window open, 0x421f2c with
// an item in the tooltip) is discarded whole, without rand(), the raven or 0x550d10, and the session goes on. Without
// writing (nothing in the tooltip, or with the trainer and an equipment slot), 0x4221d0 only does its access and
// 0x550d10 follows.
TEST(PetStatsTest, APetBeyondTheThirdIsDiscardedWhenItWrites)
{
    for (const std::uint8_t pet : {std::uint8_t{3}, std::uint8_t{0xff}})
    {
        SCOPED_TRACE(int{pet});
        auto hovered = std::make_unique<Pets>();
        hovered->world.Runtime().hoveredItemCopy.id = 0x1a05;
        ASSERT_TRUE(hovered->Reply({.pet = pet, .kind = 0, .slot = 20, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(hovered->Draws(), 0);
        EXPECT_EQ(hovered->world.Runtime().ownRecordDepth, 0);

        auto open = std::make_unique<Pets>();
        open->world.Runtime().panels.delegated.pet = 2;
        ASSERT_TRUE(open->Reply({.pet = pet, .kind = 0, .slot = 1, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(open->Draws(), 0);
        EXPECT_EQ(open->Raven().level, 0);

        auto quiet = std::make_unique<Pets>();
        ASSERT_TRUE(quiet->Reply({.pet = pet, .kind = 0, .slot = 20, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(quiet->Draws(), OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());

        auto trainer = std::make_unique<Pets>();
        trainer->world.Runtime().hoveredItemCopy.id = 0x1a05;
        trainer->world.Runtime().panels.delegated.trainer = 1;
        ASSERT_TRUE(trainer->Reply({.pet = pet, .kind = 0, .slot = 1, .level = 3, .experience = 9}).HasValue());
        EXPECT_EQ(trainer->Draws(), OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
        for (const PetStats& entry : trainer->world.Runtime().pets)
            EXPECT_EQ(entry.level, 0);
    }
}

// 0x421f8f..0x421fae: with 0xfe, 0x418390 releases five 0x60f/0 of key 2 and width 40 from the raven's position, in
// place and with the angles at 0; with 0xff, 0x418400 releases fifteen toward its position, with its angles. The raven
// is the owner (its light, +0xf4). Three rand() each, between 0x417f30 and 0x4221d0. The "/2" of 0x421f9a and
// 0x421fae is the key, not the subtype.
TEST(PetStatsTest, TheRavenSparksItsOrderJointsAfterItsInfo)
{
    using Client::Kernel::JointState;
    for (const auto& [kind, count] :
         {std::pair<std::uint8_t, int>{std::uint8_t{0xfe}, 5}, std::pair<std::uint8_t, int>{std::uint8_t{0xff}, 15}})
    {
        SCOPED_TRACE(int{kind});
        auto tested = std::make_unique<Pets>();
        DarkSpirit& raven = tested->Raven();
        raven.position = {12'300.0f, 4'500.0f, 260.0f};
        raven.angles = {5.0f, 0.0f, 135.0f};
        raven.light = {0.25f, 0.5f, 0.75f};
        const auto applied = tested->Reply({.pet = 0, .kind = kind, .slot = 1, .level = 7, .experience = 5000});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ExpectTheRavenOfLevelSeven(raven, 5000);
        const bool turned = kind == 0xff;
        const std::span<const JointState> joints = tested->world.DrawTail().joints;
        for (int index = 0; index < count; ++index)
        {
            SCOPED_TRACE(index);
            const JointState& joint = joints[static_cast<std::size_t>(index)];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x60f);
            EXPECT_EQ(joint.subtype, 0);
            EXPECT_EQ(joint.key, 2);
            EXPECT_EQ(joint.width, 40.0f);
            EXPECT_EQ(joint.life, 50);
            EXPECT_EQ(joint.ownerContext, &raven);
            EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::Raven);
            EXPECT_EQ(joint.light, raven.light);
            EXPECT_EQ(joint.position, raven.position);
            EXPECT_EQ(joint.targetPosition, raven.position);
            EXPECT_EQ(joint.angles, turned ? raven.angles : glm::vec3(0.0f));
        }
        EXPECT_FALSE(joints[static_cast<std::size_t>(count)].alive);
        EXPECT_EQ(tested->Draws(),
                  3 * OwnRecordCloseRandoms + 3 * count + std::make_unique<Pets>()->RecalculationDraws());
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
    }
}

// With the 500 joints alive, none, without rand(), and the A9 goes on to 0x4221d0.
TEST(PetStatsTest, TheOrderJointsOfTheRavenWithAFullTableAreNone)
{
    auto tested = std::make_unique<Pets>();
    for (Client::Kernel::JointState& joint : tested->world.DrawTail().joints)
        joint.alive = true;
    const auto applied = tested->Reply({.pet = 0, .kind = 0xff, .slot = 1, .level = 7, .experience = 5000});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ExpectTheRavenOfLevelSeven(tested->Raven(), 5000);
    EXPECT_EQ(tested->world.DrawTail().joints[0].type, 0);
    EXPECT_EQ(tested->Draws(), 3 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
}

// 0x421fb6..0x422196: the horse in slot 8 takes the level, the own record's durability and the experience; with 0xfe
// and 0xff, the hero's +8 at 1 and 2; 0x4221d0(1) with the strength and +0x2a, and the Dark Horse's 0x96 option
// (0x4225d0) with the record open: 60 / 20 + 2 · 10 + 5.
TEST(PetStatsTest, TheHorseReplyWritesItsStatsTheRiderAndTheOption)
{
    for (const std::uint8_t kind : {std::uint8_t{0}, std::uint8_t{0xfe}, std::uint8_t{0xff}})
    {
        auto tested = std::make_unique<Pets>();
        Client::Kernel::ItemState& horseItem = tested->world.OwnCharacter().equipment[8];
        const auto applied = tested->Reply({.pet = 1, .kind = kind, .slot = 8, .level = 10, .experience = 777});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const PetStats& horse = tested->Pet(1);
        EXPECT_EQ(horse.level, 10);
        EXPECT_EQ(horse.durability, HorseDurability);
        EXPECT_EQ(horse.experience, 777u);
        EXPECT_EQ(horse.nextExperience, 2'795'100u);
        EXPECT_EQ(horse.word0c, 182);
        EXPECT_EQ(horse.word0e, 273);
        EXPECT_EQ(horse.word10, 48);
        EXPECT_EQ(horse.word12, 1160);
        EXPECT_EQ(tested->world.Actors()[HeroIndex].byte8, kind == 0 ? 0 : (kind == 0xfe ? 1 : 2));
        ASSERT_EQ(horseItem.optionCount, 1);
        EXPECT_EQ(horseItem.optionBytes[0], 0x96);
        EXPECT_EQ(horseItem.optionBytes[Client::Kernel::ItemOptionValueOffset], 3 + 20 + 5);
        EXPECT_EQ(tested->Raven().level, 0);
    }
}

// 0x421dc7..0x421ead: with the pet window open (0x420cc0), the hero's raven with type 0, whatever the slot or class,
// and the table with the own record's horse durability. Four accesses and 0x550d10.
TEST(PetStatsTest, TheOpenWindowFillsTheRavenAndTheTableWithTheHorseDurability)
{
    auto tested = std::make_unique<Pets>();
    tested->world.Runtime().panels.delegated.pet = 2;
    const auto applied = tested->Reply({.pet = 0, .kind = 0xfe, .slot = 5, .level = 7, .experience = 5000});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ExpectTheRavenOfLevelSeven(tested->Raven(), 5000);
    const PetStats& table = tested->Pet(0);
    EXPECT_EQ(table.level, 7);
    EXPECT_EQ(table.durability, HorseDurability);
    EXPECT_EQ(table.experience, 5000u);
    EXPECT_EQ(table.word0c, 410);
    EXPECT_EQ(table.word0e, 555);
    EXPECT_EQ(table.word10, 45);
    EXPECT_EQ(table.word12, 1112);
    EXPECT_EQ(tested->Draws(), 4 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());

    auto horse = std::make_unique<Pets>();
    horse->world.Runtime().panels.delegated.pet = 2;
    ASSERT_TRUE(horse->Reply({.pet = 1, .kind = 0xff, .slot = 8, .level = 10, .experience = 1}).HasValue());
    EXPECT_EQ(horse->Pet(1).level, 10);
    EXPECT_EQ(horse->Raven().level, 0);
    EXPECT_EQ(horse->world.Actors()[HeroIndex].byte8, 0) << "la ventana abierta no toca al jinete";
    EXPECT_EQ(horse->Draws(), 2 * OwnRecordCloseRandoms + std::make_unique<Pets>()->RecalculationDraws());
}

// 0x4e4cd6: without the set tables, 0x550d10 stops at 0x40e1b0 after the table, with the record closed.
TEST(PetStatsTest, WithoutTheSetTablesTheRecalculationCuts)
{
    auto tested = std::make_unique<Pets>();
    tested->sets = nullptr;
    tested->world.Runtime().panels.delegated.pet = 2;
    const auto cut = tested->Reply({.pet = 0, .kind = 0, .slot = 1, .level = 7, .experience = 5000});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x40e1b0"), std::string::npos) << cut.Error();
    EXPECT_EQ(tested->Pet(0).level, 7);
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

// 0x420cd0: the tab, 0x723911 and the three pets at 0, without rand().
TEST(PetStatsTest, ResettingThePetsZeroesTheTableTheTabAndTheWindow)
{
    const auto owned = std::make_unique<Client::Kernel::WorldRuntime>();
    Client::Kernel::WorldRuntime& runtime = *owned;
    runtime.petTab = 1;
    runtime.panels.delegated.pet = 2;
    runtime.pets[2].experience = 5;
    runtime.pets[0].word12 = 9;
    Client::Pets::ResetThePets(runtime);
    EXPECT_EQ(runtime.petTab, 0);
    EXPECT_EQ(runtime.panels.delegated.pet, 0);
    EXPECT_EQ(runtime.pets[2].experience, 0u);
    EXPECT_EQ(runtime.pets[0].word12, 0);
}
} // namespace
