#include "Events/ObjectCollapse.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Models/ObjectState.h"
#include "Events/EventGlobals.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Particles/Particles.h"
#include "Kernel/WorldObject.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

// The object collapse (06 § El derrumbe de los objetos): 0x5ff960 in Blood Castle (gate 0x24 and objects 9 and 10),
// 0x405630 in Chaos Castle and the walk of 0x606560 through the sectors.
namespace
{
using Client::Crt::CrtRandom;
using Client::Events::ObjectCollapse;
using Client::Events::ObjectCollapseContext;
using Client::Kernel::WorldObject;

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t FourthBloodCastle = 14;
constexpr std::int32_t FirstChaosCastle = 18;
constexpr std::int32_t LastChaosCastle = 23;
constexpr std::int16_t Gate = 0x24;
constexpr float WorldClock = 1234.0f;

std::size_t CellIndex(std::int32_t x, std::int32_t y)
{
    return static_cast<std::size_t>((y << 8) + x);
}

std::size_t AliveParticles(const Client::Particles::Particles& particles)
{
    std::size_t count = 0;
    for (const Client::Particles::Particle& particle : particles.All())
        count += particle.alive ? 1U : 0U;
    return count;
}

class ObjectCollapseTest : public testing::Test
{
protected:
    ObjectCollapseContext Context()
    {
        return ObjectCollapseContext{.runtime = *m_runtime,
                                     .random = m_random,
                                     .particles = m_particles.get(),
                                     .worldClock = WorldClock,
                                     .heroZ = 170.0f,
                                     .cameraShake = &m_shake,
                                     .attributes = m_attributes.get()};
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return *m_runtime;
    }

    ObjectCollapse& Request()
    {
        return m_runtime->events.objectCollapse;
    }

    std::uint16_t& Cell(std::int32_t x, std::int32_t y)
    {
        return m_attributes->cells[CellIndex(x, y)];
    }

    static WorldObject Object(std::int16_t type)
    {
        WorldObject object;
        object.type = type;
        object.transform.position = {1000.0f, 2000.0f, 300.0f};
        object.transform.angles = {0.0f, 0.0f, 45.0f};
        object.basePosition = {1000.0f, 2000.0f, 250.0f};
        object.light = {0.5f, 0.25f, 0.75f};
        return object;
    }

    Client::Core::Expected<void, std::string> Collapse(WorldObject& object)
    {
        return Client::Events::CollapseTheObject(object, Context());
    }

    std::unique_ptr<Client::Kernel::WorldRuntime> m_runtime = std::make_unique<Client::Kernel::WorldRuntime>();
    std::unique_ptr<Client::Particles::Particles> m_particles = std::make_unique<Client::Particles::Particles>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> m_attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    CrtRandom m_random{7};
    float m_shake = 0.0f;
};

// 0x5ff966..0x5ff997: the three integers at 0 or more and the requested map equal to the current one; the count drops
// by one per step, also from −1 (it wraps around like a 32-bit DEC).
TEST_F(ObjectCollapseTest, TheRequestOnlyActsOnItsMap)
{
    Runtime().map = FirstBloodCastle;
    EXPECT_FALSE(Client::Events::ObjectCollapseActive(Runtime()));
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    EXPECT_TRUE(Client::Events::ObjectCollapseActive(Runtime()));
    Request().type = -1;
    EXPECT_FALSE(Client::Events::ObjectCollapseActive(Runtime()));
    Request().type = Gate;
    Client::Events::CountDownTheObjectCollapse(Runtime());
    EXPECT_EQ(Request().counter, -1);
    EXPECT_FALSE(Client::Events::ObjectCollapseActive(Runtime()));
    Request().counter = std::numeric_limits<std::int32_t>::min();
    Client::Events::CountDownTheObjectCollapse(Runtime());
    EXPECT_EQ(Request().counter, std::numeric_limits<std::int32_t>::max());

    // Requested for 11 with the hero in Lorencia or Devias: neither the gate nor 9 and 10 move.
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
        for (const std::int16_t type : {std::int16_t{9}, std::int16_t{10}, Gate})
        {
            WorldObject object = Object(type);
            ASSERT_TRUE(Collapse(object).HasValue());
            EXPECT_EQ(object.transform.angles.x, 0.0f);
            EXPECT_EQ(object.field8a, 0);
            EXPECT_EQ(object.state.hiddenMesh, Client::Models::NoHiddenMesh);
        }
        EXPECT_EQ(Request().counter, 0);
        EXPECT_EQ(m_random.State(), CrtRandom{7}.State());
    }
}

// 0x5ffa2c..0x5ffa7b: with the count at 20, +0x1c at 35, +0x58 at −1 and sound 0x89; then the turn adds the delay and
// the delay rises by 1.5.
TEST_F(ObjectCollapseTest, TheGateStartsFallingAtTwenty)
{
    Runtime().map = FirstBloodCastle;
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0x14, .delay = 1.0f};
    WorldObject gate = Object(Gate);
    gate.state.hiddenMesh = 3;
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 36.0f);
    EXPECT_EQ(gate.state.hiddenMesh, Client::Models::NoHiddenMesh);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x89});
    EXPECT_EQ(Request().delay, 2.5f);

    // Each object of the type adds the request's delay, which is a single one: the second falls faster (Q-C1338).
    WorldObject second = Object(Gate);
    second.transform.angles.x = 10.0f;
    Runtime().lastSoundId = Client::Audio::SoundId{0};
    Client::Events::CountDownTheObjectCollapse(Runtime());
    ASSERT_TRUE(Collapse(second).HasValue());
    EXPECT_EQ(second.transform.angles.x, 12.5f);
    EXPECT_EQ(Request().delay, 4.0f);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_EQ(m_random.State(), CrtRandom{7}.State());
}

// 0x5ffa94..0x5ffb2a: with the turn at 90 or more it subtracts the count and the delay stays at 2; if it gives exactly
// 80, ten 0x5f3/0 dusts with two rand() each, before their creation.
TEST_F(ObjectCollapseTest, TheGateRaisesDustOnlyAtEightyExactly)
{
    Runtime().map = FourthBloodCastle;
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 10, .delay = 5.0f};
    WorldObject gate = Object(Gate);
    gate.transform.angles.x = 85.0f;
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 80.0f);
    EXPECT_EQ(Request().delay, 2.0f);
    EXPECT_EQ(AliveParticles(*m_particles), 10U);

    CrtRandom mirror{7};
    auto expected = std::make_unique<Client::Particles::Particles>();
    for (int count = 0; count < 10; ++count)
    {
        const int spreadX = mirror.Next() % 300;
        const int spreadY = mirror.Next() % 20;
        const glm::vec3 dust{static_cast<float>((static_cast<double>(spreadX) - 150.0) + 1000.0),
                             static_cast<float>(2000.0 - (static_cast<double>(spreadY) + 600.0)), 300.0f};
        ASSERT_TRUE(expected
                        ->CreateChecked(Client::Particles::DustParticle, dust, gate.transform.angles, gate.light, 0,
                                        1.0f, WorldClock, mirror, 0)
                        .HasValue());
    }
    EXPECT_EQ(m_random.State(), mirror.State());
    for (std::size_t index = 0; index < 10; ++index)
    {
        SCOPED_TRACE(index);
        const Client::Particles::Particle& got = m_particles->All()[index];
        const Client::Particles::Particle& want = expected->All()[index];
        EXPECT_TRUE(got.alive);
        EXPECT_EQ(got.type, Client::Particles::DustParticle);
        EXPECT_EQ(got.subtype, 0);
        EXPECT_EQ(got.scale, want.scale);
        EXPECT_EQ(got.position, want.position);
        EXPECT_EQ(got.angles, want.angles);
        EXPECT_EQ(got.light, want.light);
        EXPECT_EQ(got.life, want.life);
        EXPECT_EQ(got.velocity, want.velocity);
    }

    // With the count at 9 it gives 81: no dust and no rand().
    m_particles = std::make_unique<Client::Particles::Particles>();
    const std::uint32_t before = m_random.State();
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 9, .delay = 5.0f};
    gate.transform.angles.x = 85.0f;
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 81.0f);
    EXPECT_EQ(AliveParticles(*m_particles), 0U);
    EXPECT_EQ(m_random.State(), before);

    // Below 90 it neither subtracts nor touches the delay; a NaN does not enter either (FCOMP with C0).
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 9, .delay = 5.0f};
    gate.transform.angles.x = 84.0f;
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 89.0f);
    EXPECT_EQ(Request().delay, 6.5f);
    gate.transform.angles.x = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_TRUE(std::isnan(gate.transform.angles.x));
    EXPECT_EQ(Request().delay, 8.0f);
}

// Without the pool, the dust stops at 0x5ffb21 with the turn already done.
TEST_F(ObjectCollapseTest, TheDustNeedsTheParticlePool)
{
    Runtime().map = FirstBloodCastle;
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 10, .delay = 5.0f};
    WorldObject gate = Object(Gate);
    gate.transform.angles.x = 85.0f;
    ObjectCollapseContext context = Context();
    context.particles = nullptr;
    const auto cut = Client::Events::CollapseTheObject(gate, context);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5ffb21"), std::string::npos) << cut.Error();
    EXPECT_EQ(gate.transform.angles.x, 80.0f);
}

// 0x5ffb36..0x5ffb54: with the count at 0 the object hides at 90, the request is cleared and attribute 8 leaves
// (13..15, 70..75), with the other bits as they were.
TEST_F(ObjectCollapseTest, TheGateEndsAndOpensThePassage)
{
    Runtime().map = FourthBloodCastle;
    for (std::int32_t y = 69; y <= 76; ++y)
        for (std::int32_t x = 12; x <= 16; ++x)
            Cell(x, y) = 0x000c;
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    WorldObject gate = Object(Gate);
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 90.0f);
    EXPECT_EQ(gate.state.hiddenMesh, Client::Kernel::HiddenObject);
    EXPECT_EQ(Request().map, -1);
    EXPECT_EQ(Request().type, -1);
    EXPECT_EQ(Request().counter, -1);
    EXPECT_EQ(Request().delay, -1.0f);
    for (std::int32_t y = 69; y <= 76; ++y)
    {
        for (std::int32_t x = 12; x <= 16; ++x)
        {
            SCOPED_TRACE(x * 1000 + y);
            const bool passage = x >= 13 && x <= 15 && y >= 70 && y <= 75;
            EXPECT_EQ(Cell(x, y), passage ? 0x0004 : 0x000c);
        }
    }
    WorldObject next = Object(Gate);
    ASSERT_TRUE(Collapse(next).HasValue());
    EXPECT_EQ(next.transform.angles.x, 0.0f);

    // Without the .att it stops at 0x5ffb54, with the object fallen and the request already cleared.
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    ObjectCollapseContext context = Context();
    context.attributes = nullptr;
    WorldObject last = Object(Gate);
    const auto cut = Client::Events::CollapseTheObject(last, context);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5ffb54"), std::string::npos) << cut.Error();
    EXPECT_EQ(last.state.hiddenMesh, Client::Kernel::HiddenObject);
    EXPECT_EQ(Request().map, -1);
}

// 0x5ff9c2..0x5ffa1e: 9 and 10 open with the count at 0, whether the requested type is another or the same; with
// another count, nothing. Another type that is not the requested one, nothing.
TEST_F(ObjectCollapseTest, TheNineAndTenOpenAtZero)
{
    Runtime().map = FirstBloodCastle;
    for (const std::int32_t requested : {std::int32_t{Gate}, std::int32_t{9}, std::int32_t{10}})
    {
        SCOPED_TRACE(requested);
        for (const std::int16_t type : {std::int16_t{9}, std::int16_t{10}})
        {
            Request() = {.map = FirstBloodCastle, .type = requested, .counter = 5, .delay = 1.0f};
            WorldObject object = Object(type);
            object.state.hiddenMesh = 2;
            ASSERT_TRUE(Collapse(object).HasValue());
            EXPECT_EQ(object.state.hiddenMesh, 2);
            EXPECT_EQ(object.field8a, 0);
            EXPECT_EQ(object.transform.angles.x, 0.0f);

            Request().counter = 0;
            ASSERT_TRUE(Collapse(object).HasValue());
            EXPECT_EQ(object.state.hiddenMesh, Client::Models::NoHiddenMesh);
            EXPECT_EQ(object.field8a, 4);
            EXPECT_EQ(object.transform.angles.x, 0.0f);
            EXPECT_EQ(Request().counter, 0);
        }
    }
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    WorldObject other = Object(0x25);
    ASSERT_TRUE(Collapse(other).HasValue());
    EXPECT_EQ(other.transform.angles.x, 0.0f);
    EXPECT_EQ(Request().counter, 0);
}

// 0x405630, the fall: the area of 0x6c634c has to be that of the type's case; z = +0x184 − (30 − count) · delay and the
// delay rises 0.4f. Another area, a type of case 3 or outside the table: nothing, and neither 9, 10 nor the gate.
TEST_F(ObjectCollapseTest, TheChaosCastleStepsSinkWithTheirArea)
{
    struct Case
    {
        std::int16_t type;
        std::uint8_t area;
    };
    for (const std::int32_t map : {FirstChaosCastle, LastChaosCastle})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        for (const Case step : {Case{0, 1}, Case{17, 1}, Case{24, 7}, Case{29, 7}, Case{30, 4}, Case{35, 4}})
        {
            SCOPED_TRACE(step.type);
            Runtime().chaosCastleArea = step.area;
            Request() = {.map = map, .type = 0x10, .counter = 10, .delay = 2.0f};
            WorldObject object = Object(step.type);
            ASSERT_TRUE(Collapse(object).HasValue());
            EXPECT_EQ(object.transform.position.z, 250.0f - (20.0f * 2.0f));
            EXPECT_EQ(Request().delay, static_cast<float>(2.0 + static_cast<double>(0.4f)));
            EXPECT_EQ(Runtime().chaosCastleArea, step.area);

            Runtime().chaosCastleArea = static_cast<std::uint8_t>(step.area + 1);
            WorldObject still = Object(step.type);
            ASSERT_TRUE(Collapse(still).HasValue());
            EXPECT_EQ(still.transform.position.z, 300.0f);
        }
        Runtime().chaosCastleArea = 1;
        Request() = {.map = map, .type = Gate, .counter = 0, .delay = 2.0f};
        for (const std::int16_t type :
             {std::int16_t{6}, std::int16_t{9}, std::int16_t{10}, std::int16_t{18}, Gate, std::int16_t{-1}})
        {
            SCOPED_TRACE(type);
            WorldObject object = Object(type);
            ASSERT_TRUE(Collapse(object).HasValue());
            EXPECT_EQ(object.transform.position.z, 300.0f);
            EXPECT_EQ(object.transform.angles.x, 0.0f);
            EXPECT_EQ(object.field8a, 0);
            EXPECT_EQ(object.state.hiddenMesh, Client::Models::NoHiddenMesh);
        }
        EXPECT_EQ(Request().counter, 0);
        EXPECT_EQ(m_random.State(), CrtRandom{7}.State());
    }
}

// 0x405715..0x40572c: with the count at 0 or less, hidden, the next area (1 → 2, 7 → 8, 4 → 5) and the request
// cleared.
TEST_F(ObjectCollapseTest, TheChaosCastleStepEndsInTheNextArea)
{
    struct Case
    {
        std::int16_t type;
        std::uint8_t area;
        std::uint8_t next;
    };
    Runtime().map = 20;
    for (const Case step : {Case{13, 1, 2}, Case{26, 7, 8}, Case{33, 4, 5}})
    {
        SCOPED_TRACE(step.type);
        Runtime().chaosCastleArea = step.area;
        Request() = {.map = 20, .type = 0x10, .counter = 0, .delay = 2.0f};
        WorldObject object = Object(step.type);
        ASSERT_TRUE(Collapse(object).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
        EXPECT_EQ(object.transform.position.z, 300.0f);
        EXPECT_EQ(Runtime().chaosCastleArea, step.next);
        EXPECT_EQ(Request().map, -1);
        EXPECT_EQ(Request().counter, -1);
    }
}

// 0x40567d..0x40570b (and its copies): with the count at 30 or more, one rand() and the stone 0x5f6/0 (0x54271f, like
// 0x5f3/0: four rand()) with the object's angles, light 1, scale 1.5 and no owner, and the shake
// (rand() % 3 − 3) · 0.1f. Replicated in a twin pool with the same CrtRandom; with the 3000 full, the stone draws
// nothing and the shake follows.
TEST_F(ObjectCollapseTest, TheChaosCastleStoneFallsWithItsShake)
{
    struct Case
    {
        std::int16_t type;
        std::uint8_t area;
    };
    Runtime().map = FirstChaosCastle;
    for (const bool full : {false, true})
        for (const Case step : {Case{2, 1}, Case{25, 7}, Case{31, 4}})
        {
            SCOPED_TRACE(testing::Message() << step.type << ' ' << full);
            m_particles = std::make_unique<Client::Particles::Particles>();
            auto twin = std::make_unique<Client::Particles::Particles>();
            if (full)
            {
                for (auto& particle : m_particles->All())
                    particle.alive = true;
                for (auto& particle : twin->All())
                    particle.alive = true;
            }
            Runtime().chaosCastleArea = step.area;
            Request() = {.map = FirstChaosCastle, .type = 0x10, .counter = 40, .delay = 2.0f};
            CrtRandom mirror{m_random.State()};
            WorldObject object = Object(step.type);
            const auto fell = Collapse(object);
            ASSERT_TRUE(fell.HasValue()) << fell.Error();
            const int spread = mirror.Next() % 300;
            const auto stone = twin->CreateChecked(
                0x5f6, glm::vec3(static_cast<float>(static_cast<double>(spread) - 150.0 + 1000.0), 2000.0f, 170.0f),
                object.transform.angles, glm::vec3(1.0f), 0, 1.5f, WorldClock, mirror, 0);
            ASSERT_TRUE(stone.HasValue());
            const auto shake = static_cast<float>(static_cast<double>(mirror.Next() % 3 - 3) *
                                                  static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
            EXPECT_EQ(m_random.State(), mirror.State());
            EXPECT_EQ(m_shake, shake);
            if (!full)
            {
                const auto& made = m_particles->All()[0];
                const auto& reference = twin->All()[0];
                ASSERT_TRUE(made.alive);
                EXPECT_EQ(made.type, 0x5f6);
                EXPECT_EQ(made.subtype, 0);
                EXPECT_EQ(made.life, 32);
                EXPECT_EQ(made.position, reference.position);
                EXPECT_EQ(made.scale, reference.scale);
                EXPECT_EQ(made.angles, reference.angles);
                EXPECT_EQ(made.velocity, reference.velocity);
                EXPECT_EQ(AliveParticles(*m_particles), 1u);
            }
            EXPECT_EQ(object.transform.position.z, 300.0f);
            EXPECT_EQ(Request().delay, 2.0f);
            EXPECT_EQ(Runtime().chaosCastleArea, step.area);
        }
}

// 0x606560: in a sector that passed, the step and the collapse with each live seen one; in one that did not pass, the
// collapse with each live one and +0x16c at 0 for all.
TEST_F(ObjectCollapseTest, TheWalkCollapsesTheObjectsOfSectorsThatDidNotPass)
{
    std::vector<WorldObject> objects(5);
    for (std::size_t index = 0; index < objects.size(); ++index)
        objects[index].type = static_cast<std::int16_t>(index);
    // 0 and 1: sector that passed, one seen and the other not. 2: sector that passed, seen but dead. 3 and 4: sector
    // that did not pass, alive and dead, with an old +0x16c on the dead one.
    objects[0].sectorPassed = true;
    objects[0].seen = true;
    objects[1].sectorPassed = true;
    objects[2].sectorPassed = true;
    objects[2].seen = true;
    objects[2].alive = false;
    objects[4].alive = false;
    std::vector<std::int16_t> stepped;
    std::vector<std::int16_t> collapsed;
    const Client::WorldObjects::AfterObjectStep seenStep =
        [&stepped](WorldObject& object) -> Client::Core::Expected<void, std::string>
    {
        stepped.push_back(object.type);
        return {};
    };
    const Client::WorldObjects::AfterObjectStep collapse =
        [&collapsed](WorldObject& object) -> Client::Core::Expected<void, std::string>
    {
        collapsed.push_back(object.type);
        return {};
    };
    ASSERT_TRUE(Client::WorldObjects::WalkTheObjectSectors(objects, seenStep, collapse).HasValue());
    EXPECT_EQ(stepped, (std::vector<std::int16_t>{0}));
    EXPECT_EQ(collapsed, (std::vector<std::int16_t>{0, 3}));
    EXPECT_TRUE(objects[0].seen);
    EXPECT_TRUE(objects[2].seen);
    EXPECT_FALSE(objects[3].seen);
    EXPECT_FALSE(objects[4].seen);

    // Without the step (no seen one) the collapse goes on; the first cut stops the walk.
    collapsed.clear();
    const Client::WorldObjects::AfterObjectStep cutting =
        [&collapsed](WorldObject& object) -> Client::Core::Expected<void, std::string>
    {
        collapsed.push_back(object.type);
        return Client::Core::Unexpected(std::string("corte"));
    };
    for (WorldObject& object : objects)
        object.sectorPassed = false;
    const auto cut = Client::WorldObjects::WalkTheObjectSectors(objects, {}, cutting);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_EQ(collapsed, (std::vector<std::int16_t>{0}));
}

// The walk with the real collapse in 11: the gate falls with its sector outside the drawing and the 9 of a seen sector
// opens; the request is cleared halfway through the walk and the 10 that comes after no longer opens (Q-C1339).
TEST_F(ObjectCollapseTest, TheWalkResetsTheRequestHalfway)
{
    Runtime().map = FirstBloodCastle;
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    // The 9 in sector (0, 1), which passed; the gate and the 10 in (0, 2), which did not.
    std::vector<WorldObject> objects{Object(Gate), Object(10), Object(9)};
    objects[0].sectorY = 2;
    objects[1].sectorY = 2;
    objects[2].sectorY = 1;
    objects[2].sectorPassed = true;
    objects[2].seen = true;
    const ObjectCollapseContext context = Context();
    const Client::WorldObjects::AfterObjectStep collapse = [&context](WorldObject& object)
    { return Client::Events::CollapseTheObject(object, context); };
    ASSERT_TRUE(Client::WorldObjects::WalkTheObjectSectors(objects, {}, collapse).HasValue());
    EXPECT_EQ(objects[2].field8a, 4);
    EXPECT_EQ(objects[0].state.hiddenMesh, Client::Kernel::HiddenObject);
    EXPECT_EQ(objects[0].transform.angles.x, 90.0f);
    EXPECT_EQ(objects[1].field8a, 0);
    EXPECT_EQ(objects[1].state.hiddenMesh, Client::Models::NoHiddenMesh);
    EXPECT_TRUE(objects[2].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(Request().map, -1);
}
} // namespace
