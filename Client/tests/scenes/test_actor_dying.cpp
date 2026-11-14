#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorDying.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

// Client::Scenes::StepActorDying: 0x4fb9e0, el paso del que se muere (+0x389), con 0x4fb2b0 y 0x51fd20. 06 § Morirse.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorDyingContext;
using Client::Scenes::ActorState;
using Client::Scenes::SceneId;
using Client::Scenes::StepActorDying;

constexpr std::uint32_t Seed = 0x13579bdu;
constexpr std::size_t Slot = 3;

CrtRandom After(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}

ActorState Dying()
{
    ActorState actor;
    actor.alive = true;
    actor.dying = 1;
    actor.position = {1000.0f, 2000.0f, 100.0f};
    actor.alpha = 1.0f;
    return actor;
}

bool CutsAt(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

// 0x4fb9fa..0x4fbb0d: las tres banderas se van, +0x3c8 sube 0,02 y, desde 1, el alpha es 1 − (+0x3c8 − 1): mientras no
// baje de 0,01 la Z baja 0,4; debajo, el que no es el héroe se borra con +0x391, +0x1a1 y +0x1bc en 0. El héroe no se
// borra ni se hunde. Sin +0x389, nada.
TEST(ActorDyingTest, TheTimerFadesSinksAndRemovesAllButTheHero)
{
    CrtRandom random(Seed);
    const ActorDyingContext context{.random = random};

    ActorState fresh = Dying();
    fresh.flags78 = 0x1000 | 0x4000 | 0x20 | 0x8;
    ASSERT_TRUE(StepActorDying(fresh, Slot, context).HasValue());
    EXPECT_EQ(fresh.flags78, 0x8u);
    EXPECT_FLOAT_EQ(fresh.dyingTime, 0.02f);
    EXPECT_EQ(fresh.alpha, 1.0f);
    EXPECT_EQ(fresh.position.z, 100.0f);

    ActorState fading = Dying();
    fading.dyingTime = 1.48f;
    ASSERT_TRUE(StepActorDying(fading, Slot, context).HasValue());
    const auto time = static_cast<float>(static_cast<double>(1.48f) + static_cast<double>(0.02f));
    EXPECT_EQ(fading.dyingTime, time);
    EXPECT_EQ(fading.alpha, static_cast<float>(1.0 - (static_cast<double>(time) - 1.0)));
    EXPECT_EQ(fading.position.z, static_cast<float>(100.0 - static_cast<double>(0.4f)));
    EXPECT_TRUE(fading.alive);

    for (const bool hero : {false, true})
    {
        SCOPED_TRACE(hero);
        ActorState gone = Dying();
        gone.dyingTime = 1.98f;
        gone.deathSkill = 5;
        gone.byte1a1 = 1;
        gone.byte1bc = 1;
        ASSERT_TRUE(StepActorDying(gone, Slot, {.hero = hero, .random = random}).HasValue());
        EXPECT_LT(gone.alpha, 0.01f);
        EXPECT_EQ(gone.position.z, 100.0f);
        EXPECT_EQ(gone.alive, hero);
        EXPECT_EQ(gone.deathSkill, hero ? 5 : 0);
        EXPECT_EQ(gone.byte1a1, hero ? 1 : 0);
        EXPECT_EQ(gone.byte1bc, hero ? 1 : 0);
    }

    ActorState living = Dying();
    living.dying = 0;
    living.flags78 = 0x1000;
    ASSERT_TRUE(StepActorDying(living, Slot, context).HasValue());
    EXPECT_EQ(living.flags78, 0x1000u);
    EXPECT_EQ(living.dyingTime, 0.0f);
    EXPECT_EQ(random.State(), Seed);
}

// 0x4fba46..0x4fba8f: el 0x172 en el mapa 7 sube 0,05; el 0x192 sube 0,01 con +0x60 del objeto en 100 o más (con
// signo) y 0,02 si no, con las banderas ya fuera.
TEST(ActorDyingTest, TheFastOneAndTheSlowOne)
{
    CrtRandom random(Seed);
    for (const std::int32_t map : {7, 2})
    {
        SCOPED_TRACE(map);
        ActorState fast = Dying();
        fast.type = 0x172;
        ASSERT_TRUE(
            StepActorDying(fast, Slot, {.map = map, .scene = SceneId::CharacterSelect, .random = random}).HasValue());
        EXPECT_FLOAT_EQ(fast.dyingTime, map == 7 ? 0.05f : 0.02f);
    }
    for (const std::int32_t level : {100, 250, 99, -5})
    {
        SCOPED_TRACE(level);
        ActorState slow = Dying();
        slow.type = 0x192;
        slow.flags78 = 0x1000;
        slow.dword60 = level;
        ASSERT_TRUE(StepActorDying(slow, Slot, {.random = random}).HasValue());
        EXPECT_EQ(slow.flags78, 0u);
        EXPECT_EQ(slow.dyingTime, level >= 100 ? 0.01f : 0.02f);
    }
    EXPECT_EQ(random.State(), Seed);
}

// 0x4fbb10..0x4fbb45 y 0x4fb2b0: en Blood Castle (11 a 17) con +0x1a1 vuela: (0, +0xd0, 0) girado por +0x1a4 desde
// +0x1b0, +0xd0 más +0xcc, +0xe4 más +0xd8, +0xd8 menos +0xd4, el ángulo X menos 5 y la Z en +0x1b8 + +0xe4.
TEST(ActorDyingTest, TheBloodCastleFlight)
{
    CrtRandom random(Seed);
    ActorState flying = Dying();
    flying.byte1a1 = 1;
    flying.push = {0.0f, 0.0f, 90.0f};
    flying.pushOrigin = {500.0f, 600.0f, 700.0f};
    flying.floatCC = 2.0f;
    flying.floatD0 = 10.0f;
    flying.floatD4 = 1.0f;
    flying.floatD8 = 3.0f;
    flying.floatE4 = 5.0f;
    flying.angles.x = 20.0f;
    const glm::vec3 turned = Client::Render::Models::OriginalVectorRotate(
        {0.0f, 10.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(flying.push));
    ActorState still = flying;
    still.byte1a1 = 0;

    ASSERT_TRUE(StepActorDying(flying, Slot, {.map = 11, .random = random}).HasValue());
    ASSERT_TRUE(StepActorDying(still, Slot, {.map = 11, .random = random}).HasValue());

    EXPECT_EQ(flying.floatD0, 12.0f);
    EXPECT_EQ(flying.floatE4, 8.0f);
    EXPECT_EQ(flying.floatD8, 2.0f);
    EXPECT_EQ(flying.angles.x, 15.0f);
    EXPECT_EQ(flying.position.x, turned.x + 500.0f);
    EXPECT_EQ(flying.position.y, turned.y + 600.0f);
    EXPECT_EQ(flying.position.z, 708.0f);
    EXPECT_EQ(still.position, Dying().position);
    EXPECT_EQ(still.floatD0, 10.0f);
    EXPECT_EQ(random.State(), Seed);
}

// 0x4fbb4a..0x4fbc19: en Chaos Castle (18 a 23) vuela con +0x1a1 y el tope es 15 (25 sin él): de tope − 10 al tope, en
// los pasos impares, las chispas de 0x51fe10 cortan (CL11), con el vuelo hecho.
TEST(ActorDyingTest, ChaosCastleSparksOnTheOddSteps)
{
    CrtRandom random(Seed);
    struct Case
    {
        std::uint8_t byte1a1;
        std::uint8_t dying;
        bool sparks;
    };
    for (const Case& probe :
         {Case{1, 4, false}, Case{1, 5, true}, Case{1, 15, true}, Case{1, 16, false}, Case{1, 17, false},
          Case{0, 14, false}, Case{0, 15, true}, Case{0, 25, true}, Case{0, 27, false}})
    {
        SCOPED_TRACE(::testing::Message() << "+0x1a1 " << int{probe.byte1a1} << ", paso " << int{probe.dying});
        ActorState actor = Dying();
        actor.byte1a1 = probe.byte1a1;
        actor.dying = probe.dying;
        actor.angles.x = 20.0f;
        const auto stepped = StepActorDying(actor, Slot, {.map = 18, .random = random});
        if (probe.sparks)
            EXPECT_TRUE(CutsAt(stepped, "0x4fbba8")) << (stepped.HasValue() ? "" : stepped.Error());
        else
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(actor.angles.x, probe.byte1a1 != 0 ? 15.0f : 20.0f);
    }
}

// 0x4fbc1e..0x4fbcdd y 0x51fd20: fuera de los castillos, con la skill 0x28 o 0x3b y hasta el paso 14, +0xd0 baja
// +0xd8 (sin bajar de 0), +0xd8 sube 1 y la posición suma (+0xcc, +0xd0, +0xd4) girado por la cabeza y +0x30. Con la
// 0x28, hasta el paso 30 las partículas 0x5ac cortan (CL11).
TEST(ActorDyingTest, ThePushedFliesUntilTheFourteenthStep)
{
    CrtRandom random(Seed);
    ActorState pushed = Dying();
    pushed.deathSkill = 0x3b;
    pushed.dying = 14;
    pushed.headTurn.angles = {10.0f, 0.0f};
    pushed.angle30 = 45.0f;
    pushed.floatCC = 1.0f;
    pushed.floatD0 = 10.0f;
    pushed.floatD4 = 2.0f;
    pushed.floatD8 = 4.0f;
    const glm::vec3 moved = Client::Render::Models::OriginalVectorRotate(
        {1.0f, 6.0f, 2.0f}, Client::Render::Models::OriginalAngleMatrix({10.0f, 0.0f, 45.0f}));
    ASSERT_TRUE(StepActorDying(pushed, Slot, {.random = random}).HasValue());
    EXPECT_EQ(pushed.floatD0, 6.0f);
    EXPECT_EQ(pushed.floatD8, 5.0f);
    EXPECT_EQ(pushed.position.x, moved.x + 1000.0f);
    EXPECT_EQ(pushed.position.y, moved.y + 2000.0f);
    EXPECT_EQ(pushed.position.z, moved.z + 100.0f);

    ActorState stopped = Dying();
    stopped.deathSkill = 0x3b;
    stopped.floatD0 = 2.0f;
    stopped.floatD8 = 4.0f;
    ASSERT_TRUE(StepActorDying(stopped, Slot, {.random = random}).HasValue());
    EXPECT_EQ(stopped.floatD0, 0.0f);

    ActorState late = Dying();
    late.deathSkill = 0x3b;
    late.dying = 15;
    late.floatD0 = 10.0f;
    ASSERT_TRUE(StepActorDying(late, Slot, {.random = random}).HasValue());
    EXPECT_EQ(late.floatD0, 10.0f);
    EXPECT_EQ(late.position, Dying().position);

    for (const std::uint8_t dying : {std::uint8_t{15}, std::uint8_t{30}})
    {
        ActorState sparks = Dying();
        sparks.deathSkill = 0x28;
        sparks.dying = dying;
        const auto cut = StepActorDying(sparks, Slot, {.random = random});
        EXPECT_TRUE(CutsAt(cut, "0x4fbcdd")) << (cut.HasValue() ? "" : cut.Error());
    }
    ActorState done = Dying();
    done.deathSkill = 0x28;
    done.dying = 31;
    EXPECT_TRUE(StepActorDying(done, Slot, {.random = random}).HasValue());
    EXPECT_EQ(random.State(), Seed);
}

// 0x4fbd45..0x4fbdf5: en la escena 5 y el mapa 7, cuatro burbujas 0x607/0 (dos rand() cada una al crearse): x e y con
// rand() % 128 − 64 y z con rand() % 256 sobre la posición, con los ángulos y la luz del que se muere. Sin el pool,
// corta.
TEST(ActorDyingTest, AtlansBlowsFourBubbles)
{
    ActorState actor = Dying();
    actor.angles = {0.0f, 0.0f, 30.0f};
    actor.light = {0.25f, 0.5f, 0.75f};
    Client::World::Particles particles;
    CrtRandom random(Seed);
    CrtRandom expected(Seed);
    const auto x = static_cast<float>((expected.Next() & 0x7f) - 0x40);
    const auto y = static_cast<float>((expected.Next() & 0x7f) - 0x40);
    const auto z = static_cast<float>(expected.Next() & 0xff);

    ASSERT_TRUE(StepActorDying(actor, Slot, {.map = 7, .random = random, .particles = &particles}).HasValue());

    EXPECT_EQ(random.State(), After(Seed, 4 * (3 + 2)).State());
    for (std::size_t bubble = 0; bubble < 4; ++bubble)
    {
        EXPECT_TRUE(particles.All()[bubble].alive);
        EXPECT_EQ(particles.All()[bubble].type, Client::World::DropParticle);
        EXPECT_EQ(particles.All()[bubble].angles, actor.angles);
    }
    EXPECT_FALSE(particles.All()[4].alive);
    EXPECT_EQ(particles.All()[0].position, (glm::vec3{x + 1000.0f, y + 2000.0f, z + 100.0f}));

    CrtRandom other(Seed);
    ActorState elsewhere = Dying();
    ASSERT_TRUE(
        StepActorDying(elsewhere, Slot, {.map = 7, .scene = SceneId::CharacterSelect, .random = other}).HasValue());
    EXPECT_EQ(other.State(), Seed);
    const auto cut = StepActorDying(elsewhere, Slot, {.map = 7, .random = other});
    EXPECT_TRUE(CutsAt(cut, "0x4fbdec")) << (cut.HasValue() ? "" : cut.Error());
}
} // namespace
