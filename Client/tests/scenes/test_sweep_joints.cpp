#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointKernels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SkillHits.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/WorldTerrain.h"

#include "support/SameBytes.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// El 0x61c/0 (SweepJoints.cpp): el alta 0x5342e4..0x534399 y el paso 0x53b3e1..0x53b623, con el pedido del efecto 0x5be
// (0x52a72a: 0x530860(0x61c, posición + 100 en z, lo mismo, +0x28, 0, +0x108, 150, +0x8a, +0x89, 0, −1)).
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x61cu;
constexpr std::size_t Last = 19;

float F32(double value)
{
    return static_cast<float>(value);
}

struct SweepWorld
{
    // El barrido en el último lugar: los rayos 0x614/3 que suelta caen en los primeros, que este paso ya pasó.
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(glm::vec3 angles, void* owner = nullptr)
    {
        for (std::size_t slot = 0; slot < Last; ++slot)
            joints[slot].alive = true;
        glm::vec3 from = start;
        const Client::World::FishEffectRequest request{.type = 0x61c,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = angles,
                                                       .subtype = 0,
                                                       .ownerContext = owner,
                                                       .scale = 150.0f,
                                                       .key = 7,
                                                       .skill = 3,
                                                       .skillSerial = 0,
                                                       .target = -1};
        auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
        for (std::size_t slot = 0; slot < Last; ++slot)
            joints[slot].alive = false;
        return created;
    }

    Client::Core::Expected<void, std::string> Step(const ActorState* hero = nullptr)
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, particles.get(), 0.0f, nullptr, nullptr, {}, {}, {},
                                          {}, nullptr, hero, nullptr, volley.get());
    }

    glm::vec3 start{1000.0f, 2000.0f, 400.0f};
    std::vector<JointState> joints = std::vector<JointState>(Last + 1);
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    CrtRandom random{Seed};
    // El asedio, los atributos y los blancos del héroe (0x53b569..0x53b5b1).
    std::unique_ptr<Client::Scenes::JointKernels::VolleyServices> volley;
};

constexpr std::size_t HeroSlot = 42;
// El lugar de la skill en el registro propio (+0x9ea del joint) y la skill que tiene ahí: la 0x37, que crea el 0x5be
// (0x4f9fae).
constexpr std::uint8_t SkillPlace = 3;
constexpr std::uint8_t Skill = 0x37;
// +0x9eb: 0x530860 no la escribe, el 0x61c/0 manda la que el lugar tenía.
constexpr std::uint8_t Volley = 0x0c;
// 0x517a20 con blancos: el relleno de 0x518515 y el cierre del serial (0x40b413) del C3, después del C1 10.
constexpr std::uint32_t TargetRandoms = 2u;

// El mundo del héroe para 0x517a20: Lorencia, el héroe en el lugar 42 y dos monstruos, uno a 30 del primer tramo y otro
// cerca del sexto (la vuelta 5).
struct HeroWorld
{
    HeroWorld()
    {
        world.Runtime().map = 0;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().SetSkillAt(SkillPlace, Skill);
        ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.seen = true;
        hero.byte88 = 1;
        hero.key = 0x0101;
        hero.position = {5000.0f, 5000.0f, 0.0f};
        Put(5, 0x1005, {1000.0f, 1850.0f, 0.0f});
        Put(6, 0x1006, {850.0f, 1900.0f, 0.0f});
    }

    void Put(std::size_t slot, std::uint16_t key, glm::vec3 position)
    {
        ActorState& monster = world.Actors()[slot];
        monster.alive = true;
        monster.seen = true;
        monster.byte88 = 2;
        monster.key = key;
        monster.position = position;
    }

    [[nodiscard]] std::function<void(Client::Protocol::Outgoing)> Capture()
    {
        return [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); };
    }

    std::unique_ptr<Client::Scenes::WorldState> ownedWorld = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::WorldState& world = *ownedWorld;
    std::vector<Client::Protocol::Outgoing> sent;
};

// Los servicios del juego (GameSceneLogic.cpp) sobre el mundo del héroe: el asedio y los atributos como están al
// llamarla, y 0x517a20 con el RNG del barrido.
void Attach(SweepWorld& sweep, HeroWorld& hero)
{
    sweep.volley =
        std::make_unique<Client::Scenes::JointKernels::VolleyServices>(Client::Scenes::JointKernels::VolleyServices{
            .siegeActive = hero.world.Runtime().siegeActive,
            .attributes = hero.world.Map().terrain.attributes.get(),
            .skillHits = [&sweep, &hero](const Client::Scenes::SkillHitsRequest& request)
                -> Client::Core::Expected<void, std::string>
            {
                const auto hit = Client::Scenes::HitTheTargetsAround(request, hero.world, sweep.random, hero.Capture());
                if (!hit.HasValue())
                    return Client::Core::Unexpected(hit.Error());
                return {};
            }});
}

// La partícula 0x5d9 con rand() & 1 en 0 y los anchos de los dos rayos (0x53b4a0..0x53b51d) de un tramo, en la réplica.
void ReplaySegment(CrtRandom& replay, Client::World::Particles& twin, glm::vec3 point)
{
    if ((replay.Next() & 1) == 0)
    {
        ASSERT_TRUE(
            twin.CreateChecked(0x5d9, point, glm::vec3(0.0f), glm::vec3(1.0f), 0, 1.0f, 0.0f, replay).HasValue());
    }
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
}

// El punto del tramo con la velocidad del barrido: el destino (la partida) más (0, −180, 0) girado.
glm::vec3 SegmentPoint(glm::vec3 velocity)
{
    const auto swing = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -180.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(velocity));
    return {F32(static_cast<double>(swing.x) + 1000.0), F32(static_cast<double>(swing.y) + 2000.0),
            F32(static_cast<double>(swing.z) + 400.0)};
}
} // namespace

// 0x5342e4..0x534399: sin rand(); la z de los ángulos + 30, la velocidad (+0x9dc) con esos ángulos, el destino en la
// partida, vida 20, 18 puntos, la cuenta en −1, sin la cola común, el ancho; la posición, (0, −180, 0) girado por los
// ángulos nuevos y sumado, y la luz en 1. Con la z en −30 los ángulos quedan en 0 y el giro es exacto.
TEST(SweepJointsTest, TheSweepBornsHundredEightyAway)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    const JointState& joint = tested.joints[Last];
    EXPECT_TRUE(joint.alive);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 150.0f);
    EXPECT_EQ(joint.angles, glm::vec3(0.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
    EXPECT_EQ(joint.targetPosition, tested.start);
    EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 1820.0f, 400.0f}));
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 18);
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.key, 7);
    EXPECT_EQ(joint.skill, 3);

    SweepWorld turned;
    ASSERT_TRUE(turned.Create({10.0f, 0.0f, 60.0f}).HasValue());
    const JointState& other = turned.joints[Last];
    EXPECT_EQ(other.angles, (glm::vec3{10.0f, 0.0f, 90.0f}));
    EXPECT_EQ(other.velocity, other.angles);
    const auto reach = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -180.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(other.angles));
    EXPECT_EQ(other.position,
              (glm::vec3{F32(static_cast<double>(reach.x) + 1000.0), F32(static_cast<double>(reach.y) + 2000.0),
                         F32(static_cast<double>(reach.z) + 400.0)}));
}

// 0x53b3f1..0x53b5c3: sin dueño, ocho tramos: la posición es el destino más (0, −180, 0) girado por la velocidad, un
// tramo, vz − 11; con rand() & 1 en 0 la partícula 0x5d9 y siempre dos 0x614/3 sin dueño desde la posición de antes
// hasta la nueva, con la key 5 (su vida) y el skill 0xa (sus puntos), de ancho rand() % 10 + 5 y (rand() & 7) + 4.
// Después la luz es la vida · 0,033333335f (0x53b611).
TEST(SweepJointsTest, TheSweepShedsTwoSparksForEachOfItsEightSegments)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    ASSERT_TRUE(tested.Step().HasValue());

    const JointState& joint = tested.joints[Last];
    EXPECT_EQ(joint.count, 7);
    EXPECT_EQ(joint.velocity.z, -88.0f);
    EXPECT_EQ(joint.life, 19);
    const float light = F32(20.0 * static_cast<double>(std::bit_cast<float>(0x3d088889u)));
    EXPECT_EQ(joint.light, glm::vec3(light));
    // El primer tramo, con la velocidad en 0: 180 hacia −y del destino.
    EXPECT_EQ(tested.joints[0].targetPosition, (glm::vec3{1000.0f, 1820.0f, 400.0f}));
    EXPECT_EQ(tested.joints[0].position, (glm::vec3{1000.0f, 1820.0f, 400.0f}));

    CrtRandom replay{Seed};
    auto twin = std::make_unique<Client::World::Particles>();
    int fires = 0;
    glm::vec3 velocity{};
    for (std::size_t turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const auto swing = Client::Render::Models::OriginalVectorRotate(
            {0.0f, -180.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(velocity));
        const glm::vec3 point{F32(static_cast<double>(swing.x) + 1000.0), F32(static_cast<double>(swing.y) + 2000.0),
                              F32(static_cast<double>(swing.z) + 400.0)};
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        if ((replay.Next() & 1) == 0)
        {
            ASSERT_TRUE(
                twin->CreateChecked(0x5d9, point, glm::vec3(0.0f), glm::vec3(1.0f), 0, 1.0f, 0.0f, replay).HasValue());
            ++fires;
        }
        const auto first = static_cast<float>(replay.Next() % 10 + 5);
        const auto second = static_cast<float>((replay.Next() & 7) + 4);
        const JointState& small = tested.joints[2 * turn];
        const JointState& large = tested.joints[2 * turn + 1];
        for (const JointState* spark : {&small, &large})
        {
            EXPECT_TRUE(spark->alive);
            EXPECT_EQ(spark->type, 0x614);
            EXPECT_EQ(spark->subtype, 3);
            EXPECT_EQ(spark->life, 5);
            EXPECT_EQ(spark->maximumCount, 10);
            EXPECT_EQ(spark->ownerContext, nullptr);
            EXPECT_EQ(spark->targetPosition, point);
        }
        EXPECT_EQ(small.width, first);
        EXPECT_EQ(large.width, second);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    std::ptrdiff_t alive = 0;
    for (const auto& particle : tested.particles->All())
        alive += particle.alive && particle.type == 0x5d9 ? 1 : 0;
    EXPECT_EQ(alive, fires);
}

// 0x53b3f8: con la cuenta en los puntos − 1 (17) no agrega tramos ni saca rand(); con la vida debajo de 10, la x de la
// luz por 0,6666667f en los tres canales (0x53b5fa).
TEST(SweepJointsTest, TheFullSweepOnlyDims)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    JointState& joint = tested.joints[Last];
    joint.count = 17;
    joint.life = 9;
    joint.light = {0.5f, 0.25f, 0.75f};
    const glm::vec3 before = joint.position;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    EXPECT_EQ(joint.position, before);
    EXPECT_EQ(joint.count, 17);
    const float dim = F32(0.5 * static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)));
    EXPECT_EQ(joint.light, glm::vec3(dim));
    EXPECT_EQ(joint.life, 8);
    EXPECT_FALSE(tested.joints[0].alive);
}

// 0x53b53e..0x53b567: con dueño igual al héroe ([0x743a170]), vida de más de 18 y la vuelta múltiplo de 5, el asedio o
// los blancos de la skill (0x53b569); sin ellos (JointKernels::VolleyServices), MuRet corta ahí. Con otro dueño sigue
// como sin dueño (los mismos rand(), rayos y partículas). Sin el héroe en el paso corta en 0x53b548, salvo con vida 18,
// donde no lo compara.
TEST(SweepJointsTest, OnlyTheHeroSweepAsksForTheSiegeAndTheTargets)
{
    ActorState owner{};
    ActorState other{};
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    const auto cut = tested.Step(&owner);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x53b569")) << cut.Error();
    EXPECT_EQ(tested.joints[Last].count, 0);

    SweepWorld foreign;
    ASSERT_TRUE(foreign.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    SweepWorld ownerless;
    ASSERT_TRUE(ownerless.Create({0.0f, 0.0f, -30.0f}).HasValue());
    ASSERT_TRUE(foreign.Step(&other).HasValue());
    ASSERT_TRUE(ownerless.Step(&other).HasValue());
    EXPECT_EQ(foreign.joints[Last].count, 7);
    EXPECT_EQ(foreign.joints[Last].life, 19);
    EXPECT_EQ(foreign.random.State(), ownerless.random.State());
    EXPECT_EQ(foreign.joints[Last].position, ownerless.joints[Last].position);
    for (std::size_t slot = 0; slot < Last; ++slot)
    {
        SCOPED_TRACE(slot);
        EXPECT_EQ(foreign.joints[slot].alive, ownerless.joints[slot].alive);
        EXPECT_EQ(foreign.joints[slot].width, ownerless.joints[slot].width);
    }
    std::size_t differences = 0;
    for (std::size_t slot = 0; slot < foreign.particles->All().size(); ++slot)
        differences += foreign.particles->All()[slot].alive == ownerless.particles->All()[slot].alive ? 0U : 1U;
    EXPECT_EQ(differences, 0U);

    SweepWorld blind;
    ASSERT_TRUE(blind.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    const auto unknown = blind.Step();
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_TRUE(unknown.Error().starts_with("0x53b548")) << unknown.Error();

    SweepWorld late;
    ASSERT_TRUE(late.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    late.joints[Last].life = 18;
    ASSERT_TRUE(late.Step().HasValue());
    EXPECT_EQ(late.joints[Last].count, 7);
}

// 0x53b569..0x53b5b6: el barrido del héroe, sin asedio, en las vueltas 0 y 5 (vida 20) llama a 0x517a20(+0x9ea,
// posición, 150, +0x9eb, +0x9e8, 0) con la posición del tramo recién agregado: el cierre del registro propio (3.848
// rand(), 0x40b4d0) y, con blancos, el C1 10 con los dos rand() del C3 (el relleno de 0x518515 y el cierre del serial,
// 0x40b413), entre los rand() de los tramos. En la vuelta 0 entra el monstruo a 30, con la celda (9, 18): 1000 · 0,01f
// (0x6b86c0) queda debajo de 10. En la 5 (vz −55) el tramo cae en (1000 − 180 · sen 55°, 2000 − 180 · cos 55°) ≈
// (852,56; 1896,76): el 0x1006 queda a ~4,1 y el 0x1005 a ~154,7 (fuera de 150), con la celda (8, 18). El paso sigue
// con todas las vueltas.
TEST(SweepJointsTest, TheHeroSweepHitsTheTargetsAroundEveryFiveTurns)
{
    HeroWorld hero;
    SweepWorld tested;
    Attach(tested, hero);
    ActorState& owner = hero.world.Actors()[HeroSlot];
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    tested.joints[Last].volley = Volley;

    const auto stepped = tested.Step(&owner);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.joints[Last].count, 7);
    EXPECT_EQ(tested.joints[Last].life, 19);

    // Cada llamada: el cierre del registro propio (0x517af3, sin mirar los blancos); con blancos, además, los dos
    // rand() del C3. Las dos llamadas de este barrido tienen blancos.
    constexpr std::uint32_t HitRandoms = Client::Render::Models::OwnRecordCloseRandoms + TargetRandoms;
    CrtRandom replay{Seed};
    auto particles = std::make_unique<Client::World::Particles>();
    glm::vec3 velocity{};
    for (int turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const glm::vec3 point = SegmentPoint(velocity);
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        ReplaySegment(replay, *particles, point);
        if (turn % 5 == 0)
            replay.Discard(HitRandoms);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    ASSERT_EQ(hero.sent.size(), 2u);
    Client::Protocol::Skill::SkillHitsSend first{.skill = Skill, .x = 9, .y = 18, .volley = Volley, .hits = 1};
    first.keys = {0x1005, 0, 0, 0, 0};
    EXPECT_TRUE(Client::Tests::SameBytes(hero.sent[0].packet.Bytes(),
                                         Client::Protocol::Skill::WriteSkillHits(first).packet.Bytes()));
    Client::Protocol::Skill::SkillHitsSend second{.skill = Skill, .x = 8, .y = 18, .volley = Volley, .hits = 1};
    second.keys = {0x1006, 0, 0, 0, 0};
    EXPECT_TRUE(Client::Tests::SameBytes(hero.sent[1].packet.Bytes(),
                                         Client::Protocol::Skill::WriteSkillHits(second).packet.Bytes()));
}

// 0x517a44..0x517af3: 0x517a20 abre y cierra el registro propio al entrar, haya blancos o no: el cierre (3.848 rand(),
// 0x40b4d0 en 0x517af3) corre en cada llamada, y sólo los dos rand() del C3 (0x518515 y 0x40b413) van con el C1 10.
// Con el 0x1006 lejos, la llamada de la vuelta 5 no encuentra a nadie a 150 (el 0x1005 queda a ~154,7): no manda nada
// y saca sólo el cierre.
TEST(SweepJointsTest, AHeroSweepCallWithoutTargetsStillClosesTheOwnRecord)
{
    HeroWorld hero;
    hero.Put(6, 0x1006, {9000.0f, 9000.0f, 0.0f});
    SweepWorld tested;
    Attach(tested, hero);
    ActorState& owner = hero.world.Actors()[HeroSlot];
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    tested.joints[Last].volley = Volley;

    const auto stepped = tested.Step(&owner);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.joints[Last].count, 7);
    CrtRandom replay{Seed};
    auto particles = std::make_unique<Client::World::Particles>();
    glm::vec3 velocity{};
    for (int turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const glm::vec3 point = SegmentPoint(velocity);
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        ReplaySegment(replay, *particles, point);
        if (turn == 0)
            replay.Discard(Client::Render::Models::OwnRecordCloseRandoms + TargetRandoms);
        else if (turn == 5)
            replay.Discard(Client::Render::Models::OwnRecordCloseRandoms);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    ASSERT_EQ(hero.sent.size(), 1u);
    Client::Protocol::Skill::SkillHitsSend first{.skill = Skill, .x = 9, .y = 18, .volley = Volley, .hits = 1};
    first.keys = {0x1005, 0, 0, 0, 0};
    EXPECT_TRUE(Client::Tests::SameBytes(hero.sent[0].packet.Bytes(),
                                         Client::Protocol::Skill::WriteSkillHits(first).packet.Bytes()));
}

// 0x53b56e..0x53b583 y 0x53b5cb..0x53b5e6: con el asedio prendido (0x4294b0) y el bit 0x100 en el atributo de la celda
// del tramo (0x5f82c0), la velocidad en 0, la vida en ftol(20 · 0,2f) = 4 y sale de las vueltas después del primer
// tramo, sin 0x517a20: con vida de menos de 10 se apaga la luz (0x53b5fa) y la vida baja a 3. Con la celda sin el bit,
// sigue como sin asedio.
TEST(SweepJointsTest, TheSiegeStopsTheHeroSweepOnTheMarkedCells)
{
    for (const std::uint32_t attribute : {0x100u, 0x4u})
    {
        SCOPED_TRACE(attribute);
        HeroWorld hero;
        hero.world.Runtime().siegeActive = true;
        hero.world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        hero.world.Map().terrain.attributes->cells.fill(static_cast<std::uint16_t>(attribute));
        SweepWorld tested;
        Attach(tested, hero);
        ActorState& owner = hero.world.Actors()[HeroSlot];
        ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
        JointState& joint = tested.joints[Last];
        joint.speed = 5.0f;

        ASSERT_TRUE(tested.Step(&owner).HasValue());

        if (attribute != 0x100u)
        {
            HeroWorld open;
            SweepWorld plain;
            Attach(plain, open);
            ActorState& same = open.world.Actors()[HeroSlot];
            ASSERT_TRUE(plain.Create({0.0f, 0.0f, -30.0f}, &same).HasValue());
            ASSERT_TRUE(plain.Step(&same).HasValue());
            EXPECT_EQ(joint.count, 7);
            EXPECT_EQ(joint.speed, 5.0f);
            EXPECT_EQ(tested.random.State(), plain.random.State());
            EXPECT_EQ(hero.sent.size(), open.sent.size());
            continue;
        }
        CrtRandom replay{Seed};
        auto particles = std::make_unique<Client::World::Particles>();
        ReplaySegment(replay, *particles, SegmentPoint(glm::vec3(0.0f)));
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_TRUE(hero.sent.empty());
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.life, 3);
        const float dim = F32(1.0 * static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)));
        EXPECT_EQ(joint.light, glm::vec3(dim));
    }
}
