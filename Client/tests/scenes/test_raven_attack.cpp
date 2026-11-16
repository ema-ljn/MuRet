#include "Scenes/DarkSpirit.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>

// Los estados del ataque del cuervo en 0x416a20 (CL9 hito 4, 06 § El ataque del cuervo): el aviso con el A7, el 5,
// el 2, el 6, el contador +0x38d y el vuelo hacia el blanco de +8. *Estático*, desde el ASM: el código real emulado
// (tests/vectors/world55/raven.txt) sólo tiene los estados 0, 1, 3 y 4.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::DarkSpirit;
using Client::Scenes::DarkSpiritOwner;
using Client::Scenes::DarkSpiritTargets;

constexpr std::uint8_t Lorencia = 0;

Client::Render::Models::Skeleton RavenSkeleton()
{
    Client::Render::Models::Skeleton skeleton;
    skeleton.actions.assign(4, Client::Render::Models::ActionInfo{.repeats = true, .keyCount = 4});
    return skeleton;
}

struct Raven
{
    Raven()
    {
        spirit.position = {1000.0f, 1000.0f, 300.0f};
        owner.position = {1000.0f, 1100.0f, 200.0f};
    }
    Client::Scenes::DarkSpiritStepped Step(std::uint8_t map = Lorencia, const DarkSpiritTargets& targets = {})
    {
        return Client::Scenes::StepDarkSpirit(spirit, skeleton, owner, map, *particles, 0.0f, random, 1, targets);
    }

    DarkSpirit spirit;
    DarkSpiritOwner owner;
    Client::Render::Models::Skeleton skeleton = RavenSkeleton();
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    CrtRandom random;
};

// 0x416a88..0x4176e6: con el dueño escondido, en el 2, el 5 y el 6, el aviso: +0x3b0 en 0xffff, el A7 y el estado 4
// con la velocidad en 3. En la zona segura sigue bajando (Land acelera 1); fuera, la zona lo vuelve al 0.
TEST(RavenAttackTest, AHiddenOwnerStopsTheAttack)
{
    for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{5}, std::uint8_t{6}})
    {
        SCOPED_TRACE(int{state});
        Raven tested;
        tested.owner.flags = 0x4000;
        tested.owner.safeZone = 1;
        tested.spirit.state = state;
        tested.spirit.target3b0 = 9;
        tested.spirit.far = 7;
        const auto stepped = tested.Step();
        EXPECT_TRUE(stepped.stopsTheAttack);
        EXPECT_EQ(tested.spirit.target3b0, 0xffff);
        EXPECT_EQ(tested.spirit.state, 4);
        EXPECT_EQ(tested.spirit.speed, 4.0f);

        Raven outside;
        outside.owner.flags = 0x4000;
        outside.spirit.state = state;
        EXPECT_TRUE(outside.Step().stopsTheAttack);
        EXPECT_LE(outside.spirit.state, 1);
    }
}

// El blanco de +0x3b0 escondido corta igual, pero sólo si está entre los 400; los estados 0, 1, 3 y 4 no avisan.
TEST(RavenAttackTest, AHiddenTargetStopsTheAttackOnlyInsideThePool)
{
    Raven inPool;
    inPool.spirit.state = 5;
    EXPECT_TRUE(inPool.Step(Lorencia, {.attackInPool = true, .attackFlags = 0x4000}).stopsTheAttack);
    Raven outOfPool;
    outOfPool.spirit.state = 5;
    EXPECT_FALSE(outOfPool.Step(Lorencia, {.attackInPool = false, .attackFlags = 0x4000}).stopsTheAttack);
    for (const std::uint8_t state : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{4}})
    {
        Raven resting;
        resting.owner.flags = 0x4000;
        resting.spirit.state = state;
        EXPECT_FALSE(resting.Step().stopsTheAttack) << int{state};
    }
}

// 0x4179ef..0x417ae3: el 5 avanza (0, −velocidad, 0) girado, gira hacia el blanco de +8 50 más arriba, sube la
// lejanía, la velocidad suma +0xf0 y +0xf0 suma 0,2; la acción 3 y +0x38d sube. Sin rand().
TEST(RavenAttackTest, TheAttackChargesTowardTheTarget)
{
    Raven tested;
    tested.spirit.state = 5;
    tested.spirit.byte1ad = 1;
    tested.spirit.speed = 20.0f;
    tested.spirit.floatF0 = 0.5f;
    tested.spirit.far = 3;
    const DarkSpiritTargets targets{.target = {1000.0f, 400.0f, 100.0f}};
    glm::vec3 expectedAngles{};
    const double distance =
        Client::Render::Models::MoveToward({1000.0f, 980.0f, 300.0f}, expectedAngles, {1000.0f, 400.0f, 150.0f}, 20.0f);
    ASSERT_GE(distance, 20.0);

    const auto stepped = tested.Step(Lorencia, targets);

    EXPECT_FALSE(stepped.stopsTheAttack);
    EXPECT_EQ(tested.spirit.state, 5);
    EXPECT_EQ(tested.spirit.position, glm::vec3(1000.0f, 980.0f, 300.0f));
    EXPECT_EQ(tested.spirit.angles, expectedAngles);
    EXPECT_EQ(tested.spirit.far, 4);
    EXPECT_EQ(tested.spirit.speed, 20.5f);
    EXPECT_EQ(tested.spirit.floatF0, static_cast<float>(0.5 + static_cast<double>(0.2f)));
    EXPECT_EQ(tested.spirit.animation.action, 3);
    EXPECT_EQ(tested.spirit.byte38d, 1);
    EXPECT_EQ(tested.spirit.byte1ad, 1);
    // Sólo el rand() del grito.
    CrtRandom expected;
    static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x417a74..0x417aa6: a menos de 20 vuelve (2) con el cabeceo en −45 y la lejanía en 1; +0x1ad en 0 salvo con la
// orden 3. Con la lejanía en más de 20, también.
TEST(RavenAttackTest, TheAttackReturnsNearTheTargetOrAfterTwentySteps)
{
    for (const std::uint8_t order : {std::uint8_t{0}, std::uint8_t{3}})
    {
        Raven tested;
        tested.spirit.state = 5;
        tested.spirit.byte1ad = 1;
        tested.spirit.order = order;
        tested.spirit.speed = 5.0f;
        tested.Step(Lorencia, {.target = {1000.0f, 995.0f, 250.0f}});
        EXPECT_EQ(tested.spirit.state, 2);
        EXPECT_EQ(tested.spirit.angles.x, -45.0f);
        EXPECT_EQ(tested.spirit.far, 1);
        EXPECT_EQ(tested.spirit.byte1ad, order == 3 ? 1 : 0);
    }
    Raven tired;
    tired.spirit.state = 5;
    tired.spirit.far = 21;
    tired.Step(Lorencia, {.target = {1000.0f, 0.0f, 0.0f}});
    EXPECT_EQ(tired.spirit.state, 2);
    Raven fresh;
    fresh.spirit.state = 5;
    fresh.spirit.far = 20;
    fresh.Step(Lorencia, {.target = {1000.0f, 0.0f, 0.0f}});
    EXPECT_EQ(fresh.spirit.state, 5);
}

// 0x417aec..0x417b26: volviendo, a 250 o más del destino (antes de avanzar) pasa al 0; frena 1 y la acción 3.
TEST(RavenAttackTest, TheReturnFlightBrakesUntilItIsFar)
{
    Raven near;
    near.spirit.state = 2;
    near.spirit.speed = 10.0f;
    near.Step();
    EXPECT_EQ(near.spirit.state, 2);
    EXPECT_EQ(near.spirit.speed, 9.0f);
    EXPECT_EQ(near.spirit.animation.action, 3);
    EXPECT_EQ(near.spirit.byte38d, 0);

    Raven far;
    far.spirit.state = 2;
    far.owner.position = {1000.0f, 1250.0f, 200.0f};
    far.Step();
    EXPECT_EQ(far.spirit.state, 0);
}

// 0x417847..0x41789d: el 6 gira 40 hacia el personaje de +0x3b0 y no se mueve; con 0xffff, nada. +0x38d sube hasta 15.
TEST(RavenAttackTest, TheShoutTurnsTowardTheAttackTarget)
{
    Raven tested;
    tested.spirit.state = 6;
    tested.spirit.target3b0 = 4;
    tested.spirit.byte38d = 14;
    const glm::vec3 victim{1500.0f, 1000.0f, 0.0f};
    const auto angle = static_cast<float>(Client::Render::Models::CreateAngle(1000.0f, 1000.0f, victim.x, victim.y));
    const auto turned = static_cast<float>(Client::Render::Models::TurnAngle(0.0f, angle, 40.0f));
    tested.Step(Lorencia, {.attackPosition = victim});
    EXPECT_EQ(tested.spirit.angles.z, turned);
    EXPECT_EQ(tested.spirit.position, glm::vec3(1000.0f, 1000.0f, 300.0f));
    EXPECT_EQ(tested.spirit.byte38d, 15);
    tested.Step(Lorencia, {.attackPosition = victim});
    EXPECT_EQ(tested.spirit.byte38d, 15);

    Raven still;
    still.spirit.state = 6;
    still.spirit.target3b0 = 0xffff;
    still.Step(Lorencia, {.attackPosition = victim});
    EXPECT_EQ(still.spirit.angles.z, 0.0f);
}

// 0x4177b1 y 0x417b33: con +0x1ad en 1, los estados 0 y 1 vuelan hacia el blanco de +8 como lo harían hacia el dueño,
// con la altura del blanco, y +0x1ad queda en 0. En Lorencia, Devias y el mapa del login (350 sobre el destino).
TEST(RavenAttackTest, TheFreeFlightFollowsTheTargetOfEight)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{0x37}})
    {
        SCOPED_TRACE(int{map});
        const glm::vec3 target{1300.0f, 900.0f, 50.0f};
        Raven following;
        following.spirit.byte1ad = 1;
        following.Step(map, {.target = target});

        Raven owned;
        owned.owner.position = target;
        owned.Step(map);

        EXPECT_EQ(following.spirit.byte1ad, 0);
        EXPECT_EQ(following.spirit.position, owned.spirit.position);
        EXPECT_EQ(following.spirit.angles, owned.spirit.angles);
        EXPECT_EQ(following.spirit.state, owned.spirit.state);
        EXPECT_EQ(following.random.State(), owned.random.State());
    }
}

// Los estados de 7 en adelante (el tipo del A8 que envuelve) sólo hacen el grito y la lejanía.
TEST(RavenAttackTest, AStateBeyondSixOnlyShoutsAndCounts)
{
    Raven tested;
    tested.spirit.state = 9;
    tested.Step();
    EXPECT_EQ(tested.spirit.state, 9);
    EXPECT_EQ(tested.spirit.position, glm::vec3(1000.0f, 1000.0f, 300.0f));
    EXPECT_EQ(tested.spirit.byte38d, 0);
}
} // namespace
