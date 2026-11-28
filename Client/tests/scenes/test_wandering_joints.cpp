#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x60f/7, /0xb y /0x19 (WanderingJoints.cpp): alta 0x533bbd, 0x533d5f y 0x533db8 y paso 0x53a87a..0x53b01f.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Scenes::JointState;
using Client::World::ModelEffect;

constexpr std::uint32_t Seed = 0x60fu;

std::uint64_t DrawsBetween(CrtRandom from, const CrtRandom& to)
{
    std::uint64_t draws = 0;
    while (from.State() != to.State() && draws < 100000)
    {
        static_cast<void>(from.Next());
        ++draws;
    }
    return draws;
}

struct Pool
{
    Pool()
    {
        cloth->random = CrtRandom{Seed};
        cloth->logicSteps = 77;
        cloth->openCounterAccesses = 1; // sin los cuatro rand() del cierre.
        arrow.alive = true;
        arrow.start = {1100, 2100, 160};
        actor.alive = true;
        actor.position = {520, 640, 0};
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, std::int32_t skill = 0,
                                                                           glm::vec3 to = {1000, 2000, 150})
    {
        glm::vec3 from{10, 20, 30};
        glm::vec3 angles{0, 0, 45};
        const bool arrowOwned = subtype != 7;
        const Client::Kernel::JointRequest request{.type = 0x60f,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = arrowOwned ? static_cast<void*>(&arrow)
                                                                              : static_cast<void*>(&actor),
                                                   .scale = 50,
                                                   .key = -1,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, cloth->random,
                                                arrowOwned ? JointOwnerKind::ModelEffect : JointOwnerKind::Actor);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(
            joints, cloth->random, cloth.get(), particles.get(), 0, sprites.get(), nullptr,
            [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); }, {}, {},
            [this](ModelEffect& hit) -> Client::Core::Expected<void, std::string>
            {
                hits.push_back(hit.transform.position);
                return {};
            });
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    std::unique_ptr<Client::Models::ClothWorld> cloth = std::make_unique<Client::Models::ClothWorld>();
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    ModelEffect arrow{};
    ActorState actor{};
    std::vector<std::int32_t> sounds;
    std::vector<glm::vec3> hits;
};

std::size_t LiveSprites(Client::World::FrameSprites& sprites)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }));
}
} // namespace

// 0x533bbd..0x533c13: la /7 nace azul (0,2; 0,2; 1) con la vida rand() % 10 + 30 y el resto en +0x9ec, 15 puntos, ancho
// 30 (no el del pedido) y vx en rand() % 3000; el destino es el segundo punto. Con el pool lleno, nada ni rand().
TEST(WanderingJointsTest, TheWanderersBornWithTheirDelay)
{
    Pool pool;
    CrtRandom expected{Seed};
    const std::int32_t delay = expected.Next() % 10;
    const std::int32_t drift = expected.Next() % 3000;

    ASSERT_TRUE(pool.Create(7).HasValue());

    const JointState& joint = pool.joints[0];
    EXPECT_EQ(pool.cloth->random.State(), expected.State());
    const float dim = std::bit_cast<float>(0x3e4ccccdu);
    EXPECT_EQ(joint.light, glm::vec3(dim, dim, 1));
    EXPECT_EQ(joint.life, delay + 30);
    EXPECT_EQ(joint.field9ec, delay);
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.width, 30.0f);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(static_cast<float>(drift), 0, 0));
    EXPECT_EQ(joint.targetPosition, glm::vec3(1000, 2000, 150));
    EXPECT_TRUE(joint.tailEnabled);

    for (auto& other : pool.joints)
        other.alive = true;
    const CrtRandom before = pool.cloth->random;
    const auto full = pool.Create(7);
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(pool.cloth->random.State(), before.State());
}

// 0x533d5f..0x533db3 y 0x533db8..0x533e0c: la /0xb (azul) y la /0x19 (rosa) sin demora: vida 30, +0x9ec en 0 y un solo
// rand(), el de vx.
TEST(WanderingJointsTest, TheArrowWanderersHaveNoDelay)
{
    for (const std::int32_t subtype : {0xb, 0x19})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        CrtRandom expected{Seed};
        const std::int32_t drift = expected.Next() % 3000;

        ASSERT_TRUE(pool.Create(subtype).HasValue());

        const JointState& joint = pool.joints[0];
        EXPECT_EQ(pool.cloth->random.State(), expected.State());
        const glm::vec3 light = subtype == 0xb
                                    ? glm::vec3(std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3e4ccccdu), 1)
                                    : glm::vec3(std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu),
                                                std::bit_cast<float>(0x3f19999au));
        EXPECT_EQ(joint.light, light);
        EXPECT_EQ(joint.life, 30);
        EXPECT_EQ(joint.field9ec, 0);
        EXPECT_EQ(joint.maximumCount, 15);
        EXPECT_EQ(joint.width, 30.0f);
        EXPECT_EQ(joint.velocity.x, static_cast<float>(drift));
        EXPECT_EQ(joint.ownerKind, JointOwnerKind::ModelEffect);
    }
}

// 0x53a89e..0x53b01f con la /0xb en el lugar 0 y 77 pasos (fase −77), vida 30: la dirección con las frecuencias de
// una vez y media, el radio 150, la mezcla con la partida de la flecha corrida por cos(arg · 0,01f) · 25, el sonido
// 0x42 (vida 30) y tres sprites (0x5fd y dos 0x5ac) con dos rand() cada uno. Calculado aparte de MuRet
// (golden_wander.py: Fraction y Decimal, FSIN/FCOS a 64 bits, PC53 y stores a float).
TEST(WanderingJointsTest, TheArrowWandererCirclesItsStart)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0xb).HasValue());
    const CrtRandom before = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.position, glm::vec3(std::bit_cast<float>(0x4484710bu), std::bit_cast<float>(0x4502a1eeu),
                                        std::bit_cast<float>(0x4237a6c4u)));
    EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 6u);
    EXPECT_EQ(LiveSprites(*pool.sprites), 3u);
    EXPECT_EQ(pool.sounds, std::vector<std::int32_t>{0x42});
    EXPECT_TRUE(pool.hits.empty());
    EXPECT_EQ(joint.life, 29);
    EXPECT_EQ(joint.count, 1);
    // La /0xb no se apaga (0x53b318).
    EXPECT_EQ(joint.light.x, std::bit_cast<float>(0x3e4ccccdu));
}

// 0x53acaf..0x53ae06: la /7 en el lugar 1 (fase 53.808) con vida 12 y +0x9ec 3, alrededor del +0x10 del personaje y
// 100 más arriba; con el contador cerrado en 0, sus cuatro rand() antes de los seis de los sprites.
TEST(WanderingJointsTest, TheWandererCirclesItsOwner)
{
    Pool pool;
    pool.joints[0].alive = true;
    pool.joints[0].type = 0x617; // un lugar ocupado que no se mueve
    pool.joints[0].subtype = 0;
    pool.joints[0].life = 1000;
    pool.joints[0].tailEnabled = false;
    ASSERT_TRUE(pool.Create(7, 0, {500, 600, 70}).HasValue());
    JointState& joint = pool.joints[1];
    joint.life = 12;
    joint.field9ec = 3;
    pool.cloth->openCounterAccesses = 0;
    const CrtRandom before = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_EQ(joint.position, glm::vec3(std::bit_cast<float>(0x43f50746u), std::bit_cast<float>(0x44021e2cu),
                                        std::bit_cast<float>(0x432cb27fu)));
    EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 4u + 6u);
    EXPECT_TRUE(pool.sounds.empty());
}

// 0x53aaa2..0x53acd7 con el radio sin recortar a 150 y la mezcla de la rama "· 4": con t = vida · 4/3, la /0x19 con
// vida 5 (t debajo de 20: la mezcla t · 4 y el radio 0, recortado desde t − 10 < 0), la /7 con vida 29 y +0x9ec 3 (el
// radio (40 − t) · 15 ≈ 20) y la /0xb con vida 20 (el radio (t − 10) · 5 ≈ 83), con 77 pasos y el reloj sin cerrar.
// Los bits los dio una emulación x87 independiente de MuRet (la revisión ASM de la cuarta vuelta).
TEST(WanderingJointsTest, TheWanderersOutsideTheWidestRadius)
{
    struct Probe
    {
        std::int32_t subtype;
        std::size_t slot;
        std::int32_t life;
        std::int32_t delay;
        glm::vec3 to;
        glm::vec3 expected;
    };
    const auto bits = [](std::uint32_t x, std::uint32_t y, std::uint32_t z)
    { return glm::vec3(std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)); };
    for (const Probe& probe : {Probe{0x19, 2, 5, 0, {1000, 2000, 150}, bits(0x44875a61u, 0x45029f09u, 0x432f74d5u)},
                               Probe{7, 1, 29, 3, {500, 600, 70}, bits(0x43fa232au, 0x44135cf0u, 0x43280d68u)},
                               Probe{0xb, 0, 20, 0, {1000, 2000, 150}, bits(0x4482aa50u, 0x4500b5c2u, 0x42cc8c48u)}})
    {
        SCOPED_TRACE(probe.subtype);
        Pool pool;
        for (std::size_t slot = 0; slot < probe.slot; ++slot)
        {
            pool.joints[slot].alive = true;
            pool.joints[slot].type = 0x617; // un lugar ocupado que no se mueve
            pool.joints[slot].subtype = 0;
            pool.joints[slot].life = 1000;
            pool.joints[slot].tailEnabled = false;
        }
        const auto created = pool.Create(probe.subtype, 0, probe.to);
        ASSERT_TRUE(created.HasValue());
        ASSERT_EQ(created.Value(), std::optional<std::size_t>(probe.slot));
        JointState& joint = pool.joints[probe.slot];
        joint.life = probe.life;
        joint.field9ec = probe.delay;
        const CrtRandom before = pool.cloth->random;

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(joint.position, probe.expected);
        EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 6u);
        EXPECT_TRUE(pool.sounds.empty());
    }
}

// 0x53afc0..0x53b017: la /7 en su última vida también suelta la 0x5ed/1 (escala 0,6f) en su posición, sin rand() ni
// apagarse (0x53b318); con las 3.000 partículas llenas, no la crea y sigue igual.
TEST(WanderingJointsTest, TheWandererBurstsAtTheEndUnlessTheParticlesAreFull)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Pool pool;
        ASSERT_TRUE(pool.Create(7).HasValue());
        JointState& joint = pool.joints[0];
        joint.life = 1;
        const glm::vec3 light = joint.light;
        if (full)
            for (auto& particle : pool.particles->All())
                particle.alive = true;
        const CrtRandom before = pool.cloth->random;

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 6u);
        EXPECT_EQ(joint.light, light);
        EXPECT_EQ(joint.life, 0);
        EXPECT_TRUE(joint.alive);
        if (full)
            continue;
        const auto bursts = std::ranges::count_if(pool.particles->All(), [](const auto& particle)
                                                  { return particle.alive && particle.type == 0x5ed; });
        ASSERT_EQ(bursts, 1);
        const auto& burst = pool.particles->All().front();
        EXPECT_EQ(burst.subtype, 1);
        EXPECT_EQ(burst.position, joint.position);
        EXPECT_EQ(burst.scale, std::bit_cast<float>(0x3f19999au));
    }
}

// 0x53ae2f..0x53ae72: con el skill 1, la flecha queda en la posición del joint y choca (0x520170).
// 0x53a884..0x53a899: con la flecha apagada, el joint se apaga con la vida en −1 y pasa por la cola (−2), sin rand().
TEST(WanderingJointsTest, TheSecondArrowWandererDrivesTheArrow)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0xb, 1).HasValue());
    const JointState& joint = pool.joints[0];

    ASSERT_TRUE(pool.Step().HasValue());

    ASSERT_EQ(pool.hits.size(), 1u);
    EXPECT_EQ(pool.hits.front(), joint.position);
    EXPECT_EQ(pool.arrow.transform.position, joint.position);

    pool.arrow.alive = false;
    const CrtRandom before = pool.cloth->random;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -2);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(pool.cloth->random.State(), before.State());
    EXPECT_EQ(pool.hits.size(), 1u);
}

// 0x53afc9..0x53b017 y 0x53b339: la /0x19 en su última vida suelta la partícula 0x5ed/1 (escala 0,6f) y, con menos de
// 10 de vida, se apaga por 0,7692308f. La /0xb no la suelta.
TEST(WanderingJointsTest, ThePinkWandererBurstsAtTheEnd)
{
    for (const std::int32_t subtype : {0x19, 0xb})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(subtype).HasValue());
        JointState& joint = pool.joints[0];
        joint.life = 1;
        const glm::vec3 light = joint.light;

        ASSERT_TRUE(pool.Step().HasValue());

        const auto bursts = std::ranges::count_if(pool.particles->All(), [](const auto& particle)
                                                  { return particle.alive && particle.type == 0x5ed; });
        EXPECT_EQ(bursts, subtype == 0x19 ? 1 : 0);
        const double fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
        EXPECT_EQ(joint.light.x, subtype == 0x19 ? static_cast<float>(static_cast<double>(light.x) * fade) : light.x);
    }
}

// Sin la flecha dueña viva y sin el choque, la /0xb con skill 1 corta en 0x53ae6d.
TEST(WanderingJointsTest, TheArrowHitNeedsItsService)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0xb, 1).HasValue());

    const auto stepped = Client::Scenes::StepJoints(pool.joints, pool.cloth->random, pool.cloth.get(),
                                                    pool.particles.get(), 0, pool.sprites.get());

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53ae6d"), std::string::npos) << stepped.Error();
}
