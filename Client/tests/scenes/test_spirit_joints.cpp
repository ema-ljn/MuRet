#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <functional>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x613 y el 0x6ee (SpiritJoints.cpp) y la gota 0x60f/2 (DropJoints.cpp): el alta de 0x53265e y 0x5340dc, el paso
// de 0x538d77 y 0x53b156, y el dibujo común de 0x53c6a0. 06, "Los joints de las familias".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x613beefu;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t type, std::int32_t subtype, glm::vec3 at, float width, void* owner = nullptr,
           JointOwnerKind kind = JointOwnerKind::ByType, std::int32_t skill = 0)
    {
        glm::vec3 from = at;
        glm::vec3 angles = startAngles;
        const Client::World::FishEffectRequest request{.type = type,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = owner,
                                                       .scale = width,
                                                       .key = -1,
                                                       .skill = skill,
                                                       .skillSerial = 0,
                                                       .target = -1};
        const Client::Scenes::JointKernels::CreationServices services{
            .playerBonePoint = {},
            .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
            .ownRecordDepth = withOwnRecord ? &ownRecordDepth : nullptr,
            .ownVolley = withOwnRecord ? &ownVolley : nullptr};
        return Client::Scenes::CreateFaunaJoint(joints, request, random, kind, &services);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, &cloth, &particles, 0.0f, &sprites, lighting.get(), {},
                                          terrainHeight, {}, {}, effects.get());
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    std::function<double(float, float)> terrainHeight{};
    CrtRandom random{Seed};
    glm::vec3 startAngles{};
    std::vector<std::int32_t> sounds;
    bool withOwnRecord = true;
    std::uint8_t ownRecordDepth = 0;
    std::uint8_t ownVolley = 0;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom{1}};
    Client::World::Particles particles;
    Client::World::FrameSprites sprites;
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    std::unique_ptr<Client::World::ModelEffects> effects = std::make_unique<Client::World::ModelEffects>();
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

std::vector<Client::World::Sprite> AliveSprites(Client::World::FrameSprites& sprites)
{
    std::vector<Client::World::Sprite> found;
    for (const Client::World::Sprite& sprite : sprites.Sprites())
        if (sprite.alive)
            found.push_back(sprite);
    return found;
}

// La partícula que pide un paso, repetida en un pool gemelo con `replay`, que avanza los rand() como el original.
Client::World::Particle Twin(CrtRandom& replay, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                             const glm::vec3& angles, const glm::vec3& light, float scale)
{
    auto twin = std::make_unique<Client::World::Particles>();
    const auto made = twin->CreateChecked(type, position, angles, light, subtype, scale, 0.0f, replay);
    EXPECT_TRUE(made.HasValue() && made.Value().has_value());
    return twin->All()[0];
}

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

std::array<std::uint32_t, 3> Bits(const glm::vec3& value)
{
    return {Bits(value.x), Bits(value.y), Bits(value.z)};
}

// Todos los campos, bit a bit.
void ExpectSameParticle(const Client::World::Particle& made, const Client::World::Particle& twin)
{
    EXPECT_EQ(made.alive, twin.alive);
    EXPECT_EQ(made.type, twin.type);
    EXPECT_EQ(made.texture, twin.texture);
    EXPECT_EQ(made.subtype, twin.subtype);
    EXPECT_EQ(Bits(made.scale), Bits(twin.scale));
    EXPECT_EQ(Bits(made.position), Bits(twin.position));
    EXPECT_EQ(Bits(made.angles), Bits(twin.angles));
    EXPECT_EQ(Bits(made.light), Bits(twin.light));
    EXPECT_EQ(made.field38, twin.field38);
    EXPECT_EQ(made.life, twin.life);
    EXPECT_EQ(made.owner, twin.owner);
    EXPECT_EQ(Bits(made.rotation), Bits(twin.rotation));
    EXPECT_EQ(Bits(made.field48), Bits(twin.field48));
    EXPECT_EQ(made.moves, twin.moves);
    EXPECT_EQ(Bits(made.verticalSpeed), Bits(twin.verticalSpeed));
    EXPECT_EQ(Bits(made.velocity), Bits(twin.velocity));
    EXPECT_EQ(Bits(made.rotationFactor), Bits(twin.rotationFactor));
    EXPECT_EQ(made.field64, twin.field64);
    EXPECT_EQ(Bits(made.start), Bits(twin.start));
}

std::size_t AliveParticles(Client::World::Particles& particles)
{
    std::size_t count = 0;
    for (const Client::World::Particle& particle : particles.All())
        count += particle.alive ? 1 : 0;
    return count;
}

// 0x53265e y la tabla 0x534f18: los subtipos sin rand() escriben sus campos; la mezcla es 3 salvo donde la pisan (en
// la /3, la /0xd y la /0x13 sólo el 0x613; en la /0x12 y la /0x14 sólo el 0x6ee).
TEST(SpiritJointsTest, TheFixedSubtypesWriteTheirFields)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        std::uint8_t blend;
        std::uint8_t faces;
        float speed;
        std::int32_t life;
        std::int32_t points;
        glm::vec3 light;
        bool keepsTheStart;
    };
    const glm::vec3 white{1.0f};
    const std::array cases{
        Case{0x613, 1, 3, 3, 70.0f, 49, 6, white, false},
        Case{0x613, 3, 1, 3, 140.0f, 49, 10, {1.0f, 0.5f, Tenth}, false},
        Case{0x6ee, 3, 3, 3, 140.0f, 49, 10, white, false},
        Case{0x613, 0xd, 1, 3, 40.0f, 20, 10, {1.0f, std::bit_cast<float>(0x3f19999au), 0.4f}, false},
        Case{0x6ee, 0xd, 3, 3, 40.0f, 20, 10, {1.0f, std::bit_cast<float>(0x3f19999au), 0.4f}, false},
        Case{0x613, 2, 1, 2, 50.0f, 20, 3, glm::vec3(0.5f), true},
        Case{0x613, 0x15, 1, 2, 50.0f, 20, 3, glm::vec3(0.5f), true},
        Case{0x613, 7, 3, 0, 10.0f, 20, 5, {0.3f, 0.3f, 1.0f}, true},
        Case{0x613, 8, 3, 3, 0.0f, 49, 15, white, false},
        Case{0x613, 0xa, 3, 3, 0.0f, 20, 6, white, false},
        Case{0x613, 0xb, 3, 3, 0.0f, 50, 1, white, false},
        Case{0x613, 0xc, 3, 3, 0.0f, 30, 1, white, false},
        Case{0x613, 0xf, 3, 3, 0.0f, 20, 1, white, false},
        Case{0x613, 0x10, 3, 3, 0.0f, 50, 1, white, false},
        Case{0x613, 0x11, 3, 3, 0.0f, 100, 1, white, false},
        Case{0x613, 0x12, 3, 3, 50.0f, 39, 15, glm::vec3(std::bit_cast<float>(0x3f333333u)), false},
        Case{0x6ee, 0x12, 1, 3, 50.0f, 39, 15, glm::vec3(std::bit_cast<float>(0x3f333333u)), false},
        Case{0x613, 0x13, 1, 3, 70.0f, 49, 9, {Tenth, 0.5f, 0.2f}, false},
        Case{0x6ee, 0x14, 1, 3, 0.0f, 49, 15, white, false},
        Case{0x613, 0x16, 3, 3, 0.0f, 0, 0, white, false},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << std::hex << probe.type << "/" << probe.subtype);
        Pool pool;
        const auto created = pool.Create(probe.type, probe.subtype, {10.0f, 20.0f, 30.0f}, 25.0f);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.texture, probe.type);
        EXPECT_EQ(joint.blend, probe.blend);
        EXPECT_EQ(joint.drawEnabled, probe.faces);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.maximumCount, probe.points);
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.width, probe.subtype == 0x16 ? 0.0f : 25.0f);
        if (probe.keepsTheStart)
        {
            EXPECT_EQ(joint.pathIncrement, glm::vec3(10.0f, 20.0f, 30.0f));
        }
        EXPECT_EQ(pool.random.State(), CrtRandom(Seed).State());
        EXPECT_EQ(pool.sounds, probe.subtype == 1 ? std::vector<std::int32_t>{0x3e} : std::vector<std::int32_t>{});
    }
    // 0x5334bb: la /0xe nace con la x de los ángulos en 20.
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0xe, {}, 25.0f).HasValue());
    EXPECT_EQ(pool.joints[0].angles.x, 20.0f);
    EXPECT_EQ(pool.joints[0].life, 10);
}

// 0x5331e8..0x533201: la /6 sortea la key con rand() % 5 y sin 0 pierde la cola; la skill 1 la deja con velocidad 10 y
// la luz 0,5 (0x533218).
TEST(SpiritJointsTest, TheSixthDrawsItsKeyAndLosesItsTailWithoutZero)
{
    for (const bool zero : {true, false})
    {
        SCOPED_TRACE(zero);
        Pool pool;
        pool.random = CrtRandom(SeedWhere(0, [zero](int value) { return (value % 5 == 0) == zero; }));
        CrtRandom expected = pool.random;
        ASSERT_TRUE(pool.Create(0x613, 6, {}, 25.0f, nullptr, JointOwnerKind::ByType, 1).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.key, expected.Next() % 5);
        EXPECT_EQ(joint.tailEnabled, zero);
        EXPECT_EQ(joint.speed, 10.0f);
        EXPECT_EQ(joint.light, glm::vec3(0.5f));
        EXPECT_EQ(joint.blend, 1);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(pool.random.State(), expected.State());
    }
}

// 0x532675 y 0x532bab: la /0 y la /5 copian el +0xf04 del registro propio a +0x9eb con un acceso; el cierre que lo
// deja en 0 saca 3.848 rand(). Sin el registro, cortan. Con el pool lleno, nada.
TEST(SpiritJointsTest, TheVolleySubtypesReadTheOwnRecord)
{
    for (const std::int32_t subtype : {0, 5})
    {
        SCOPED_TRACE(subtype);
        for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
        {
            Pool pool;
            pool.ownRecordDepth = depth;
            pool.ownVolley = 0x2a;
            ASSERT_TRUE(pool.Create(0x613, subtype, {}, 80.0f).HasValue());
            const JointState& joint = pool.joints[0];
            EXPECT_EQ(joint.volley, 0x2a);
            EXPECT_EQ(pool.ownRecordDepth, depth);
            CrtRandom expected(Seed);
            if (depth == 0)
                expected.Discard(Client::Render::Models::OwnRecordCloseRandoms);
            EXPECT_EQ(pool.random.State(), expected.State());
            EXPECT_EQ(joint.blend, subtype == 0 ? 3 : 1);
            EXPECT_EQ(joint.speed, subtype == 0 ? 70.0f : 30.0f);
            EXPECT_EQ(joint.life, 49);
            EXPECT_EQ(joint.maximumCount, subtype == 0 ? 6 : 12);
            EXPECT_EQ(joint.width, 80.0f);
        }
        Pool without;
        without.withOwnRecord = false;
        const auto cut = without.Create(0x613, subtype, {}, 80.0f);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_TRUE(cut.Error().starts_with(subtype == 0 ? "0x532675" : "0x532bab")) << cut.Error();
        Pool full;
        for (JointState& joint : full.joints)
            joint.alive = true;
        const auto none = full.Create(0x613, subtype, {}, 80.0f);
        ASSERT_TRUE(none.HasValue());
        EXPECT_FALSE(none.Value().has_value());
        EXPECT_EQ(full.random.State(), CrtRandom(Seed).State());
    }
}

// 0x5332b8 y 0x5333f6 con la tabla 0x534f18 compartida: la 6ee/4 nace como la 613/4 (la viga del ayudante, sin
// velocidad) y la 613/9 como el meteoro 6ee/9; los pasos son los mismos (la /4 sin caso, la /9 con dos rand()).
TEST(SpiritJointsTest, TheFourthAndTheNinthShareTheirTable)
{
    Pool beam;
    ASSERT_TRUE(beam.Create(0x6ee, 4, {}, 20.0f).HasValue());
    EXPECT_EQ(beam.joints[0].speed, 0.0f);
    EXPECT_EQ(beam.joints[0].maximumCount, 10);
    EXPECT_EQ(beam.joints[0].light, glm::vec3(std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e99999au), 1));
    ASSERT_TRUE(beam.Step().HasValue());
    EXPECT_EQ(beam.random.State(), CrtRandom(Seed).State());

    Pool meteor;
    ASSERT_TRUE(meteor.Create(0x613, 9, {}, 20.0f).HasValue());
    EXPECT_EQ(meteor.joints[0].blend, 3);
    EXPECT_EQ(meteor.joints[0].drawEnabled, 2);
    EXPECT_EQ(meteor.joints[0].angles, glm::vec3(-90.0f, 0.0f, 0.0f));
    EXPECT_EQ(meteor.joints[0].life, 20);
    EXPECT_EQ(meteor.joints[0].maximumCount, 15);
    ASSERT_TRUE(meteor.Step().HasValue());
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(meteor.random.State(), expected.State());
    EXPECT_EQ(meteor.joints[0].speed, 11.0f);
}

// 0x538d99..0x538dda: la /1 avanza 70 con sus ángulos, suelta el sprite 0x5ac de escala 4 con la luz (0,8; 0,4; 1) y el
// giro rand() % 360, y pierde una vida.
TEST(SpiritJointsTest, TheSparkStepsWithItsSprite)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 1, {100.0f, 200.0f, 300.0f}, 60.0f).HasValue());
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 130.0f, 300.0f));
    EXPECT_EQ(joint.life, 48);
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(sprites[0].light, glm::vec3(std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3ecccccdu), 1.0f));
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[0].position, joint.position);
    EXPECT_EQ(pool.random.State(), expected.State());
}

// 0x539586..0x539775: la /2 suelta en su primer paso el sprite de escala 4 con la luz (1; 0,5; 0,1) y sube la
// velocidad 5; con vida 19 sortea los ángulos y un punto alrededor de la partida y suelta allí la gota 0x60f/2 de ancho
// 40, que nace con vz en rand() % 20 + 35 y vida rand() % 50 + 25 y, en un lugar de después, pasa en la misma vuelta.
TEST(SpiritJointsTest, TheCrossDropsItsDropOnItsSecondStep)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 2, {100.0f, 200.0f, 300.0f}, 60.0f).HasValue());
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const JointState& cross = pool.joints[0];
    EXPECT_EQ(cross.speed, 55.0f);
    EXPECT_EQ(cross.life, 19);
    auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f, 0.5f, Tenth));
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_FALSE(pool.joints[1].alive);

    pool.sprites.Clear();
    ASSERT_TRUE(pool.Step().HasValue());
    const float turn = static_cast<float>(expected.Next() % 360);
    const float x = F32((static_cast<double>(expected.Next() % 200) + 100.0) - 100.0);
    const float y = F32((static_cast<double>(expected.Next() % 200) + 200.0) - 100.0);
    const float fall = static_cast<float>(expected.Next() % 20 + 35);
    const std::int32_t life = expected.Next() % 50 + 25;
    const float rotation = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(pool.random.State(), expected.State());
    const JointState& drop = pool.joints[1];
    ASSERT_TRUE(drop.alive);
    EXPECT_EQ(drop.type, 0x60f);
    EXPECT_EQ(drop.subtype, 2);
    EXPECT_EQ(drop.angles, glm::vec3(0.0f, 0.0f, turn));
    EXPECT_EQ(drop.targetPosition, glm::vec3(x, y, 100.0f));
    EXPECT_EQ(drop.width, 40.0f);
    EXPECT_EQ(drop.light, glm::vec3(1.0f));
    EXPECT_EQ(drop.life, life - 1);
    if (life > 25)
    {
        EXPECT_EQ(drop.velocity.z, fall);
        EXPECT_EQ(drop.position, glm::vec3(x, y, 100.0f));
    }
    sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].rotation, rotation);
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(cross.speed, 60.0f);
    EXPECT_EQ(cross.life, 18);
}

// 0x53b156..0x53b185 y 0x53b339: con vida de 25 o menos, la gota baja sobre su destino con vz + 5; con menos de 10, la
// luz por 0,7692308f.
TEST(SpiritJointsTest, TheDropFallsOnItsTarget)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x60f, 2, {50.0f, 60.0f, 70.0f}, 40.0f).HasValue());
    JointState& drop = pool.joints[0];
    drop.life = 9;
    drop.position = {1.0f, 2.0f, 70.0f};
    drop.velocity.z = 36.0f;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(drop.position, glm::vec3(50.0f, 60.0f, 111.0f));
    EXPECT_EQ(drop.velocity.z, 41.0f);
    EXPECT_EQ(drop.light, glm::vec3(F32(1.0 * D(std::bit_cast<float>(0x3f44ec4fu)))));
    EXPECT_EQ(drop.life, 8);
}

// 0x539600..0x539641: con vida de menos de 10, la luz propia por 0,8333333f es también la del sprite, de escala
// (20 − vida) / 5 + 4. La /6 con una skill que no es 0 ni 1 deja la luz local sin escribir: corta.
TEST(SpiritJointsTest, TheCrossFadesBelowTenAndCutsWithoutLight)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0x15, {}, 60.0f).HasValue());
    pool.joints[0].life = 9;
    ASSERT_TRUE(pool.Step().HasValue());
    const float faded = F32(0.5 * D(std::bit_cast<float>(0x3f555555u)));
    EXPECT_EQ(pool.joints[0].light, glm::vec3(faded));
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].light, glm::vec3(faded));
    EXPECT_EQ(sprites[0].scale, 6.0f);

    Pool cut;
    ASSERT_TRUE(cut.Create(0x613, 6, {}, 60.0f, nullptr, JointOwnerKind::ByType, 2).HasValue());
    const auto stepped = cut.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_TRUE(stepped.Error().starts_with("0x53975b")) << stepped.Error();
}

// 0x539941..0x5399a2 y 0x5399a2..0x539a09: la /3 avanza 140 y se desvía con 0x53d1a0(lugar, dir, 1): el contador con
// su acceso (4 rand() al cerrarlo en 0) y la dirección de la fase lugar + contador, por 50; después los sprites 0x5ac
// de escala 3 y 0x5fd de escala 1,5 con la luz (1; 0,5; 0,1).
TEST(SpiritJointsTest, TheBoltWandersWithTheSharedCounter)
{
    Pool pool;
    pool.cloth.logicSteps = 1234;
    ASSERT_TRUE(pool.Create(0x613, 3, {1000.0f, 2000.0f, 300.0f}, 50.0f).HasValue());
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    expected.Discard(Client::Render::Models::SmallLogicalCloseRandoms);
    const auto direction = Client::Render::Models::Detail::FaunaDirectionPC53(1234);
    ASSERT_TRUE(direction.has_value());
    const JointState& bolt = pool.joints[0];
    const glm::vec3 wandered{F32(D((*direction)[0]) * 50.0 + 1000.0), F32(D(F32(D((*direction)[1]) * 50.0)) + 1860.0),
                             F32(D(F32(D((*direction)[2]) * 50.0)) + 300.0)};
    EXPECT_EQ(bolt.position, wandered);
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].scale, 3.0f);
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[1].texture, 0x5fd);
    EXPECT_EQ(sprites[1].scale, 1.5f);
    EXPECT_EQ(sprites[1].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[1].light, glm::vec3(1.0f, 0.5f, Tenth));
    EXPECT_EQ(pool.random.State(), expected.State());

    // Con el contador abierto, sin los 4 rand().
    Pool open;
    open.cloth.openCounterAccesses = 1;
    ASSERT_TRUE(open.Create(0x613, 3, {}, 50.0f).HasValue());
    CrtRandom twoDraws = open.random;
    twoDraws.Discard(2);
    ASSERT_TRUE(open.Step().HasValue());
    EXPECT_EQ(open.random.State(), twoDraws.State());
}

// 0x539789..0x53993f: la /0xd con vida de 28 o menos y un personaje de dueño gira 20 en x con rand() & 1 en 0, busca
// un punto ±70 alrededor del dueño y 50 más arriba con 0x4e9810 de a (velocidad − 20), sube la velocidad 1 y usa su
// propia luz con escala (vida / 10 + 1) · 2. Sin dueño, la luz local queda sin escribir: corta.
TEST(SpiritJointsTest, TheThirteenthSeeksItsOwner)
{
    ActorState owner = Owner({1500.0f, 1500.0f, 100.0f});
    Pool pool;
    pool.random = CrtRandom(SeedWhere(0, [](int value) { return (value & 1) == 0; }));
    ASSERT_TRUE(pool.Create(0x613, 0xd, {1000.0f, 1000.0f, 100.0f}, 40.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& seeker = pool.joints[0];
    seeker.life = 28;
    seeker.speed = 0.0f;
    CrtRandom expected = pool.random;
    glm::vec3 angles = seeker.angles;
    const glm::vec3 position = seeker.position;
    ASSERT_TRUE(pool.Step().HasValue());
    static_cast<void>(expected.Next());
    angles.x = F32(D(angles.x) - 20.0);
    const glm::vec3 target{F32((static_cast<double>(expected.Next() % 150) - 70.0) + 1500.0),
                           F32((static_cast<double>(expected.Next() % 150) - 70.0) + 1500.0), 150.0f};
    const double distance = Client::Render::Models::MoveToward(position, angles, target, -20.0f);
    EXPECT_EQ(seeker.angles, angles);
    EXPECT_EQ(seeker.speed, 1.0f);
    const std::int32_t life = distance < 50.0 ? 26 : 27;
    EXPECT_EQ(seeker.life, life);
    const auto sprites = AliveSprites(pool.sprites);
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].scale, 6.0f);
    EXPECT_EQ(sprites[0].light, seeker.light);
    EXPECT_EQ(sprites[0].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(sprites[1].rotation, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(pool.random.State(), expected.State());

    Pool ownerless;
    ASSERT_TRUE(ownerless.Create(0x613, 0xd, {}, 40.0f).HasValue());
    ownerless.joints[0].life = 20;
    const auto stepped = ownerless.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_TRUE(stepped.Error().starts_with("0x5399cd")) << stepped.Error();
}

// 0x53909f..0x539125: el 0x6ee/0xe (los 24 de Kundun, 0x43d19f) baja 2 la x de los ángulos y con rand() % 5 crea la
// 0x6f2/2 (0), la 0x6f3/2 (1 o 2) o la 0x6f4/4, de escala 1 en su posición, con sus ángulos y su luz (+0x48); sigue a
// la cola. Cada partícula se repite en un pool gemelo con el mismo CrtRandom.
TEST(SpiritJointsTest, TheKundunSparkTiltsAndPicksItsParticle)
{
    struct Case
    {
        std::int32_t pick;
        std::int32_t type;
        std::int32_t subtype;
    };
    for (const Case& each :
         {Case{0, 0x6f2, 2}, Case{1, 0x6f3, 2}, Case{2, 0x6f3, 2}, Case{3, 0x6f4, 4}, Case{4, 0x6f4, 4}})
    {
        SCOPED_TRACE(each.pick);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x6ee, 0xe, {10.0f, 20.0f, 30.0f}, 30.0f).HasValue());
        pool.joints[0].angles.x = 20.0f;
        const std::uint32_t seed = SeedWhere(0, [&each](std::int32_t r) { return r % 5 == each.pick; });
        pool.random = CrtRandom(seed);
        const JointState before = pool.joints[0];

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(pool.joints[0].angles.x, 18.0f);
        CrtRandom replay(seed);
        static_cast<void>(replay.Next());
        const auto twin = Twin(replay, each.type, each.subtype, before.position,
                               {18.0f, before.angles.y, before.angles.z}, before.light, 1.0f);
        ExpectSameParticle(pool.particles.All()[0], twin);
        EXPECT_EQ(AliveParticles(pool.particles), 1U);
        EXPECT_EQ(pool.random.State(), replay.State());
        EXPECT_EQ(pool.joints[0].life, before.life - 1);
    }
}

// 0x538e1e..0x538e7f: la /8 sube con la x en 110 y velocidad 10 hasta la vida 42; después va con velocidad 50 desde 180
// de a rand() % 3 + 2. 0x538e89: la /0xa gira 50 en z por paso con velocidad 50.
TEST(SpiritJointsTest, TheEighthArcsAndTheTenthTurns)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 8, {}, 40.0f).HasValue());
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(pool.joints[0].angles.x, 110.0f);
    EXPECT_EQ(pool.joints[0].speed, 10.0f);
    pool.joints[0].life = 41;
    CrtRandom expected = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(pool.joints[0].speed, 50.0f);
    EXPECT_EQ(pool.joints[0].angles.x, F32((static_cast<double>(expected.Next() % 3) + 180.0) + 2.0));
    EXPECT_EQ(pool.random.State(), expected.State());

    Pool turning;
    ASSERT_TRUE(turning.Create(0x613, 0xa, {}, 40.0f).HasValue());
    ASSERT_TRUE(turning.Step().HasValue());
    EXPECT_EQ(turning.joints[0].angles.z, -50.0f);
    EXPECT_EQ(turning.joints[0].speed, 50.0f);
    EXPECT_EQ(turning.random.State(), CrtRandom(Seed).State());
}

// 0x539a0e: con ancho 80 la /0, la /5 y la /0x13 cortan en el efecto 0xd0 (0x539a48); con otro, la /0x13 suelta el
// destello 0x11b/0 (0x539b08: vida 1, escala 2,3f, sin rand()) y sigue como la /0; sin la tabla de efectos, corta ahí.
TEST(SpiritJointsTest, TheWideVolleyStepIsCut)
{
    for (const std::int32_t subtype : {0, 5, 0x13})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ActorState owner = Owner({});
        ASSERT_TRUE(pool.Create(0x613, subtype, {}, 80.0f, &owner, JointOwnerKind::Actor).HasValue());
        const auto stepped = pool.Step();
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_TRUE(stepped.Error().starts_with("0x539a48")) << stepped.Error();
    }
    Pool thin;
    ActorState owner = Owner({});
    thin.terrainHeight = [](float, float) { return 150.0; };
    ASSERT_TRUE(thin.Create(0x613, 0x13, {100.0f, 200.0f, 300.0f}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
    const glm::vec3 light = thin.joints[0].light;
    const auto stepped = thin.Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto& spark = thin.effects->Effects()[0];
    ASSERT_TRUE(spark.alive);
    EXPECT_EQ(spark.type, 0x11b);
    EXPECT_EQ(spark.subtype, 0);
    EXPECT_EQ(spark.life, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(spark.transform.scale), 0x40133333u);
    EXPECT_EQ(spark.light, light);
    EXPECT_FALSE(thin.effects->Effects()[1].alive);

    Pool tableless;
    tableless.effects.reset();
    ASSERT_TRUE(tableless.Create(0x613, 0x13, {}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto cut = tableless.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x539b08")) << cut.Error();
}

// 0x539b10..0x539cc7: la /0 y la /5 de otro ancho van hacia el dueño 80 más arriba, la /0 sacudida con dos rand(); el
// terreno las sube a menos de 100 y las baja a más de 400; la luz es la vida · 0,1f y la luz dinámica, un rand() más.
TEST(SpiritJointsTest, TheThinVolleyFollowsItsOwnerOverTheGround)
{
    for (const std::int32_t subtype : {0, 5})
        for (const double ground : {20.0, 150.0, -300.0})
        {
            SCOPED_TRACE(subtype);
            SCOPED_TRACE(ground);
            Pool pool;
            pool.terrainHeight = [ground](float, float) { return ground; };
            pool.ownRecordDepth = 1; // Sin el cierre del registro propio (3.848 rand()) en el alta.
            ActorState owner = Owner({900.0f, 700.0f, 120.0f});
            ASSERT_TRUE(pool.Create(0x613, subtype, {1000.0f, 1100.0f, 200.0f}, 20.0f, &owner, JointOwnerKind::Actor)
                            .HasValue());
            JointState joint = pool.joints[0];
            const float dx = F32(D(joint.position.x) - D(joint.targetPosition.x));
            const float dy = F32(D(joint.position.y) - D(joint.targetPosition.y));
            const auto matrix = Client::Render::Models::OriginalAngleMatrix(joint.angles);
            const auto moved = Client::Render::Models::OriginalVectorRotate({0.0f, -joint.speed, 0.0f}, matrix);
            for (int axis = 0; axis < 3; ++axis)
                joint.position[axis] = F32(D(joint.position[axis]) + moved[axis]);
            const glm::vec3 target{900.0f, 700.0f, 200.0f};
            static_cast<void>(
                Client::Render::Models::MoveToward(joint.position, joint.angles, target, subtype == 5 ? 2.0f : 10.0f));
            CrtRandom expected(Seed);
            float velocityX = 0.0f;
            if (subtype == 0)
            {
                const int first = (expected.Next() & 31) - 16;
                const int second = (expected.Next() & 31) - 16;
                velocityX = F32(first * D(0.2f));
                const double shake = second * D(0.8f);
                joint.angles.x = F32(D(joint.angles.x) + D(velocityX));
                joint.angles.z = F32(shake + D(joint.angles.z));
                velocityX = F32(D(velocityX) * D(0.6f));
                joint.velocity.z = F32(shake * D(0.8f));
            }
            if (ground + 100.0 > D(joint.position.z))
            {
                velocityX = 0.0f;
                joint.angles.x = -5.0f;
            }
            if (!(ground + 400.0 >= D(joint.position.z)))
            {
                velocityX = 0.0f;
                joint.angles.x = 5.0f;
            }
            expected.Discard(1);
            ASSERT_TRUE(pool.Step().HasValue());
            const JointState& stepped = pool.joints[0];
            EXPECT_EQ(stepped.targetPosition, target);
            EXPECT_EQ(stepped.position, joint.position);
            EXPECT_EQ(stepped.angles, joint.angles);
            EXPECT_EQ(stepped.velocity.x, velocityX);
            EXPECT_EQ(stepped.velocity.z, joint.velocity.z);
            const float distance = F32(std::sqrt(D(dy) * D(dy) + D(dx) * D(dx)));
            EXPECT_EQ(stepped.field9d4, D(joint.speed) * 2 >= D(distance) ? 1 : 0);
            EXPECT_EQ(stepped.light, glm::vec3(F32(49 * D(Tenth))));
            EXPECT_EQ(stepped.life, 48);
            EXPECT_EQ(pool.random.State(), expected.State());
        }
    // Sin la altura del terreno, corta en 0x5f93c0.
    Pool blind;
    ActorState owner = Owner({});
    ASSERT_TRUE(blind.Create(0x613, 5, {}, 20.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto cut = blind.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x539c19")) << cut.Error();
}

// 0x538f45..0x538feb: la /0xb (vida 50) pone la vida en la x de los ángulos, deja la 0x5da/5 de 0,9f con la luz 1 en su
// posición, saca rand() % 200 (con 0, el joint 0x613/0xc ahí) y avanza 20 por FCOS y FSIN de la z (30, sin pasar a
// radianes). 0x539328..0x539384: la /0x10 igual con la 0x6f4/4 de escala 1 y su luz, sin rand() propio, y 30. Los bits
// de la posición salen de una cuenta aparte (seno y coseno exactos a 64 bits, productos y sumas a 53, fstp a float).
TEST(SpiritJointsTest, TheStillOnesLeaveTheirParticlesAndDrift)
{
    struct Case
    {
        std::int32_t subtype;
        std::int32_t type;
        std::int32_t particleSubtype;
        std::uint32_t scale;
        std::array<std::uint32_t, 2> drifted;
    };
    for (const Case& each : {Case{0xb, 0x5da, 5, 0x3f666666u, {0x42ce2b89u, 0x43343d47u}},
                             Case{0x10, 0x6f4, 4, 0x3f800000u, {0x42d1414du, 0x432a5bebu}}})
    {
        SCOPED_TRACE(each.subtype);
        Pool pool;
        pool.startAngles = {0.0f, 0.0f, 30.0f};
        ASSERT_TRUE(pool.Create(0x613, each.subtype, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
        ASSERT_EQ(pool.joints[0].life, 50);
        const JointState before = pool.joints[0];
        const CrtRandom start = pool.random;

        ASSERT_TRUE(pool.Step().HasValue());

        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.angles.x, 50.0f);
        CrtRandom replay = start;
        const glm::vec3 light = each.subtype == 0xb ? glm::vec3(1.0f) : before.light;
        const auto twin = Twin(replay, each.type, each.particleSubtype, before.position, {50.0f, 0.0f, 30.0f}, light,
                               std::bit_cast<float>(each.scale));
        ExpectSameParticle(pool.particles.All()[0], twin);
        if (each.subtype == 0xb)
        {
            EXPECT_NE(replay.Next() % 200, 0);
        }
        EXPECT_EQ(pool.random.State(), replay.State());
        EXPECT_FALSE(pool.joints[1].alive);
        EXPECT_EQ(Bits(joint.position.x), each.drifted[0]);
        EXPECT_EQ(Bits(joint.position.y), each.drifted[1]);
        EXPECT_EQ(joint.position.z, 0.0f);
    }
}

// 0x538f96..0x538fc3: con rand() % 200 en 0, la /0xb suelta el joint 0x613/0xc (vida 30) desde la copia de su
// posición, con sus ángulos (la x ya en la vida), sin dueño y de ancho 0,9f; desde un 0x6ee, también un 0x613.
TEST(SpiritJointsTest, TheStillOneSometimesReleasesASmoke)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(Twin(probe, 0x5da, 5, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 0.9f));
        if (probe.Next() % 200 == 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x6ee, 0xb, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    pool.joints[3] = pool.joints[0];
    pool.joints[0].alive = false;
    pool.random = CrtRandom(seed);

    ASSERT_TRUE(pool.Step().HasValue());

    const JointState& smoke = pool.joints[0];
    ASSERT_TRUE(smoke.alive);
    EXPECT_EQ(smoke.type, 0x613);
    EXPECT_EQ(smoke.subtype, 0xc);
    EXPECT_EQ(smoke.ownerContext, nullptr);
    EXPECT_EQ(smoke.position, (glm::vec3{100.0f, 200.0f, 0.0f}));
    EXPECT_EQ(smoke.angles, (glm::vec3{50.0f, 0.0f, 30.0f}));
    EXPECT_EQ(Bits(smoke.width), 0x3f666666u);
    EXPECT_EQ(smoke.life, 30);
    EXPECT_FALSE(pool.joints[1].alive);
    EXPECT_EQ(Bits(pool.joints[3].position.x), 0x42ce2b89u);
}

// 0x538ff9..0x539091: la /0xc saca rand() & 31 y en su posición deja la 0x5f5/0 de escala (r + 48) · 0,01f (bits de una
// cuenta aparte) con la luz (1; 0,2f; 0,1f) y después la 0x5da/6 de 0,6f con la luz 1, con sus ángulos.
TEST(SpiritJointsTest, TheSmokeLeavesTwoParticles)
{
    constexpr std::array<std::uint32_t, 32> Scales{
        0x3ef5c28fu, 0x3efae147u, 0x3f000000u, 0x3f028f5cu, 0x3f051eb8u, 0x3f07ae14u, 0x3f0a3d70u, 0x3f0ccccdu,
        0x3f0f5c29u, 0x3f11eb85u, 0x3f147ae1u, 0x3f170a3du, 0x3f199999u, 0x3f1c28f6u, 0x3f1eb852u, 0x3f2147aeu,
        0x3f23d70au, 0x3f266666u, 0x3f28f5c2u, 0x3f2b851eu, 0x3f2e147bu, 0x3f30a3d7u, 0x3f333333u, 0x3f35c28fu,
        0x3f3851ebu, 0x3f3ae147u, 0x3f3d70a4u, 0x3f400000u, 0x3f428f5cu, 0x3f451eb8u, 0x3f47ae14u, 0x3f4a3d70u};
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xc, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    const JointState before = pool.joints[0];
    CrtRandom replay = pool.random;

    ASSERT_TRUE(pool.Step().HasValue());

    const std::int32_t size = replay.Next() & 31;
    const float scale = std::bit_cast<float>(Scales[static_cast<std::size_t>(size)]);
    const auto smoke = Twin(replay, 0x5f5, 0, before.position, before.angles,
                            {1.0f, std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3dcccccdu)}, scale);
    const auto glow =
        Twin(replay, 0x5da, 6, before.position, before.angles, glm::vec3(1.0f), std::bit_cast<float>(0x3f19999au));
    ExpectSameParticle(pool.particles.All()[0], smoke);
    ExpectSameParticle(pool.particles.All()[1], glow);
    EXPECT_EQ(AliveParticles(pool.particles), 2U);
    EXPECT_EQ(pool.random.State(), replay.State());
    EXPECT_EQ(pool.joints[0].position, before.position);
}

// 0x539133..0x53931e: la /0xf suma 30 a la key. Con vida de menos de 35 (la del alta, 20) deja la 0x5d9/7 de escala 1
// con la luz 1 en su posición y saca rand() & 1: con 1, nada más. Siempre avanza k = key · 0,1f + 30 con el seno y el
// coseno de la z (30). Con 35 o más, sólo avanza (sin rand()). Bits de una cuenta aparte.
TEST(SpiritJointsTest, TheSpiralTurnsWithItsKey)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(Twin(probe, 0x5d9, 7, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
        if ((probe.Next() & 1) != 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xf, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    EXPECT_EQ(pool.joints[0].life, 20);
    pool.random = CrtRandom(seed);
    const JointState before = pool.joints[0];

    ASSERT_TRUE(pool.Step().HasValue());

    CrtRandom replay(seed);
    const auto twin = Twin(replay, 0x5d9, 7, before.position, before.angles, glm::vec3(1.0f), 1.0f);
    ExpectSameParticle(pool.particles.All()[0], twin);
    EXPECT_EQ(replay.Next() & 1, 1);
    EXPECT_EQ(pool.random.State(), replay.State());
    EXPECT_FALSE(pool.joints[1].alive);
    EXPECT_EQ(pool.joints[0].key, 29);
    EXPECT_EQ(Bits(pool.joints[0].position.x), 0x4286fcceu);
    EXPECT_EQ(Bits(pool.joints[0].position.y), 0x4342ecd5u);

    pool.joints[0].life = 40;
    pool.joints[0].position = {100.0f, 200.0f, 0.0f};
    const CrtRandom still = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    // k = 59 · 0,1f + 30 retenido; bits de una cuenta aparte (seno y coseno de 30 exactos a 64 bits).
    EXPECT_EQ(pool.joints[0].key, 59);
    EXPECT_EQ(Bits(pool.joints[0].position.x), 0x42810f30u);
    EXPECT_EQ(Bits(pool.joints[0].position.y), 0x4342765eu);
    EXPECT_EQ(pool.random.State(), still.State());
}

// 0x5391a9..0x53923a: con rand() & 1 en 0, la copia de la posición pasa a (x − FSIN(z) · k, FCOS(z) · k + y, 350) con
// k = 40 − key · 0,1f retenido y suelta dos joints 0x613/0xe (vida 10, la x de los ángulos en 20 por su alta) con los
// ángulos (0, 0, 0) y (0, 0, 3), de ancho 0,9f; caen en lugares anteriores y no pasan en esta vuelta. En (0, 0) con la
// z en 2, el k retenido da (0xc206f093, 0xc1770657) y guardado en float daría (0xc206f092, 0xc1770656). Bits aparte.
TEST(SpiritJointsTest, TheSpiralSometimesReleasesTwoSplashes)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(Twin(probe, 0x5d9, 7, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
        if ((probe.Next() & 1) == 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 2.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xf, {0.0f, 0.0f, 0.0f}, 30.0f).HasValue());
    pool.joints[3] = pool.joints[0];
    pool.joints[0].alive = false;
    pool.random = CrtRandom(seed);

    ASSERT_TRUE(pool.Step().HasValue());

    for (std::size_t piece = 0; piece < 2; ++piece)
    {
        SCOPED_TRACE(piece);
        const JointState& splash = pool.joints[piece];
        ASSERT_TRUE(splash.alive);
        EXPECT_EQ(splash.type, 0x613);
        EXPECT_EQ(splash.subtype, 0xe);
        EXPECT_EQ(splash.ownerContext, nullptr);
        EXPECT_EQ(Bits(splash.position.x), 0xc206f093u);
        EXPECT_EQ(Bits(splash.position.y), 0xc1770657u);
        EXPECT_EQ(splash.position.z, 350.0f);
        EXPECT_EQ(splash.angles, (glm::vec3{20.0f, 0.0f, piece == 0 ? 0.0f : 3.0f}));
        EXPECT_EQ(Bits(splash.width), 0x3f666666u);
        EXPECT_EQ(splash.life, 10);
    }
    EXPECT_FALSE(pool.joints[2].alive);
    EXPECT_EQ(pool.joints[3].key, 29);
    EXPECT_EQ(Bits(pool.joints[3].position.x), 0x41ef53bcu);
    EXPECT_EQ(Bits(pool.joints[3].position.y), 0x415b0f48u);

    Pool other;
    int otherOwner = 0;
    ASSERT_TRUE(other.Create(0x613, 0xf, {}, 30.0f, &otherOwner, JointOwnerKind::ByType).HasValue());
    const auto cut = other.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x539249")) << cut.Error();
}

// 0x539392..0x53941e: la /0x11 con vida 100 suelta 120 joints 0x613/0x10 de ancho 0,9f con los ángulos (0, 0, 3 · i).
// Los que caen en lugares de más adelante pasan en la misma vuelta (y dejan su 0x6f4/4); acá caen todos antes.
TEST(SpiritJointsTest, TheBurstReleasesItsPieces)
{
    Pool pool;
    pool.joints = std::vector<JointState>(130);
    ASSERT_TRUE(pool.Create(0x613, 0x11, {10.0f, 20.0f, 30.0f}, 30.0f).HasValue());
    EXPECT_EQ(pool.joints[0].life, 100);
    pool.joints[129] = pool.joints[0];
    pool.joints[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    for (std::size_t i = 0; i < 120; ++i)
    {
        SCOPED_TRACE(i);
        const JointState& piece = pool.joints[i];
        ASSERT_TRUE(piece.alive);
        EXPECT_EQ(piece.subtype, 0x10);
        EXPECT_EQ(piece.angles, glm::vec3(0.0f, 0.0f, static_cast<float>(i) * 3.0f));
        EXPECT_EQ(piece.width, std::bit_cast<float>(0x3f666666u));
        EXPECT_EQ(piece.position, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(piece.life, 50);
    }
    EXPECT_FALSE(pool.joints[120].alive);
    EXPECT_EQ(pool.joints[129].life, 99);
    // En el paso siguiente, cada pieza (0x613/0x10) deja su 0x6f4/4 y avanza 30 por el coseno y el seno de su z.
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(AliveParticles(pool.particles), 120U);
    EXPECT_EQ(pool.joints[0].position, glm::vec3(40.0f, 20.0f, 30.0f));
    EXPECT_EQ(pool.joints[0].angles.x, 50.0f);
}

// 0x53942c..0x539500: la /0x12 sube 3 la velocidad, mueve la key de a 6 y apunta −90 − key en x y 10 más en z; deja la
// 0x5f2/0x13 de escala 7 con la luz 1 en su posición y sigue a la cola. Con un personaje de dueño corta antes, en
// 0x539494 (sólo el efecto con modelo del 0x125 está portado, test_high_effect_families). La /0x14 sube como la /8.
TEST(SpiritJointsTest, TheSwingAndTheTwentiethStep)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0x12, {}, 30.0f).HasValue());
    const CrtRandom start = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.speed, 53.0f);
    EXPECT_EQ(joint.key, 5); // 39 % 12 = 3: + 6.
    EXPECT_EQ(joint.angles.x, -95.0f);
    EXPECT_EQ(joint.angles.z, 10.0f);
    EXPECT_EQ(joint.life, 38);
    CrtRandom replay = start;
    const auto twin = Twin(replay, 0x5f2, 0x13, joint.position, joint.angles, glm::vec3(1.0f), 7.0f);
    ExpectSameParticle(pool.particles.All()[0], twin);
    EXPECT_EQ(pool.random.State(), replay.State());

    Pool owned;
    ActorState owner = Owner({});
    ASSERT_TRUE(owned.Create(0x613, 0x12, {}, 30.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto ownerCut = owned.Step();
    ASSERT_FALSE(ownerCut.HasValue());
    EXPECT_TRUE(ownerCut.Error().starts_with("0x539494")) << ownerCut.Error();

    Pool rising;
    ASSERT_TRUE(rising.Create(0x613, 0x14, {}, 30.0f).HasValue());
    ASSERT_TRUE(rising.Step().HasValue());
    EXPECT_EQ(rising.joints[0].angles.x, 110.0f);
    EXPECT_EQ(rising.joints[0].speed, 10.0f);
}

// 0x53c6a0: el dibujo común, con la textura del tipo y la mezcla 3 (resta) o 1 (suma); la /2 sólo la segunda cara.
TEST(SpiritJointsTest, TheJointsDrawWithTheirBlend)
{
    std::array<std::size_t, 2> vertices{};
    for (const std::int32_t subtype : {1, 2})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x613, subtype, {}, 50.0f).HasValue());
        ASSERT_TRUE(pool.Step().HasValue());
        ASSERT_EQ(pool.joints[0].count, 1);
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Client::Scenes::DrawJoints(batch, pool.joints, 0).HasValue());
        ASSERT_TRUE(batch.State().texture.has_value());
        EXPECT_EQ(*batch.State().texture, Client::Render::TextureSlot{0x613});
        EXPECT_EQ(batch.State().blend,
                  subtype == 1 ? Client::Render::ModelBlend::Subtractive : Client::Render::ModelBlend::Additive);
        vertices[static_cast<std::size_t>(subtype - 1)] = batch.Vertices().size();
    }
    EXPECT_GT(vertices[1], 0u);
    EXPECT_EQ(vertices[0], 2 * vertices[1]);
}
} // namespace
