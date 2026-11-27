#include "Kernel/WorldState.h"

#include "Crt/CrtRandom.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/ItemState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Client::Kernel::WorldState: what WinMain zeroes and lasts from one scene to another (06, "Los personajes de la
// lista"; 04, the own record).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Protocol::FrontEnd::CharacterEntry;

constexpr std::uint32_t Seed = 0x0badf00du;

// The state of the CRT's rand() seeded with Seed after `draws` calls.
std::uint32_t StateAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random.State();
}

// One owner: copying or moving it would give another copy of the pool's history.
static_assert(!std::is_copy_constructible_v<WorldState>);
static_assert(!std::is_copy_assignable_v<WorldState>);
static_assert(!std::is_move_constructible_v<WorldState>);
static_assert(!std::is_move_assignable_v<WorldState>);

// WinMain's REP STOSD (0x4bafa6..0x4bafb3): 401 zeroed records, including the one outside the 400 that the drawing
// and the picking walk.
TEST(WorldStateTest, StartsWith401ZeroedRecords)
{
    const WorldState world;
    ASSERT_EQ(world.Actors().size(), 401u);
    for (std::size_t index = 0; index < world.Actors().size(); ++index)
    {
        SCOPED_TRACE(index);
        const Client::Kernel::ActorState& actor = world.Actors()[index];
        EXPECT_EQ(actor.position.x, 0.0f);
        EXPECT_EQ(actor.position.y, 0.0f);
        EXPECT_EQ(actor.position.z, 0.0f);
        EXPECT_FALSE(actor.previousHad1c15);
        EXPECT_EQ(actor.revision, 0u);
    }
}

// WinMain zeroes the own record (04, 0x4bafbc..0x4bafc2): the type of the twelve places starts at 0, not at −1. Only
// the F3 03 reply sets the −1 (06, row 7 of 0x4c3940).
TEST(WorldStateTest, TheOwnRecordStartsZeroed)
{
    const WorldState world;
    const Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    EXPECT_EQ(own.name, (std::array<std::byte, 11>{}));
    EXPECT_EQ(own.characterClass, 0);
    EXPECT_EQ(own.skin, 0);
    EXPECT_EQ(own.level, 0);
    EXPECT_EQ(own.zen, 0u);
    ASSERT_EQ(own.equipment.size(), 12u);
    for (const Client::Kernel::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0);
        EXPECT_EQ(item.byte0a, 0);
        EXPECT_EQ(item.optionCount, 0);
    }
}

// The sets' object, [0x6eed3c], is a static: it starts at zero, and what 0x40e1b0 and 0x40da20 write stays from one
// call to the next (06, "Los stats del registro propio").
TEST(WorldStateTest, TheItemSetStateStartsZeroedAndStays)
{
    WorldState world;
    const WorldState& shown = world;
    EXPECT_EQ(shown.ItemSets().byte9307, 0);
    EXPECT_EQ(shown.ItemSets().list.count9348, 0);
    EXPECT_EQ(shown.ItemSets().names[0][0], '\0');
    EXPECT_EQ(shown.ItemSets().list.used, (std::array<std::uint8_t, Client::Player::ItemSetOptionCapacity>{}));

    world.ItemSets().list.count9348 = 2;
    world.ItemSets().list.used[3] = 1;

    EXPECT_EQ(shown.ItemSets().list.count9348, 2);
    EXPECT_EQ(shown.ItemSets().list.used[3], 1);
}

// 06, "La entrada al mundo", step 1: on going to scene 3 the selection opens the own record, copies the chosen one's
// level, class and skin 0 into it and the name with a strcpy, which keeps what follows the zero (0x634433..0x63444e);
// the closing spends 3,848 rand() calls if it leaves it with no access (0x40f370, 0x40b4d0). Then it clears the world
// load's flag (0x633efe, 0x634469, 0x634631).
TEST(WorldStateTest, PreparingTheChosenCopiesItIntoTheOwnRecord)
{
    struct Case
    {
        std::uint8_t depth;
        int draws;
    };
    for (const Case& probe : {Case{0, Client::Models::OwnRecordCloseRandoms}, Case{1, 0}})
    {
        SCOPED_TRACE(static_cast<int>(probe.depth));
        WorldState world;
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.name.fill(std::byte{0x7a});
        own.skin = 5;
        own.characterClass = 9;
        world.Runtime().loadingOnceFlag = 1;
        world.Runtime().ownRecordDepth = probe.depth;
        CharacterEntry chosen;
        chosen.name[0] = std::byte{0x61};
        chosen.name[1] = std::byte{0x62};
        chosen.name[2] = std::byte{0x63};
        chosen.level = 0x0123;
        chosen.classByte = 0x30;
        CrtRandom random(Seed);

        world.PrepareSelectedActor(chosen, random);

        EXPECT_EQ(own.name,
                  (std::array<std::byte, 11>{std::byte{0x61}, std::byte{0x62}, std::byte{0x63}, std::byte{0},
                                             std::byte{0x7a}, std::byte{0x7a}, std::byte{0x7a}, std::byte{0x7a},
                                             std::byte{0x7a}, std::byte{0x7a}, std::byte{0x7a}}));
        EXPECT_EQ(own.level, 0x0123);
        EXPECT_EQ(own.characterClass, Client::Protocol::FrontEnd::CharacterClassOf(0x30));
        EXPECT_EQ(own.skin, 0);
        EXPECT_EQ(world.Runtime().loadingOnceFlag, 0);
        EXPECT_EQ(world.Runtime().ownRecordDepth, probe.depth);
        EXPECT_EQ(random.State(), StateAfter(probe.draws));
    }
}

// A ten-byte name arrives with no zero in the list: the strcpy reads +0x1e3's zero and writes it in the record's last
// byte.
TEST(WorldStateTest, AFullNameIsTerminatedInTheOwnRecord)
{
    WorldState world;
    world.OwnCharacter().name.fill(std::byte{0x7a});
    CharacterEntry chosen;
    chosen.name.fill(std::byte{0x6d});
    CrtRandom random(Seed);

    world.PrepareSelectedActor(chosen, random);

    std::array<std::byte, 11> expected{};
    expected.fill(std::byte{0x6d});
    expected[10] = std::byte{0};
    EXPECT_EQ(world.OwnCharacter().name, expected);
}
} // namespace
