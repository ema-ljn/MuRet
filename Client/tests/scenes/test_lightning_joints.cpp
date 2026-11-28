#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointKernels.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Las familias 0x614 (LightningJoints.cpp) y 0x61b/0x5bc (HandBoltJoints.cpp): el alta de 0x530860 (0x5309d6,
// 0x5335cf), el paso de 0x535840 (0x53683e, 0x539ccc) y el dibujo común de 0x53c6a0. 06, "Los joints de las familias".
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x51c0ffeu;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);

using Client::Math::D;
using Client::Math::F32;

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t type, std::int32_t subtype, glm::vec3 from, glm::vec3 to, float width, void* owner = nullptr,
           JointOwnerKind kind = JointOwnerKind::ByType, std::int32_t key = -1, std::int32_t skill = 0,
           const Client::Joints::JointKernels::CreationServices* services = nullptr)
    {
        glm::vec3 angles = startAngles;
        const Client::Kernel::JointRequest request{.type = type,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = width,
                                                   .key = key,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind, services);
    }

    Client::Core::Expected<void, std::string> Step(float clock = 0.0f, Client::Kernel::WorldState* world = nullptr)
    {
        return Client::Joints::StepJoints(joints, random, nullptr, &particles, clock, nullptr, lighting.get(), {}, {},
                                          {}, {}, nullptr, nullptr, world);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    glm::vec3 startAngles{};
    Client::Particles::Particles particles;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
};

ActorState Owner(glm::vec3 position)
{
    ActorState owner;
    owner.alive = true;
    owner.position = position;
    return owner;
}

// Una semilla cuyo rand() número `index` (desde 0) cumple `wanted`.
template <typename Wanted> std::uint32_t SeedWhere(int index, Wanted wanted)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        CrtRandom probe(seed);
        probe.Discard(static_cast<std::uint32_t>(index));
        if (wanted(probe.Next()))
            return seed;
    }
    return 0;
}

int AliveParticles(const Client::Particles::Particles& particles, std::int32_t type)
{
    int found = 0;
    for (const Client::Particles::Particle& particle : particles.All())
        found += particle.alive && particle.type == type ? 1 : 0;
    return found;
}

// 0x5309d6..0x5309ec y 0x5313b1: el 614/0 toma el ancho del pedido después del primer tramo, que usó el retenido,
// velocidad 50, 50 puntos y vida 2, sin rand().
TEST(LightningJointsTest, TheMonsterBoltKeepsTheRetainedWidthForItsFirstQuad)
{
    Pool pool;
    pool.joints[0].width = 8.0f;
    const auto created = pool.Create(0x614, 0, {100, 200, 300}, {0, 0, 0}, 50.0f);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.points[0][0], (glm::vec3{96, 200, 300}));
    EXPECT_EQ(joint.points[0][3], (glm::vec3{100, 200, 304}));
    EXPECT_EQ(joint.width, 50.0f);
    EXPECT_EQ(joint.speed, 50.0f);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_EQ(joint.life, 2);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(pool.random.State(), Seed);
}

// 0x5313c0, 0x531318, 0x530a00, 0x531618 y 0x53163a: los rand() del alta, en su orden.
TEST(LightningJointsTest, TheRandomSubtypesDrawInTheirNativeOrder)
{
    struct Case
    {
        std::int32_t subtype;
        float speed;
        std::int32_t life;
        int draws;
    };
    CrtRandom r(Seed);
    const int a = r.Next();
    const int b = r.Next();
    const int c = r.Next();
    const std::vector<Case> cases{
        {1, F32(static_cast<double>(a % 10) + 20.0), b % 8 + 8, 2},
        {0x10, F32(static_cast<double>(a % 10) + 20.0), b % 2 + 2, 2},
        {0x11, F32(static_cast<double>(a % 10) + 20.0), 10, 1},
        {8, static_cast<float>(a % 10 + 6), b % 8 + 8, 2},
        {9, F32(static_cast<double>(a % 20) + 80.0), 30, 2},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x614, probe.subtype, {}, {}, 30.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.life, probe.life);
        CrtRandom expected(Seed);
        expected.Discard(static_cast<std::uint32_t>(probe.draws));
        EXPECT_EQ(pool.random.State(), expected.State());
        if (probe.subtype == 9)
        {
            const float gray = F32(static_cast<double>(b % 10) * D(std::bit_cast<float>(0x3d888889u)) + D(Tenth));
            EXPECT_EQ(joint.light, glm::vec3(gray));
        }
        if (probe.subtype == 8)
        {
            EXPECT_EQ(joint.width, 5.0f);
            EXPECT_EQ(joint.maximumCount, 2);
        }
    }
    static_cast<void>(c);
}

// 0x531460 (la /3: la vida en la key y los puntos y la velocidad en el skill), 0x531494/0x5314ed (la /4 y la /5, al
// destino y 600 más arriba), 0x53139d (la /0xb) y 0x531376 (la /0xd, que pasa a ser una /0xb).
TEST(LightningJointsTest, TheFixedSubtypesWriteTheirFields)
{
    const glm::vec3 halfBlue{0.5f, 0.5f, 1.0f};
    {
        Pool pool;
        ASSERT_TRUE(
            pool.Create(0x614, 3, {1, 2, 3}, {}, 20.0f, nullptr, JointOwnerKind::ByType, 0x1234, 0x10c).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.life, 0x1234);
        EXPECT_EQ(joint.maximumCount, 12);
        EXPECT_EQ(joint.speed, 12.0f);
        EXPECT_EQ(joint.key, -1);
        EXPECT_EQ(joint.light, halfBlue);
        EXPECT_EQ(joint.pathIncrement, (glm::vec3{1, 2, 3}));
    }
    for (const std::int32_t subtype : {4, 5})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        pool.joints[0].velocity = {7, 8, 9};
        ASSERT_TRUE(pool.Create(0x614, subtype, {1, 2, 3}, {10, 20, 30}, 20.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.speed, subtype == 4 ? 60.0f : -60.0f);
        EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.maximumCount, 10);
        EXPECT_EQ(joint.targetPosition, (glm::vec3{10, 20, subtype == 4 ? 30.0f : 630.0f}));
        EXPECT_EQ(joint.pathIncrement, (glm::vec3{1, 2, 3}));
        EXPECT_EQ(pool.random.State(), Seed);
    }
    for (const std::int32_t subtype : {0xb, 0xd})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x614, subtype, {}, {}, 20.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.subtype, 0xb);
        EXPECT_EQ(joint.life, 2);
        EXPECT_EQ(joint.light, subtype == 0xb ? glm::vec3(1.0f, 0.5f, Tenth) : glm::vec3(1.0f, Tenth, Tenth));
    }
}

// 0x5313fe: la /2 apunta al hueso 33 del personaje dueño, (0, −150, 0), con el modelo del jugador; sin dueño o sin el
// servicio, corta.
TEST(LightningJointsTest, TheOwnerBoneBoltAimsAtTheChestOfItsOwner)
{
    ActorState owner = Owner({500, 600, 700});
    std::size_t askedBone = 0;
    glm::vec3 askedOffset{};
    const Client::Joints::JointKernels::CreationServices services{
        .playerBonePoint = [&](const ActorState& bones, std::size_t bone, const glm::vec3& offset)
        {
            askedBone = bone;
            askedOffset = offset;
            return bones.position + glm::vec3(1, 2, 3);
        }};
    Pool pool;
    ASSERT_TRUE(pool.Create(0x614, 2, {}, {}, 50.0f, &owner, JointOwnerKind::Actor, -1, 0, &services).HasValue());
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(askedBone, 33u);
    EXPECT_EQ(askedOffset, (glm::vec3{0, -150, 0}));
    EXPECT_EQ(joint.targetPosition, (glm::vec3{501, 602, 703}));
    EXPECT_EQ(joint.light, (glm::vec3{1.0f, Tenth, 0.0f}));
    EXPECT_EQ(joint.life, 2);

    Pool unserved;
    const auto cut = unserved.Create(0x614, 2, {}, {}, 50.0f, &owner, JointOwnerKind::Actor);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x531456"), std::string::npos) << cut.Error();
    Pool orphan;
    const auto lonely = orphan.Create(0x614, 2, {}, {}, 50.0f, nullptr, JointOwnerKind::ByType, -1, 0, &services);
    ASSERT_FALSE(lonely.HasValue());
    EXPECT_NE(lonely.Error().find("0x531412"), std::string::npos) << lonely.Error();
}

// 0x531356: la /0xc nace roja con vida 10. Su paso (0x536adf..0x536b22) pone la posición en el punto (0, −100, 0) de
// la matriz 0x743751c del modelo del tipo del dueño: nadie la escribe en c32bbd5f y queda en cero, así que el punto es
// la posición del modelo compartido. Salta 0x4e9810: "lo que falta" es el [ebp+8] de 0x53586c, la y de la posición
// menos la del destino antes de moverse, con signo. Sin dueño personaje o sin los modelos, corta.
TEST(LightningJointsTest, TheRedBoltSitsOnTheSharedModelOfItsOwner)
{
    ActorState owner = Owner({1.0f, 2.0f, 3.0f});
    owner.type = 0x1a5;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    const glm::vec3 origin{700.25f, 800.5f, 900.75f};
    world->PlaceModel(owner.type, {.scale = 1.5f, .origin = origin});
    // y de partida menos y del destino: 40 (< 50 · 1,5, llega), −500 (llega: no hay valor absoluto), 75 (50 · 1,5 = 75
    // exacto: con el > estricto de 0x536d71 no llega en ese tramo) y 80 (no llega). Con dueño, el alta no copia el
    // segundo punto (0x530942 salta a 0x530957): el destino es el que tenía el lugar, (0, 0, 0), y no el (0, 1000, 0)
    // que se pasa.
    for (const float startY : {40.0f, -500.0f, 75.0f, 80.0f})
    {
        SCOPED_TRACE(startY);
        Pool pool;
        ASSERT_TRUE(
            pool.Create(0x614, 0xc, {10.0f, startY, 30.0f}, {0.0f, 1000.0f, 0.0f}, 30.0f, &owner, JointOwnerKind::Actor)
                .HasValue());
        EXPECT_EQ(pool.joints[0].targetPosition, glm::vec3(0.0f));
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.light, (glm::vec3{1.0f, Tenth, Tenth}));
        EXPECT_EQ(joint.life, 10);
        EXPECT_EQ(joint.speed, 50.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        const std::int32_t count = joint.count;
        CrtRandom twin = pool.random;
        const auto stepped = pool.Step(0.0f, world.get());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const bool arrives = startY < 75.0f;
        // Cada tramo saca los dos rand() del temblor por defecto (0x536cb3); con ancho 30 no hay luz ni partículas.
        const int segments = arrives ? 1 : 50;
        glm::vec3 shaken{};
        for (int segment = 0; segment < segments; ++segment)
        {
            shaken.x = F32(static_cast<double>((twin.Next() & 0x3ff) - 0x200) / 30.0);
            shaken.z = F32(static_cast<double>((twin.Next() & 0x3ff) - 0x200) / 30.0);
        }
        EXPECT_EQ(pool.random.State(), twin.State());
        EXPECT_EQ(joint.velocity.x, shaken.x);
        EXPECT_EQ(joint.velocity.z, shaken.z);
        shaken = {F32(D(joint.angles.x) + D(shaken.x)), F32(D(joint.velocity.y) + D(joint.angles.y)),
                  F32(D(shaken.z) + D(joint.angles.z))};
        // Un corrimiento por tramo y otro de la cola común (CommonTail), hasta 49 (0x53553f).
        EXPECT_EQ(joint.count, std::min(count + segments + 1, joint.maximumCount - 1));
        if (arrives)
        {
            EXPECT_EQ(joint.position, origin);
            continue;
        }
        // El último tramo vuelve al modelo y avanza (0, −50, 0) por la matriz de sus ángulos con el temblor.
        const auto moved =
            Client::Math::OriginalVectorRotate({0.0f, -50.0f, 0.0f}, Client::Math::OriginalAngleMatrix(shaken));
        EXPECT_EQ(joint.position, (glm::vec3{F32(D(moved.x) + D(origin.x)), F32(D(moved.y) + D(origin.y)),
                                             F32(D(moved.z) + D(origin.z))}));
    }

    Pool orphan;
    ASSERT_TRUE(orphan.Create(0x614, 0xc, {}, {}, 30.0f).HasValue());
    const auto lonely = orphan.Step(0.0f, world.get());
    ASSERT_FALSE(lonely.HasValue());
    EXPECT_TRUE(lonely.Error().starts_with("0x536adf")) << lonely.Error();
    Pool unserved;
    ASSERT_TRUE(unserved.Create(0x614, 0xc, {}, {}, 30.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto cut = unserved.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x536b1d")) << cut.Error();
}

// 0x531907..0x5319e5: la /0xf no se dibuja, vive 80 y elige hasta seis personajes cercanos (a 400 o menos), vivos, sin
// +0x389, de clase 2, visibles y que no son el dueño; +0x9eb es la cuenta · 15 y +0x24 el segundo punto 150 más arriba.
// Su paso no se mueve (0x53587c); con la vida par suelta dos 0x614/0 y corta en la luz de su 0x5ca (0x5370e8, Q-C1622);
// con la vida impar sólo corre la cuenta (sin dibujo, la matriz de la pila no se ve).
TEST(LightningJointsTest, TheChainPicksTheNearMonsters)
{
    std::vector<ActorState> actors(14);
    for (std::size_t i = 0; i < actors.size(); ++i)
    {
        actors[i] = Owner({100.0f + static_cast<float>(i) * 10.0f, 0.0f, 0.0f});
        actors[i].byte88 = 2;
        actors[i].seen = true;
    }
    actors[0].position.x = 600.0f; // lejos
    actors[2].dying = 1;
    actors[3].byte88 = 1;
    actors[4].seen = false;
    actors[5].alive = false;
    // A 400 justos entra (0x53194f: test ah,0x41); apenas más lejos, no. Hay siete candidatos: el séptimo (el 13) queda
    // afuera por el tope de seis (0x531995: cmp [+0x9ec],5; jg).
    actors[6].position = {500.0f, 0.0f, 0.0f};
    actors[7].position = {500.0f, 1.0f, 0.0f};
    const Client::Joints::JointKernels::CreationServices services{.actors = actors};
    Pool pool;
    Pool unserved;
    const auto cut = unserved.Create(0x614, 0xf, {}, {}, 50.0f);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x531922")) << cut.Error();
    ASSERT_TRUE(pool.Create(0x614, 0xf, {100.0f, 0.0f, 0.0f}, {7.0f, 8.0f, 9.0f}, 50.0f, &actors[1],
                            JointOwnerKind::Actor, -1, 0, &services)
                    .HasValue());
    JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.drawEnabled, 0);
    EXPECT_EQ(joint.life, 80);
    EXPECT_EQ(joint.maximumCount, 0);
    EXPECT_EQ(joint.field9ec, 0);
    EXPECT_EQ(joint.volley, 90);
    EXPECT_EQ(joint.chainTargets, (std::array<std::int32_t, 6>{6, 8, 9, 10, 11, 12}));
    EXPECT_EQ(joint.skillSerial, 12);
    EXPECT_EQ(joint.pathIncrement, (glm::vec3{7.0f, 8.0f, 159.0f}));

    const auto stepped = pool.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_TRUE(stepped.Error().starts_with("0x5370e8")) << stepped.Error();
    EXPECT_EQ(joint.position, (glm::vec3{100.0f, 0.0f, 0.0f}));
    for (std::size_t i = 1; i <= 2; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_TRUE(pool.joints[i].alive);
        EXPECT_EQ(pool.joints[i].type, 0x614);
        EXPECT_EQ(pool.joints[i].subtype, 0);
        EXPECT_EQ(pool.joints[i].width, i == 1 ? 50.0f : 10.0f);
        EXPECT_EQ(pool.joints[i].targetPosition, joint.pathIncrement);
    }
    pool.joints[1].alive = false;
    pool.joints[2].alive = false;
    joint.life = 79;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(joint.position, (glm::vec3{100.0f, 0.0f, 0.0f}));
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.life, 78);
    EXPECT_EQ(pool.random.State(), Seed);
}

// 0x530860 con el pool lleno: nada, ni los rand() de la /1.
TEST(LightningJointsTest, AFullPoolCreatesNothingAndDrawsNothing)
{
    Pool pool;
    for (JointState& joint : pool.joints)
        joint.alive = true;
    const auto created = pool.Create(0x614, 1, {}, {}, 30.0f);
    ASSERT_TRUE(created.HasValue());
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(pool.random.State(), Seed);
    const auto bolt = pool.Create(0x61b, 0, {}, {}, 30.0f);
    ASSERT_TRUE(bolt.HasValue());
    EXPECT_FALSE(bolt.Value().has_value());
}

// 0x536982 y 0x536e95..0x537042: la /0 sigue a su dueño (80 más arriba); al llegar, con el ancho fuera de 50 no hay
// partículas y con el reloj en la segunda mitad del segundo, tampoco la rama: dos rand() de la sacudida y la cola.
TEST(LightningJointsTest, TheMonsterBoltFollowsItsOwnerAndStopsWhenItArrives)
{
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    ASSERT_TRUE(pool.Create(0x614, 0, {1000, 1040, 80}, {}, 10.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& joint = pool.joints[0];
    const JointState before = joint;
    ASSERT_TRUE(pool.Step(600.0f).HasValue());
    EXPECT_EQ(joint.targetPosition, (glm::vec3{1000, 1000, 80}));
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(pool.random.State(), expected.State());
    // El movimiento común (velocidad 50 con los ángulos de antes) y nada más: llega en el primer tramo.
    const glm::vec3 moved = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -50.0f, 0.0f),
                                                               Client::Math::OriginalAngleMatrix(before.angles));
    EXPECT_EQ(joint.position, (glm::vec3{F32(D(before.position.x) + D(moved.x)), F32(D(before.position.y) + D(moved.y)),
                                         F32(D(before.position.z) + D(moved.z))}));
    EXPECT_EQ(joint.life, 1);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::ThunderParticle), 0);
}

// 0x536f71..0x53703d: con el ancho en 50, en la primera mitad del segundo y con rand() % 16 en 0, la /0 suelta una /1
// desde el destino corrido (rand() % 100 − 50, rand() % 100 − 50, rand() % 120 − 60) con el ancho rand() % 8 + 6, del
// mismo dueño. Antes: los dos rand() de la sacudida, los dos del 0x5ca/0 (0x536eb9) y el % 8 que no suelta el 0x5f2.
TEST(LightningJointsTest, TheMonsterBoltBranchesInTheFirstHalfOfTheSecond)
{
    std::uint32_t seed = 0;
    for (std::uint32_t probe = 1; probe < 100000 && seed == 0; ++probe)
    {
        CrtRandom draws(probe);
        draws.Discard(4);
        const int breath = draws.Next();
        if (breath % 8 != 0 && draws.Next() % 16 == 0)
            seed = probe;
    }
    ASSERT_NE(seed, 0u);
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    pool.random = CrtRandom(seed);
    ASSERT_TRUE(pool.Create(0x614, 0, {1000, 1040, 80}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
    // El padre en el lugar 2: la rama cae en el 0, que el paso ya dejó atrás (0x53c670 recorre los 500 en orden).
    pool.joints[2] = pool.joints[0];
    pool.joints[0] = JointState{};
    ASSERT_TRUE(pool.Step(400.0f).HasValue());
    EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::ThunderParticle), 1);
    CrtRandom expected(seed);
    expected.Discard(6);
    const int x = expected.Next() % 100 - 50;
    const int y = expected.Next() % 100 - 50;
    const int z = expected.Next() % 120 - 60;
    const float width = F32(static_cast<double>(expected.Next() % 8) + 6.0);
    const float speed = F32(static_cast<double>(expected.Next() % 10) + 20.0);
    const std::int32_t life = expected.Next() % 8 + 8;
    const JointState& branch = pool.joints[0];
    ASSERT_TRUE(branch.alive);
    EXPECT_EQ(branch.type, 0x614);
    EXPECT_EQ(branch.subtype, 1);
    EXPECT_EQ(branch.ownerContext, &owner);
    EXPECT_EQ(branch.ownerKind, JointOwnerKind::Actor);
    EXPECT_EQ(branch.width, width);
    EXPECT_EQ(branch.speed, speed);
    EXPECT_EQ(branch.position, (glm::vec3{F32(x + 1000.0), F32(y + 1000.0), F32(z + 80.0)}));
    EXPECT_EQ(branch.targetPosition, glm::vec3(0.0f)); // Con dueño, +0x58 queda la del uso anterior.
    EXPECT_EQ(branch.life, life);
    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_FALSE(pool.joints[1].alive);
}

// 0x536e9c: con un ancho distinto de 50 (los 0x614/0 de ancho 10 de la cola de 0x4f5b70) la llegada salta entera a
// 0x537045: ni el 0x5ca, ni el % 8, ni la rama de la /0 aunque el reloj y el rand() la habrían soltado.
TEST(LightningJointsTest, AThinMonsterBoltNeverBranches)
{
    const std::uint32_t seed = SeedWhere(2, [](int value) { return value % 16 == 0; });
    ASSERT_NE(seed, 0u);
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    pool.random = CrtRandom(seed);
    ASSERT_TRUE(pool.Create(0x614, 0, {1000, 1040, 80}, {}, 10.0f, &owner, JointOwnerKind::Actor).HasValue());
    pool.joints[2] = pool.joints[0];
    pool.joints[0] = JointState{};
    ASSERT_TRUE(pool.Step(400.0f).HasValue());
    CrtRandom expected(seed);
    expected.Discard(2);
    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_FALSE(pool.joints[0].alive);
    EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::ThunderParticle), 0);
}

// 0x536d4d: la /3 lejos de su destino (más de 150) pone la vida en 0 y la cola común la mata.
TEST(LightningJointsTest, TheFarThirdSubtypeDiesAfterItsSegment)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x614, 3, {}, {}, 20.0f, nullptr, JointOwnerKind::ByType, 5, 1).HasValue());
    JointState& joint = pool.joints[0];
    joint.targetPosition = {0, 1000, 0};
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(pool.random.State(), expected.State());
}

// 0x5335cf..0x533672: el 61b repite la textura; los dos con velocidad 40, ancho 60, 50 puntos y vida 2; la /0 blanca,
// la /3 quieta con vida 20, y las demás rojas (61b) o azules (5bc). El primer tramo usa el ancho retenido.
TEST(HandBoltJointsTest, TheBoltsWriteTheirLightSpeedAndWidth)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        glm::vec3 light;
        float speed;
        std::int32_t life;
    };
    const std::vector<Case> cases{
        {0x61b, 0, glm::vec3(1.0f), 40.0f, 2}, {0x61b, 1, {std::bit_cast<float>(0x3eb33333u), Tenth, 1.0f}, 40.0f, 2},
        {0x61b, 3, glm::vec3(1.0f), 0.0f, 20}, {0x5bc, 1, {0.0f, std::bit_cast<float>(0x3e99999au), 1.0f}, 40.0f, 2},
        {0x5bc, 0, glm::vec3(1.0f), 40.0f, 2},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.type << "/" << probe.subtype);
        Pool pool;
        pool.joints[0].width = 4.0f;
        ASSERT_TRUE(pool.Create(probe.type, probe.subtype, {10, 20, 30}, {}, 10.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.repeatedTexture, probe.type == 0x61b);
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.width, 60.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.texture, probe.type);
        EXPECT_EQ(joint.points[0][0], (glm::vec3{8, 20, 30}));
        EXPECT_EQ(pool.random.State(), Seed);
    }
}

// 0x539ce1..0x539ec9: un tramo lejos del dueño: el destino 80 sobre el dueño, la sacudida acumulada y amortiguada a
// 0,8f (con la z en la x87), la luz dinámica ((rand() & 3) + 4) · 0,05f y el avance de 40 con la matriz sacudida.
TEST(HandBoltJointsTest, AFarBoltShakesGlowsAndAdvances)
{
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    ASSERT_TRUE(pool.Create(0x61b, 1, {500, 500, 0}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& joint = pool.joints[0];
    joint.maximumCount = 1;
    joint.velocity = {2, 5, 3};
    // El movimiento común y el giro, a mano.
    glm::vec3 position = joint.position;
    const glm::vec3 common = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -40.0f, 0.0f),
                                                                Client::Math::OriginalAngleMatrix(joint.angles));
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(position[axis]) + D(common[axis]));
    glm::vec3 angles = joint.angles;
    const glm::vec3 target{1000, 1000, 80};
    static_cast<void>(Client::Math::MoveToward(position, angles, target, 25.0f));

    ASSERT_TRUE(pool.Step().HasValue());

    CrtRandom expected(Seed);
    const double damping = D(std::bit_cast<float>(0x3f4ccccdu));
    const float vx = F32(F32(static_cast<double>((expected.Next() & 0xff) - 0x80) / 60.0 + 2.0) * damping);
    const float vy = F32(5.0 * damping);
    const double roll = (static_cast<double>((expected.Next() & 0xff) - 0x80) / 60.0 + 3.0) * damping;
    static_cast<void>(expected.Next());
    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_EQ(joint.targetPosition, target);
    EXPECT_EQ(joint.velocity, (glm::vec3{vx, vy, F32(roll)}));
    const glm::vec3 shaken{F32(D(angles.x) + D(vx)), F32(D(vy) + D(angles.y)), F32(roll + D(angles.z))};
    const glm::vec3 moved =
        Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -40.0f, 0.0f), Client::Math::OriginalAngleMatrix(shaken));
    EXPECT_EQ(joint.position, (glm::vec3{F32(D(moved.x) + D(position.x)), F32(D(moved.y) + D(position.y)),
                                         F32(D(moved.z) + D(position.z))}));
    EXPECT_EQ(joint.life, 1);
    EXPECT_TRUE(joint.alive);
}

// 0x539ed4..0x539f11: al llegar, una vez cada dos, tres rand() para nada y el fuego 0x5d9/0 en la posición, con los
// ángulos y la luz del joint; la otra vez, nada.
TEST(HandBoltJointsTest, AnArrivingBoltDropsFireEveryOtherTime)
{
    for (const bool even : {true, false})
    {
        SCOPED_TRACE(even);
        const std::uint32_t seed = SeedWhere(2, [even](int value) { return (value % 2 == 0) == even; });
        ASSERT_NE(seed, 0u);
        ActorState owner = Owner({1000, 1000, 0});
        Pool pool;
        pool.random = CrtRandom(seed);
        ASSERT_TRUE(pool.Create(0x5bc, 1, {1000, 1000, 80}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
        JointState& joint = pool.joints[0];
        ASSERT_TRUE(pool.Step().HasValue());
        CrtRandom expected(seed);
        expected.Discard(3);
        if (even)
        {
            expected.Discard(3);
            Client::Particles::Particles reference;
            ASSERT_TRUE(reference
                            .CreateChecked(Client::Particles::FireParticle, joint.position, joint.angles, joint.light,
                                           0, 1.0f, 0.0f, expected)
                            .HasValue());
        }
        EXPECT_EQ(pool.random.State(), expected.State());
        EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::FireParticle), even ? 1 : 0);
        EXPECT_EQ(joint.count, 2);
        EXPECT_EQ(joint.life, 1);
    }
}

// 0x53c6a0: el 61b se dibuja con su textura (JointLaser02, repetida) y el aditivo común, dos caras por tramo.
TEST(HandBoltJointsTest, TheBoltDrawsWithItsOwnTexture)
{
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    ASSERT_TRUE(pool.Create(0x61b, 0, {0, 0, 0}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& joint = pool.joints[0];
    joint.maximumCount = 3;
    ASSERT_TRUE(pool.Step().HasValue());
    ASSERT_EQ(joint.count, 2);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, pool.joints, 0).HasValue());
    ASSERT_TRUE(batch.State().texture.has_value());
    EXPECT_EQ(*batch.State().texture, Client::Render::TextureSlot{0x61b});
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(batch.Vertices().size(), 2u * 12u);
}
} // namespace
