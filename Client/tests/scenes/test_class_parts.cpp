#include "Actors/ClassParts.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Skeleton.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Client::Actors::ApplyTheClassParts: 0x513100, the clothes of the new class from a quest's prize. The values come from
// the ASM checked against PE c32bbd5f; none from a run of the original.
namespace
{
using Client::Actors::ActorStopContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorItemPart;
using Client::Kernel::ActorState;
using Client::Kernel::NoItemModel;
using Client::Tests::UnarmedPlayer;

// The models of the class's clothes (Kernel::ClassPartModel): the helmet, the armor, the pants, the gloves and the
// boots, plus (class >> 3) · 5 + (class & 7).
constexpr std::uint16_t HelmClass = 0x2212, ArmorClass = 0x221a, PantsClass = 0x2222, GlovesClass = 0x222a,
                        BootsClass = 0x2232;
// Class 1 with the second class's bit: the base is 1 and the whole class adds 5 + 1.
constexpr std::uint8_t SecondClass = 0x09;
constexpr std::uint16_t BaseOffset = 1, SecondOffset = 6;
// Outside [0x22, 0x97] and [0xc5, 0xcc]: 0x4f2250 runs. 0x22 and 0xcc are the windows'.
constexpr std::uint8_t StandAction = 1;

// Any item on a part, with a level and bytes +3 and +4.
ActorItemPart Worn(std::uint16_t model)
{
    ActorItemPart part;
    part.model = model;
    part.level = 7;
    part.flags1b = 0x11;
    part.flags1c = 0x22;
    return part;
}

class ClassPartsTest : public ::testing::Test
{
protected:
    Client::Core::Expected<Client::Actors::ActorStopResult, std::string> Apply(ActorState& actor)
    {
        const ActorStopContext context{.runtime = m_runtime,
                                       .scene = Client::Kernel::SceneId::Game,
                                       .playerSkeleton = m_skeleton,
                                       .random = m_random};
        return Client::Actors::ApplyTheClassParts(actor, context);
    }

    // The whole class with all the clothes of its base class, with nothing in the hands.
    static ActorState BaseClothes(std::uint8_t action)
    {
        ActorState actor = UnarmedPlayer(SecondClass);
        actor.animation.action = action;
        actor.body = {Worn(HelmClass + BaseOffset), Worn(ArmorClass + BaseOffset), Worn(PantsClass + BaseOffset),
                      Worn(GlovesClass + BaseOffset), Worn(BootsClass + BaseOffset)};
        return actor;
    }

    Client::Kernel::WorldRuntime m_runtime = []
    {
        Client::Kernel::WorldRuntime runtime;
        runtime.map = 0;
        return runtime;
    }();
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    CrtRandom m_random{1};
};

// 0x513107: with a type other than 0x20a, nothing: no 0x4f2250, no clothes and no 0x512930.
TEST_F(ClassPartsTest, OnlyAPlayerChangesItsClothes)
{
    ActorState actor = BaseClothes(StandAction);
    actor.type = 0x19;
    actor.scale = 3.0f;
    const ActorState before = actor;

    const auto applied = Apply(actor);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_FALSE(applied.Value().sound.has_value());
    EXPECT_EQ(actor.body[0].model, before.body[0].model);
    EXPECT_EQ(actor.animation.action, StandAction);
    EXPECT_EQ(actor.scale, 3.0f);
    EXPECT_EQ(m_random.State(), CrtRandom(1).State());
}

// Q-C1024, 0x513146..0x51324d: with all the base class's clothes, only the helmet compares its own part and goes to the
// whole class's, with the level and bytes +3 and +4 at 0. The armor compares the pants, the pants the gloves, the
// gloves the boots and the boots the right hand: none is the round's base model, and they stay.
TEST_F(ClassPartsTest, WithTheBaseClothesOnlyTheHelmChanges)
{
    ActorState actor = BaseClothes(0x22);
    const ActorState before = actor;

    ASSERT_TRUE(Apply(actor).HasValue());

    EXPECT_EQ(actor.body[0].model, HelmClass + SecondOffset);
    EXPECT_EQ(actor.body[0].level, 0);
    EXPECT_EQ(actor.body[0].flags1b, 0);
    EXPECT_EQ(actor.body[0].flags1c, 0);
    for (std::size_t part = 1; part < actor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(actor.body[part].model, before.body[part].model);
        EXPECT_EQ(actor.body[part].level, 7);
        EXPECT_EQ(actor.body[part].flags1b, 0x11);
        EXPECT_EQ(actor.body[part].flags1c, 0x22);
    }
    // 0x513255, 0x512930: the new helmet is not open (no head) and the scale of class 1 without skin.
    EXPECT_EQ(actor.head, NoItemModel);
    EXPECT_EQ(actor.scale, 0.9f);
    // Action 0x22 is the first window's: no 0x4f2250 and no rand().
    EXPECT_EQ(actor.animation.action, 0x22);
    EXPECT_EQ(m_random.State(), CrtRandom(1).State());
}

// Q-C1024: each part from 1 to 4 changes when the next one (and the right hand, for the boots) has the base model of
// its own, whatever its own is. Each comparison reads the next part before its round writes it.
TEST_F(ClassPartsTest, EachLowerPartComparesTheNextOne)
{
    ActorState actor = UnarmedPlayer(SecondClass);
    actor.animation.action = 0x97;
    actor.body = {Worn(0x300), Worn(0x301), Worn(ArmorClass + BaseOffset), Worn(PantsClass + BaseOffset),
                  Worn(GlovesClass + BaseOffset)};
    actor.hands[0].model = BootsClass + BaseOffset;

    ASSERT_TRUE(Apply(actor).HasValue());

    EXPECT_EQ(actor.body[0].model, 0x300);
    EXPECT_EQ(actor.body[0].level, 7);
    EXPECT_EQ(actor.body[1].model, ArmorClass + SecondOffset);
    EXPECT_EQ(actor.body[2].model, PantsClass + SecondOffset);
    EXPECT_EQ(actor.body[3].model, GlovesClass + SecondOffset);
    EXPECT_EQ(actor.body[4].model, BootsClass + SecondOffset);
    for (std::size_t part = 1; part < actor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(actor.body[part].level, 0);
        EXPECT_EQ(actor.body[part].flags1b, 0);
        EXPECT_EQ(actor.body[part].flags1c, 0);
    }
    EXPECT_EQ(actor.hands[0].model, BootsClass + BaseOffset);
}

// 0x51314a: the signed WORD (MOVSX) against base + (class & 7): a hand without an item (0xffff, −1) is none of them.
TEST_F(ClassPartsTest, AnEmptyRightHandIsNotTheBaseBoots)
{
    ActorState actor = BaseClothes(0xc5);
    actor.body[4] = Worn(0x400);

    ASSERT_TRUE(Apply(actor).HasValue());

    EXPECT_EQ(actor.body[4].model, 0x400);
    EXPECT_EQ(actor.body[4].level, 7);
}

// 0x51311c..0x513135: with the action outside both windows, 0x4f2250 first (the standing action and the suffix's
// rand()). With +0x3e3 other than 0, 0x512930 does not touch the head or the scale, but the clothes change anyway.
TEST_F(ClassPartsTest, AnotherActionStopsFirst)
{
    for (const std::uint8_t action : {std::uint8_t{0x21}, std::uint8_t{0x98}, std::uint8_t{0xc4}, std::uint8_t{0xcd}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        m_random = CrtRandom(1);
        ActorState actor = BaseClothes(action);
        actor.byte3e3 = 1;
        actor.scale = 2.0f;
        actor.head = 0x1234;

        ASSERT_TRUE(Apply(actor).HasValue());

        EXPECT_EQ(actor.animation.action, 1);
        EXPECT_NE(m_random.State(), CrtRandom(1).State());
        EXPECT_EQ(actor.body[0].model, HelmClass + SecondOffset);
        EXPECT_EQ(actor.scale, 2.0f);
        EXPECT_EQ(actor.head, 0x1234);
    }
}

// If 0x4f2250 fails (sitting, +0x7c with 0x200000, without a sender for the C1 18), the clothes stay as they were.
TEST_F(ClassPartsTest, AStopThatCutsLeavesTheClothes)
{
    ActorState actor = BaseClothes(StandAction);
    actor.flags7c = 0x200000;

    const auto applied = Apply(actor);

    ASSERT_FALSE(applied.HasValue());
    EXPECT_EQ(actor.body[0].model, HelmClass + BaseOffset);
    EXPECT_EQ(actor.body[0].level, 7);
}
} // namespace
