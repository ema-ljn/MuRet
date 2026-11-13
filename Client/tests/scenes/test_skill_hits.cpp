#include "Scenes/SkillHits.h"

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/SameBytes.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::HitTheTargetsAround: 0x517a20, los blancos de una skill alrededor de un punto y el C1 10 que los
// manda (06, "La flecha").
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::HitTheTargetsAround;
using Client::Scenes::SkillHitsRequest;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t SkillPlace = 3;
constexpr int OwnRecord = Client::Render::Models::OwnRecordCloseRandoms;
// 0x518515..0x518606: el relleno y el serial del C3.
constexpr int Delivery = 2;

int DrawsSince(std::uint32_t seed, const CrtRandom& random)
{
    constexpr int Limit = 1'000'000;
    CrtRandom probe(seed);
    int draws = 0;
    while (probe.State() != random.State() && draws < Limit)
    {
        static_cast<void>(probe.Next());
        ++draws;
    }
    return draws;
}

struct HitsWorld
{
    HitsWorld(std::int32_t map, std::uint8_t skill)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().SetSkillAt(SkillPlace, skill);
        ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.seen = true;
        hero.byte88 = 1;
        hero.key = 0x0101;
        hero.position = {12345.0f, 6789.0f, 0.0f};
    }

    // Un monstruo (+0x88 en 2) o un jugador (en 1), vivo y visto, a `distance` del punto en x.
    ActorState& Put(std::size_t slot, std::uint8_t kind, std::uint16_t key, float distance)
    {
        ActorState& actor = world.Actors()[slot];
        actor = ActorState{};
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = kind;
        actor.key = key;
        actor.position = {Point.x + distance, Point.y, 0.0f};
        return actor;
    }

    Client::Core::Expected<bool, std::string> Hit(const SkillHitsRequest& request)
    {
        return HitTheTargetsAround(request, world, random,
                                   [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); });
    }

    static constexpr glm::vec3 Point{12345.0f, 6789.0f, 150.0f};
    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    CrtRandom random{Seed};
    std::vector<Client::Protocol::Outgoing> sent;
};

SkillHitsRequest Request(float radius)
{
    return SkillHitsRequest{.skillIndex = SkillPlace,
                            .point = HitsWorld::Point,
                            .radius = radius,
                            .volley = 0x0c,
                            .key = 0x0222,
                            .extra = 0};
}

void Name(ActorState& actor, std::string_view name)
{
    for (std::size_t index = 0; index < name.size() && index < actor.name.size(); ++index)
        actor.name[index] = static_cast<std::byte>(name[index]);
}

// En Lorencia y en Devias, de los 400 en orden: entran los monstruos a 100 o menos y el jugador de la clave; quedan
// afuera el jugador sin la clave, el monstruo a más de 100, el que no se ve, el que se muere y el que no está vivo. El
// C1 10 lleva la skill, la celda del punto (ftol(x · 0,01) & 0xff = 123 y 67), la tanda, la cuenta y las claves, con
// los 3.848 rand() del registro propio y los dos del C3.
TEST(SkillHitsTest, TheMonstersAroundAndThePlayerOfTheKeyAreHit)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        HitsWorld tested(map, 0x18);
        tested.Put(5, 2, 0x1005, 100.0f);
        tested.Put(6, 1, 0x1006, 10.0f);
        tested.Put(7, 1, 0x0222, 20.0f);
        tested.Put(8, 2, 0x1008, 100.5f);
        tested.Put(9, 2, 0x1009, 10.0f).seen = false;
        tested.Put(10, 2, 0x100a, 10.0f).dying = 1;
        tested.Put(11, 2, 0x100b, 10.0f).alive = false;
        tested.Put(12, 3, 0x100c, 10.0f);
        tested.Put(13, 2, 0x100d, -60.0f);

        const auto hit = tested.Hit(Request(100.0f));

        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        EXPECT_TRUE(hit.Value());
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + Delivery);
        ASSERT_EQ(tested.sent.size(), 1u);
        Client::Protocol::Skill::SkillHitsSend expected{.skill = 0x18, .x = 123, .y = 67, .volley = 0x0c, .hits = 3};
        expected.keys = {0x1005, 0x0222, 0x100d, 0, 0};
        EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                             Client::Protocol::Skill::WriteSkillHits(expected).packet.Bytes()));
    }
}

// Hasta cinco: el sexto monstruo no entra. Con la skill 8 o 9, cada uno con +0x394 en 0 queda con +0x398 en 10; con
// otra, ninguno.
TEST(SkillHitsTest, FiveAtMostAndTheEighthAndNinthSkillsMark)
{
    for (const std::uint8_t skill : {std::uint8_t{8}, std::uint8_t{9}, std::uint8_t{0x33}})
    {
        SCOPED_TRACE(static_cast<int>(skill));
        HitsWorld tested(2, skill);
        for (std::size_t slot = 20; slot < 26; ++slot)
            tested.Put(slot, 2, static_cast<std::uint16_t>(0x2000 + slot), 5.0f);
        tested.world.Actors()[21].dword394 = 1;

        const auto hit = tested.Hit(Request(100.0f));

        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        ASSERT_EQ(tested.sent.size(), 1u);
        Client::Protocol::Skill::SkillHitsSend expected{.skill = skill, .x = 123, .y = 67, .volley = 0x0c, .hits = 5};
        expected.keys = {0x2014, 0x2015, 0x2016, 0x2017, 0x2018};
        EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                             Client::Protocol::Skill::WriteSkillHits(expected).packet.Bytes()));
        const bool marks = skill != 0x33;
        for (std::size_t slot = 20; slot < 26; ++slot)
        {
            const bool marked = marks && slot != 21 && slot != 25;
            EXPECT_EQ(tested.world.Actors()[slot].byte398, marked ? 10 : 0) << slot;
        }
    }
}

// Sin nadie, false con solo los rand() del registro propio. Con "webzen" en el nombre del héroe, true sin mandar nada
// ni los rand() del C3. El héroe nunca entra, aunque esté en el punto.
TEST(SkillHitsTest, NobodyOrTheGameMasterSendNothing)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        HitsWorld nobody(map, 0x18);
        nobody.world.Actors()[HeroSlot].position = HitsWorld::Point;
        const auto missed = nobody.Hit(Request(100.0f));
        ASSERT_TRUE(missed.HasValue()) << missed.Error();
        EXPECT_FALSE(missed.Value());
        EXPECT_TRUE(nobody.sent.empty());
        EXPECT_EQ(DrawsSince(Seed, nobody.random), OwnRecord);

        HitsWorld master(map, 0x18);
        Name(master.world.Actors()[HeroSlot], "xwebzenq");
        master.Put(5, 2, 0x1005, 1.0f);
        const auto hit = master.Hit(Request(100.0f));
        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        EXPECT_TRUE(hit.Value());
        EXPECT_TRUE(master.sent.empty());
        EXPECT_EQ(DrawsSince(Seed, master.random), OwnRecord);
    }
}

// Sin la sesión, los dos rand() del C3 salen igual. Con el radio NaN, la comparación de la x87 deja entrar a todos.
TEST(SkillHitsTest, WithoutTheSessionTheDeliveryStillDraws)
{
    HitsWorld tested(0, 0x18);
    tested.Put(5, 2, 0x1005, 5000.0f);
    const auto hit =
        HitTheTargetsAround(Request(std::numeric_limits<float>::quiet_NaN()), tested.world, tested.random, nullptr);
    ASSERT_TRUE(hit.HasValue()) << hit.Error();
    EXPECT_TRUE(hit.Value());
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + Delivery);
}

// En el mapa 30 con el asedio (0x4294b0), el atributo de la celda del punto corta (0x517b1e, CL9.5), después de leer
// la skill; sin el asedio, el 30 es como los demás. Un punto que el ftol no representa corta en 0x517deb.
TEST(SkillHitsTest, TheSiegeAndTheOutOfRangePointCut)
{
    HitsWorld siege(30, 0x18);
    siege.world.Runtime().siegeActive = true;
    siege.Put(5, 2, 0x1005, 1.0f);
    const auto cut = siege.Hit(Request(100.0f));
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x517b1e"), std::string::npos) << cut.Error();
    EXPECT_EQ(DrawsSince(Seed, siege.random), OwnRecord);

    HitsWorld calm(30, 0x18);
    calm.Put(5, 2, 0x1005, 1.0f);
    const auto hit = calm.Hit(Request(100.0f));
    ASSERT_TRUE(hit.HasValue()) << hit.Error();
    EXPECT_TRUE(hit.Value());
    EXPECT_EQ(calm.sent.size(), 1u);

    HitsWorld far(0, 0x18);
    SkillHitsRequest request = Request(std::numeric_limits<float>::infinity());
    request.point.x = 1.0e30f;
    far.Put(5, 2, 0x1005, 1.0f);
    const auto outside = far.Hit(request);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x517deb"), std::string::npos) << outside.Error();
    EXPECT_TRUE(far.sent.empty());
}
} // namespace
