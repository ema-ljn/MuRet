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
#include <string_view>
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
                                          terrainHeight, {}, effects.get());
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

// 0x53909f..0x539125: el 0x6ee/0xe (los 24 de Kundun, 0x43d19f) baja 2 la x de los ángulos y saca rand() % 5 antes de
// su partícula 0x6f2/2, 0x6f3/2 o 0x6f4/4, que corta (CL11).
TEST(SpiritJointsTest, TheKundunSparkTiltsAndDrawsBeforeItsParticle)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x6ee, 0xe, {}, 30.0f).HasValue());
    pool.joints[0].angles.x = 20.0f;
    const auto before = pool.random.State();
    const auto stepped = pool.Step();
    ASSERT_FALSE(stepped.HasValue());
    CrtRandom expected(before);
    const std::int32_t pick = expected.Next() % 5;
    const std::string_view address = pick == 0 ? "0x5390e6" : (pick <= 2 ? "0x539109" : "0x539120");
    EXPECT_TRUE(stepped.Error().starts_with(address)) << stepped.Error();
    EXPECT_EQ(pool.joints[0].angles.x, 18.0f);
    EXPECT_EQ(pool.random.State(), expected.State());
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

// 0x538f45, 0x538ff9 y 0x539328: la /0xb y la /0x10 ponen la vida en la x de los ángulos y la /0xc saca su rand();
// después las tres cortan en su partícula (0x5da/5, 0x5f5/0, 0x6f4/4, del lote de partículas).
TEST(SpiritJointsTest, TheStillOnesCutInTheirParticles)
{
    struct Case
    {
        std::int32_t subtype;
        std::string_view address;
        std::uint32_t randoms;
    };
    for (const Case& each : {Case{0xb, "0x538f8e", 0}, Case{0xc, "0x539058", 1}, Case{0x10, "0x53935c", 0}})
    {
        SCOPED_TRACE(each.subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x613, each.subtype, {}, 30.0f).HasValue());
        const std::int32_t life = pool.joints[0].life;
        const auto stepped = pool.Step();
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_TRUE(stepped.Error().starts_with(each.address)) << stepped.Error();
        if (each.subtype != 0xc)
        {
            EXPECT_EQ(pool.joints[0].angles.x, static_cast<float>(life));
        }
        CrtRandom expected(Seed);
        expected.Discard(each.randoms);
        EXPECT_EQ(pool.random.State(), expected.State());
    }
}

// 0x539133..0x53931e: la /0xf suma 30 a la key; con menos de 35 de vida corta en su partícula 0x5d9/7 y si no avanza
// key · 0,1f + 30 con el seno y el coseno de la z de los ángulos.
TEST(SpiritJointsTest, TheSpiralTurnsWithItsKey)
{
    Pool pool;
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(pool.Create(0x613, 0xf, {100.0f, 200.0f, 0.0f}, 30.0f).HasValue());
    EXPECT_EQ(pool.joints[0].life, 20);
    const auto cut = pool.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x53918a")) << cut.Error();
    EXPECT_EQ(pool.joints[0].key, 29);

    pool.joints[0].life = 40;
    ASSERT_TRUE(pool.Step().HasValue());
    const double radius = 59.0 * D(Tenth) + 30.0;
    const auto sine = Client::Render::Models::Detail::SineDoubleProductPC53(30.0, radius);
    const auto cosine = Client::Render::Models::Detail::CosineDoubleProductPC53(30.0, radius);
    ASSERT_TRUE(sine && cosine);
    EXPECT_EQ(pool.joints[0].key, 59);
    EXPECT_EQ(pool.joints[0].position.x, F32(*sine + 100.0));
    EXPECT_EQ(pool.joints[0].position.y, F32(200.0 - *cosine));
    EXPECT_EQ(pool.random.State(), CrtRandom(Seed).State());
}

// 0x539392..0x53941e: la /0x11 con vida 100 suelta 120 joints 0x613/0x10 de ancho 0,9f con los ángulos (0, 0, 3 · i).
// Los que caen en lugares de más adelante pasan en la misma vuelta (y cortan en su partícula).
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
    // En el paso siguiente, el primero corta en su partícula 0x6f4/4.
    const auto cut = pool.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x53935c")) << cut.Error();
}

// 0x53942c..0x5394fb: la /0x12 sube 3 la velocidad, mueve la key de a 6 y apunta −90 − key en x y 10 más en z; sin
// dueño corta en su partícula 0x5f2/0x13 y con dueño antes, en 0x539494. La /0x14 sube como la /8.
TEST(SpiritJointsTest, TheSwingAndTheTwentiethStep)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x613, 0x12, {}, 30.0f).HasValue());
    const auto cut = pool.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x5394fb")) << cut.Error();
    EXPECT_EQ(pool.joints[0].speed, 53.0f);
    EXPECT_EQ(pool.joints[0].key, 5); // 39 % 12 = 3: + 6.
    EXPECT_EQ(pool.joints[0].angles.x, -95.0f);
    EXPECT_EQ(pool.joints[0].angles.z, 10.0f);
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
