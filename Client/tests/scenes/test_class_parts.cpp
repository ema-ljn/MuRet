#include "Actors/ClassParts.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Skeleton.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Scenes/SceneId.h"
#include "Kernel/WorldRuntime.h"

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Client::Actors::ApplyTheClassParts: 0x513100, la ropa de la clase nueva del premio de una misión. Los valores salen
// del ASM cotejado con el PE c32bbd5f; ninguno de una corrida del original.
namespace
{
using Client::Actors::ActorStopContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorItemPart;
using Client::Kernel::ActorState;
using Client::Kernel::NoItemModel;
using Client::Tests::UnarmedPlayer;

// Los modelos de la ropa de la clase (Kernel::ClassPartModel): el casco, la armadura, los pantalones, los guantes y
// las botas, más (clase >> 3) · 5 + (clase & 7).
constexpr std::uint16_t HelmClass = 0x2212, ArmorClass = 0x221a, PantsClass = 0x2222, GlovesClass = 0x222a,
                        BootsClass = 0x2232;
// La clase 1 con el bit de la segunda clase: la base es la 1 y la entera suma 5 + 1.
constexpr std::uint8_t SecondClass = 0x09;
constexpr std::uint16_t BaseOffset = 1, SecondOffset = 6;
// Fuera de [0x22, 0x97] y de [0xc5, 0xcc]: 0x4f2250 corre. 0x22 y 0xcc son de las ventanas.
constexpr std::uint8_t StandAction = 1;

// Un ítem cualquiera en una parte, con nivel y bytes +3 y +4.
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
                                       .scene = Client::Scenes::SceneId::Game,
                                       .playerSkeleton = m_skeleton,
                                       .random = m_random};
        return Client::Actors::ApplyTheClassParts(actor, context);
    }

    // La clase entera con toda la ropa de su clase base, sin nada en las manos.
    static ActorState BaseClothes(std::uint8_t action)
    {
        ActorState actor = UnarmedPlayer(SecondClass);
        actor.animation.action = action;
        actor.body = {Worn(HelmClass + BaseOffset), Worn(ArmorClass + BaseOffset), Worn(PantsClass + BaseOffset),
                      Worn(GlovesClass + BaseOffset), Worn(BootsClass + BaseOffset)};
        return actor;
    }

    Client::Kernel::WorldRuntime m_runtime{.map = 0, .movementSpeed = {}};
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    CrtRandom m_random{1};
};

// 0x513107: con otro tipo que el 0x20a, nada: ni 0x4f2250 ni la ropa ni 0x512930.
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

// Q-C1024, 0x513146..0x51324d: con toda la ropa de la clase base, sólo el casco compara su propia parte y pasa a la de
// la clase entera, con el nivel y los bytes +3 y +4 en 0. La armadura compara los pantalones, los pantalones los
// guantes, los guantes las botas y las botas la mano derecha: ninguno es el modelo base de la vuelta, y quedan.
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
    // 0x513255, 0x512930: el casco nuevo no es abierto (sin cabeza) y la escala de la clase 1 sin piel.
    EXPECT_EQ(actor.head, NoItemModel);
    EXPECT_EQ(actor.scale, 0.9f);
    // La acción 0x22 es de la primera ventana: sin 0x4f2250 ni rand().
    EXPECT_EQ(actor.animation.action, 0x22);
    EXPECT_EQ(m_random.State(), CrtRandom(1).State());
}

// Q-C1024: cada parte de la 1 a la 4 cambia cuando la siguiente (y la mano derecha, para las botas) tiene el modelo
// base de la suya, sea cual sea el suyo. Cada comparación lee la parte siguiente antes de que su vuelta la escriba.
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

// 0x51314a: el WORD con signo (MOVSX) contra base + (clase & 7): una mano sin ítem (0xffff, −1) no es ninguno.
TEST_F(ClassPartsTest, AnEmptyRightHandIsNotTheBaseBoots)
{
    ActorState actor = BaseClothes(0xc5);
    actor.body[4] = Worn(0x400);

    ASSERT_TRUE(Apply(actor).HasValue());

    EXPECT_EQ(actor.body[4].model, 0x400);
    EXPECT_EQ(actor.body[4].level, 7);
}

// 0x51311c..0x513135: con la acción fuera de las dos ventanas, 0x4f2250 primero (la de parado y el rand() del sufijo).
// Con +0x3e3 distinto de 0, 0x512930 no toca la cabeza ni la escala, pero la ropa cambia igual.
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

// Si 0x4f2250 corta (sentado, +0x7c con 0x200000, sin quien mande el C1 18), la ropa queda como estaba.
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
