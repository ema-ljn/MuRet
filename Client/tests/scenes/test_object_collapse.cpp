#include "Scenes/ObjectCollapse.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

// El derrumbe de los objetos (06 § El derrumbe de los objetos): 0x5ff960 en Blood Castle (la puerta 0x24 y los
// objetos 9 y 10), 0x405630 en Chaos Castle y el recorrido de 0x606560 por los sectores.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ObjectCollapse;
using Client::Scenes::ObjectCollapseContext;
using Client::World::WorldObject;

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

std::size_t AliveParticles(const Client::World::Particles& particles)
{
    std::size_t count = 0;
    for (const Client::World::Particle& particle : particles.All())
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

    Client::Scenes::WorldRuntime& Runtime()
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
        return Client::Scenes::CollapseTheObject(object, Context());
    }

    std::unique_ptr<Client::Scenes::WorldRuntime> m_runtime = std::make_unique<Client::Scenes::WorldRuntime>();
    std::unique_ptr<Client::World::Particles> m_particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> m_attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    CrtRandom m_random{7};
    float m_shake = 0.0f;
};

// 0x5ff966..0x5ff997: los tres enteros en 0 o más y el mapa pedido igual al de ahora; la cuenta baja uno por paso,
// también desde −1 (da la vuelta como un DEC de 32 bits).
TEST_F(ObjectCollapseTest, TheRequestOnlyActsOnItsMap)
{
    Runtime().map = FirstBloodCastle;
    EXPECT_FALSE(Client::Scenes::ObjectCollapseActive(Runtime()));
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    EXPECT_TRUE(Client::Scenes::ObjectCollapseActive(Runtime()));
    Request().type = -1;
    EXPECT_FALSE(Client::Scenes::ObjectCollapseActive(Runtime()));
    Request().type = Gate;
    Client::Scenes::CountDownTheObjectCollapse(Runtime());
    EXPECT_EQ(Request().counter, -1);
    EXPECT_FALSE(Client::Scenes::ObjectCollapseActive(Runtime()));
    Request().counter = std::numeric_limits<std::int32_t>::min();
    Client::Scenes::CountDownTheObjectCollapse(Runtime());
    EXPECT_EQ(Request().counter, std::numeric_limits<std::int32_t>::max());

    // Pedido para el 11 con el héroe en Lorencia o en Devias: ni la puerta ni los 9 y 10 se mueven.
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
            EXPECT_EQ(object.state.hiddenMesh, Client::Render::Models::NoHiddenMesh);
        }
        EXPECT_EQ(Request().counter, 0);
        EXPECT_EQ(m_random.State(), CrtRandom{7}.State());
    }
}

// 0x5ffa2c..0x5ffa7b: con la cuenta en 20, +0x1c en 35, +0x58 en −1 y el sonido 0x89; después el giro suma la
// demora y la demora sube 1,5.
TEST_F(ObjectCollapseTest, TheGateStartsFallingAtTwenty)
{
    Runtime().map = FirstBloodCastle;
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0x14, .delay = 1.0f};
    WorldObject gate = Object(Gate);
    gate.state.hiddenMesh = 3;
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 36.0f);
    EXPECT_EQ(gate.state.hiddenMesh, Client::Render::Models::NoHiddenMesh);
    EXPECT_EQ(Runtime().lastSoundId, 0x89);
    EXPECT_EQ(Request().delay, 2.5f);

    // Cada objeto del tipo suma la demora del pedido, que es una sola: el segundo cae más rápido (Q-C1338).
    WorldObject second = Object(Gate);
    second.transform.angles.x = 10.0f;
    Runtime().lastSoundId = 0;
    Client::Scenes::CountDownTheObjectCollapse(Runtime());
    ASSERT_TRUE(Collapse(second).HasValue());
    EXPECT_EQ(second.transform.angles.x, 12.5f);
    EXPECT_EQ(Request().delay, 4.0f);
    EXPECT_EQ(Runtime().lastSoundId, 0);
    EXPECT_EQ(m_random.State(), CrtRandom{7}.State());
}

// 0x5ffa94..0x5ffb2a: con el giro en 90 o más le resta la cuenta y la demora queda en 2; si da 80 justo, diez polvos
// 5f3/0 con dos rand() cada uno, antes de su alta.
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
    auto expected = std::make_unique<Client::World::Particles>();
    for (int count = 0; count < 10; ++count)
    {
        const int spreadX = mirror.Next() % 300;
        const int spreadY = mirror.Next() % 20;
        const glm::vec3 dust{static_cast<float>((static_cast<double>(spreadX) - 150.0) + 1000.0),
                             static_cast<float>(2000.0 - (static_cast<double>(spreadY) + 600.0)), 300.0f};
        ASSERT_TRUE(expected
                        ->CreateChecked(Client::World::DustParticle, dust, gate.transform.angles, gate.light, 0, 1.0f,
                                        WorldClock, mirror, 0)
                        .HasValue());
    }
    EXPECT_EQ(m_random.State(), mirror.State());
    for (std::size_t index = 0; index < 10; ++index)
    {
        SCOPED_TRACE(index);
        const Client::World::Particle& got = m_particles->All()[index];
        const Client::World::Particle& want = expected->All()[index];
        EXPECT_TRUE(got.alive);
        EXPECT_EQ(got.type, Client::World::DustParticle);
        EXPECT_EQ(got.subtype, 0);
        EXPECT_EQ(got.scale, want.scale);
        EXPECT_EQ(got.position, want.position);
        EXPECT_EQ(got.angles, want.angles);
        EXPECT_EQ(got.light, want.light);
        EXPECT_EQ(got.life, want.life);
        EXPECT_EQ(got.velocity, want.velocity);
    }

    // Con la cuenta en 9 da 81: sin polvo ni rand().
    m_particles = std::make_unique<Client::World::Particles>();
    const std::uint32_t before = m_random.State();
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 9, .delay = 5.0f};
    gate.transform.angles.x = 85.0f;
    ASSERT_TRUE(Collapse(gate).HasValue());
    EXPECT_EQ(gate.transform.angles.x, 81.0f);
    EXPECT_EQ(AliveParticles(*m_particles), 0U);
    EXPECT_EQ(m_random.State(), before);

    // Debajo de 90 no resta ni toca la demora; un NaN tampoco entra (FCOMP con C0).
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

// Sin el pool, el polvo corta en 0x5ffb21 con el giro ya hecho.
TEST_F(ObjectCollapseTest, TheDustNeedsTheParticlePool)
{
    Runtime().map = FirstBloodCastle;
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 10, .delay = 5.0f};
    WorldObject gate = Object(Gate);
    gate.transform.angles.x = 85.0f;
    ObjectCollapseContext context = Context();
    context.particles = nullptr;
    const auto cut = Client::Scenes::CollapseTheObject(gate, context);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5ffb21"), std::string::npos) << cut.Error();
    EXPECT_EQ(gate.transform.angles.x, 80.0f);
}

// 0x5ffb36..0x5ffb54: con la cuenta en 0 el objeto se oculta en 90, el pedido se borra y sale el atributo 8 de
// (13..15, 70..75), con los demás bits como estaban.
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
    EXPECT_EQ(gate.state.hiddenMesh, Client::World::HiddenObject);
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
    // Después del pedido borrado, el resto del recorrido ya no hace nada.
    WorldObject next = Object(Gate);
    ASSERT_TRUE(Collapse(next).HasValue());
    EXPECT_EQ(next.transform.angles.x, 0.0f);

    // Sin el .att corta en 0x5ffb54, con el objeto caído y el pedido ya borrado.
    Request() = {.map = FourthBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    ObjectCollapseContext context = Context();
    context.attributes = nullptr;
    WorldObject last = Object(Gate);
    const auto cut = Client::Scenes::CollapseTheObject(last, context);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5ffb54"), std::string::npos) << cut.Error();
    EXPECT_EQ(last.state.hiddenMesh, Client::World::HiddenObject);
    EXPECT_EQ(Request().map, -1);
}

// 0x5ff9c2..0x5ffa1e: el 9 y el 10 se abren con la cuenta en 0, aunque el tipo pedido sea otro o el mismo; con otra
// cuenta, nada. Otro tipo que no es el pedido, nada.
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
            EXPECT_EQ(object.state.hiddenMesh, Client::Render::Models::NoHiddenMesh);
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

// 0x405630, la caída: el área de 0x6c634c tiene que ser la del caso del tipo; z = +0x184 − (30 − cuenta) · demora y la
// demora sube 0,4f. Otro área, un tipo del caso 3 o fuera de la tabla: nada, y tampoco el 9, el 10 ni la puerta.
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

            // Con el área de otro caso, nada.
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
            EXPECT_EQ(object.state.hiddenMesh, Client::Render::Models::NoHiddenMesh);
        }
        EXPECT_EQ(Request().counter, 0);
        EXPECT_EQ(m_random.State(), CrtRandom{7}.State());
    }
}

// 0x405715..0x40572c: con la cuenta en 0 o menos, oculto, el área siguiente (1 → 2, 7 → 8, 4 → 5) y el pedido borrado.
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
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        EXPECT_EQ(object.transform.position.z, 300.0f);
        EXPECT_EQ(Runtime().chaosCastleArea, step.next);
        EXPECT_EQ(Request().map, -1);
        EXPECT_EQ(Request().counter, -1);
    }
}

// 0x40567d..0x4056e5 (y sus copias): con la cuenta en 30 o más, un rand() y la piedra 5f6/0, que el pool de MuRet no
// tiene (CL11): corta donde la crea cada caso, antes del temblor, con el rand() del alta que el pool saca antes de
// fallar.
TEST_F(ObjectCollapseTest, TheChaosCastleStoneIsCutInThePool)
{
    struct Case
    {
        std::int16_t type;
        std::uint8_t area;
        const char* where;
    };
    Runtime().map = FirstChaosCastle;
    for (const Case step : {Case{2, 1, "0x405899"}, Case{25, 7, "0x4056e5"}, Case{31, 4, "0x4057bf"}})
    {
        SCOPED_TRACE(step.type);
        Runtime().chaosCastleArea = step.area;
        Request() = {.map = FirstChaosCastle, .type = 0x10, .counter = 40, .delay = 2.0f};
        CrtRandom mirror{m_random.State()};
        WorldObject object = Object(step.type);
        const auto cut = Collapse(object);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(step.where), std::string::npos) << cut.Error();
        const int spread = mirror.Next() % 300;
        auto pool = std::make_unique<Client::World::Particles>();
        EXPECT_FALSE(pool->CreateChecked(0x5f6,
                                         glm::vec3(static_cast<float>(static_cast<double>(spread) - 150.0 + 1000.0),
                                                   2000.0f, 170.0f),
                                         object.transform.angles, glm::vec3(1.0f), 0, 1.5f, WorldClock, mirror, 0)
                         .HasValue());
        EXPECT_EQ(m_random.State(), mirror.State());
        EXPECT_EQ(m_shake, 0.0f);
        EXPECT_EQ(object.transform.position.z, 300.0f);
        EXPECT_EQ(Request().delay, 2.0f);
        EXPECT_EQ(Runtime().chaosCastleArea, step.area);
    }
}

// 0x606560: en un sector que pasó, el paso y el derrumbe con cada vivo visto; en uno que no pasó, el derrumbe con cada
// vivo y +0x16c en 0 para todos.
TEST_F(ObjectCollapseTest, TheWalkCollapsesTheObjectsOfSectorsThatDidNotPass)
{
    std::vector<WorldObject> objects(5);
    for (std::size_t index = 0; index < objects.size(); ++index)
        objects[index].type = static_cast<std::int16_t>(index);
    // 0 y 1: sector que pasó, uno visto y otro no. 2: sector que pasó, visto pero muerto. 3 y 4: sector que no pasó,
    // vivo y muerto, con +0x16c viejo en el muerto.
    objects[0].sectorPassed = true;
    objects[0].seen = true;
    objects[1].sectorPassed = true;
    objects[2].sectorPassed = true;
    objects[2].seen = true;
    objects[2].alive = false;
    objects[4].alive = false;
    std::vector<std::int16_t> stepped;
    std::vector<std::int16_t> collapsed;
    const Client::Scenes::AfterObjectStep seenStep =
        [&stepped](WorldObject& object) -> Client::Core::Expected<void, std::string>
    {
        stepped.push_back(object.type);
        return {};
    };
    const Client::Scenes::AfterObjectStep collapse =
        [&collapsed](WorldObject& object) -> Client::Core::Expected<void, std::string>
    {
        collapsed.push_back(object.type);
        return {};
    };
    ASSERT_TRUE(Client::Scenes::WalkTheObjectSectors(objects, seenStep, collapse).HasValue());
    EXPECT_EQ(stepped, (std::vector<std::int16_t>{0}));
    EXPECT_EQ(collapsed, (std::vector<std::int16_t>{0, 3}));
    EXPECT_TRUE(objects[0].seen);
    EXPECT_TRUE(objects[2].seen);
    EXPECT_FALSE(objects[3].seen);
    EXPECT_FALSE(objects[4].seen);

    // Sin el paso (ningún visto) el derrumbe sigue; el primer corte para el recorrido.
    collapsed.clear();
    const Client::Scenes::AfterObjectStep cutting =
        [&collapsed](WorldObject& object) -> Client::Core::Expected<void, std::string>
    {
        collapsed.push_back(object.type);
        return Client::Core::Unexpected(std::string("corte"));
    };
    for (WorldObject& object : objects)
        object.sectorPassed = false;
    const auto cut = Client::Scenes::WalkTheObjectSectors(objects, {}, cutting);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_EQ(collapsed, (std::vector<std::int16_t>{0}));
}

// El recorrido con el derrumbe de verdad en el 11: la puerta cae con su sector afuera del dibujo y el 9 de un sector
// visto se abre; el pedido se borra a mitad del recorrido y el 10 que viene después ya no se abre (Q-C1339).
TEST_F(ObjectCollapseTest, TheWalkResetsTheRequestHalfway)
{
    Runtime().map = FirstBloodCastle;
    Request() = {.map = FirstBloodCastle, .type = Gate, .counter = 0, .delay = 1.0f};
    // El 9 en el sector (0, 1), que pasó; la puerta y el 10 en el (0, 2), que no.
    std::vector<WorldObject> objects{Object(Gate), Object(10), Object(9)};
    objects[0].sectorY = 2;
    objects[1].sectorY = 2;
    objects[2].sectorY = 1;
    objects[2].sectorPassed = true;
    objects[2].seen = true;
    const ObjectCollapseContext context = Context();
    const Client::Scenes::AfterObjectStep collapse = [&context](WorldObject& object)
    { return Client::Scenes::CollapseTheObject(object, context); };
    ASSERT_TRUE(Client::Scenes::WalkTheObjectSectors(objects, {}, collapse).HasValue());
    EXPECT_EQ(objects[2].field8a, 4);
    EXPECT_EQ(objects[0].state.hiddenMesh, Client::World::HiddenObject);
    EXPECT_EQ(objects[0].transform.angles.x, 90.0f);
    EXPECT_EQ(objects[1].field8a, 0);
    EXPECT_EQ(objects[1].state.hiddenMesh, Client::Render::Models::NoHiddenMesh);
    EXPECT_TRUE(objects[2].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(Request().map, -1);
}
} // namespace
