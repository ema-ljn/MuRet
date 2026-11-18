#include "Scenes/HeroAttack.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "GameLogic/PathFinder.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill55cbc0.h"
#include "Scenes/Skill55faa0.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// El caso 5 de 0x5650c0 (0x56601f..0x566754, CL9 hito 4, fase 2c): el héroe llega con +0x379 en 5 y usa la skill del
// arma pendiente (0x7758580..0x7758588). El héroe en el lugar 0 en (182, 115), el blanco en el lugar 5 en (184, 117)
// con la clave 0x0a1b, el alcance 4 en un Skill.bmd inventado. Cada caso contra la llamada directa (0x55cbc0, 0x55b6f0
// o 0x55faa0) después de los mismos rand(), en Lorencia (0), Devias (2) y Blood Castle (11, el mapa de 0x55b6f0).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// La skill (el registro propio) y su alcance (0x5526f0, la entrada de Skill.bmd).
constexpr int SkillDraws = OwnDraws + EntryDraws;
// La celda lógica del héroe: la Y y la X.
constexpr int HeroCellDraws = 2 * CellDraws;
constexpr std::size_t TargetSlot = 5;
constexpr std::uint16_t TargetKey = 0x0a1b;
constexpr std::uint32_t Ticks = 100000;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint16_t BowModel = 0xa12;
constexpr std::uint16_t UniriaModel = 0x1c14;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Skill.bmd con el alcance (+0x26) de cada skill.
Client::Data::Tables::SkillTable SkillsWithRange(std::uint8_t range)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (std::size_t skill = 0; skill < 0x100; ++skill)
        plain[skill * Client::Data::Tables::SkillRecordSize + 0x26] = static_cast<std::byte>(range);
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

struct ArrivingHero
{
    ArrivingHero(std::uint8_t used, std::int32_t map, std::uint8_t range = 4) : skills(SkillsWithRange(range))
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero.alive = true;
        hero.type = Client::Data::Models::PlayerSkeletonModel;
        hero.characterClass = ElfClass;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.wings.model = hero.pet.model = Client::Scenes::NoItemModel;
        hero.hands[1].model = BowModel;
        hero.alpha = 1.0f;
        hero.animation.action = 1;
        hero.movementType = 5;
        world.OwnCharacter().SetSkillAt(3, used);
        auto& target = Target();
        target.alive = true;
        target.type = 0x20;
        target.byte88 = 2;
        target.byte370 = 2;
        target.key = TargetKey;
        target.alpha = 1.0f;
        target.position = {18450.0f, 11750.0f, 0.0f};
        runtime.movement.weaponSkill = {.active = 1, .slot = 3, .target = static_cast<std::int32_t>(TargetSlot)};
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Scenes::ActorState& Target()
    {
        return world.Actors()[TargetSlot];
    }
    Client::Scenes::GameMovementState& Movement()
    {
        return world.Runtime().movement;
    }
    void Send(Client::Protocol::Outgoing outgoing)
    {
        sent.push_back(std::move(outgoing));
    }
    Client::Scenes::HeroArrivalContext Arrival()
    {
        return {.combat = {.world = world, .random = random, .playerSkeleton = &skeleton},
                .skills = &skills,
                .tickCount = [this] { return ticks; },
                .sendSkill = [this](Client::Protocol::Outgoing outgoing) { Send(std::move(outgoing)); }};
    }
    // El mismo contexto para llamar directo a 0x55cbc0, 0x55b6f0 o 0x55faa0.
    HeroSkillContext Direct()
    {
        return {.world = world,
                .random = random,
                .skills = &skills,
                .playerSkeleton = &skeleton,
                .tickCount = [this] { return ticks; },
                .send = [this](Client::Protocol::Outgoing outgoing) { Send(std::move(outgoing)); }};
    }
    Client::Core::Expected<void, std::string> Arrive()
    {
        return Client::Scenes::InteractOnArrival(Hero(), Arrival());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        return ClearBytesOf(sent.at(index));
    }

    Client::Scenes::WorldState world;
    CrtRandom random;
    std::uint32_t ticks = Ticks;
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Data::Tables::SkillTable skills;
};

std::unique_ptr<ArrivingHero> Arriving(std::uint8_t skill, std::int32_t map, std::uint8_t range = 4)
{
    return std::make_unique<ArrivingHero>(skill, map, range);
}

using DirectCall = std::function<Client::Core::Expected<void, std::string>(ArrivingHero&)>;

// La llegada contra la llamada directa después de `before` rand() y con la celda del blanco ya anotada: los mismos
// rand(), los mismos bytes y el mismo héroe.
void ExpectTheDirectCall(std::uint8_t skill, std::int32_t map, int before, const DirectCall& call,
                         const std::function<void(ArrivingHero&)>& prepare = {})
{
    SCOPED_TRACE(skill);
    SCOPED_TRACE(map);
    const auto arrived = Arriving(skill, map);
    const auto direct = Arriving(skill, map);
    if (prepare)
    {
        prepare(*arrived);
        prepare(*direct);
    }
    ASSERT_TRUE(arrived->Arrive().HasValue());
    direct->random = Advanced(direct->random, before);
    direct->Movement().targetX = 184;
    direct->Movement().targetY = 117;
    ASSERT_TRUE(call(*direct).HasValue());
    EXPECT_EQ(arrived->random.State(), direct->random.State());
    ASSERT_EQ(arrived->sent.size(), direct->sent.size());
    for (std::size_t i = 0; i < arrived->sent.size(); ++i)
    {
        EXPECT_EQ(arrived->sent[i].delivery, direct->sent[i].delivery);
        EXPECT_EQ(arrived->Sent(i), direct->Sent(i));
    }
    EXPECT_EQ(arrived->Movement().targetX, 184u);
    EXPECT_EQ(arrived->Movement().targetY, 117u);
    EXPECT_EQ(arrived->Hero().animation.action, direct->Hero().animation.action);
    EXPECT_EQ(arrived->Hero().attackPosition, direct->Hero().attackPosition);
    EXPECT_FLOAT_EQ(arrived->Hero().angles.z, direct->Hero().angles.z);
    EXPECT_EQ(arrived->Hero().movement, direct->Hero().movement);
    EXPECT_EQ(arrived->Hero().movementType, 5);
}

Client::Core::Expected<void, std::string> OnItsTarget(ArrivingHero& hero)
{
    return Client::Scenes::UseTheSkillOnItsTarget(hero.Hero(), hero.Hero(), hero.Direct());
}

Client::Core::Expected<void, std::string> AtTheTarget(ArrivingHero& hero)
{
    return Client::Scenes::CastTheSkillAtTheTarget(hero.Hero(), hero.Hero(), hero.Direct());
}

Client::Core::Expected<void, std::string> ElfOnItsTarget(ArrivingHero& hero)
{
    return Client::Scenes::UseTheElfSkillOnItsTarget(hero.Hero(), hero.Hero(), hero.Direct());
}

// 0x56629a..0x5663b7: 1 a 4, 7, 0xb, 0x11, 0x2d y 0x49 en alcance (· 100) y a la vista: 0x55cbc0(héroe, héroe) después
// de la skill, el alcance y la celda del héroe. La 1: el 19 con la clave del blanco y el D3 con el rumbo nuevo.
TEST(HeroArrivalSkillTest, TheFirstGroupGoesTo55cbc0)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x1}, std::uint8_t{0x4}, std::uint8_t{0x11}, std::uint8_t{0x2d}})
            ExpectTheDirectCall(skill, map, SkillDraws + HeroCellDraws, OnItsTarget);
    }
    const auto tested = Arriving(0x1, 0);
    ASSERT_TRUE(tested->Arrive().HasValue());
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x01, 0x0a, 0x1b}));
    EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Clear);
    EXPECT_EQ(tested->Sent(1)[2], 0xd3);
    EXPECT_EQ(tested->Sent(1)[3], 182);
    EXPECT_EQ(tested->Sent(1)[4], 115);
    EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
}

// 0x566143..0x5661fc: 0x13 a 0x17, 0x2b, 0x2c, 0x31, 0x39, 0x3c, 0x3d y 0x4a en alcance (· 120,0000076):
// 0x55b6f0(héroe, héroe) sin la celda del héroe ni la línea de vista. La 0x2f, solo con el héroe publicado en Uniria
// (0x56612e).
TEST(HeroArrivalSkillTest, TheSecondGroupGoesTo55b6f0WithoutLookingAtTheWalls)
{
    for (const std::int32_t map : {0, 2, 11})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x2c}, std::uint8_t{0x3c}})
            ExpectTheDirectCall(skill, map, SkillDraws, AtTheTarget);
    }
    // Una pared entre los dos no cambia nada.
    ExpectTheDirectCall(0x13, 0, SkillDraws, AtTheTarget,
                        [](ArrivingHero& hero) { hero.world.Map().terrain.attributes->cells[(116u << 8) + 183u] = 4; });
    ExpectTheDirectCall(0x2f, 2, SkillDraws, AtTheTarget,
                        [](ArrivingHero& hero) { hero.Hero().pet.model = UniriaModel; });
    // La 0x13: el D3, la acción, el brillo y el 19 con la clave del blanco.
    {
        const auto tested = Arriving(0x13, 0);
        ASSERT_TRUE(tested->Arrive().HasValue());
        ASSERT_GE(tested->sent.size(), 2u);
        EXPECT_EQ(tested->Sent(0)[2], 0xd3);
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x13, 0x0a, 0x1b}));
    }
    // Sin Uniria, la 0x2f no hace nada.
    const auto tested = Arriving(0x2f, 0);
    const CrtRandom expected = Advanced(tested->random, SkillDraws);
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Movement().targetX, 0u);
}

// 0x56642c..0x56654f y 0x5665c3..0x5666e0: la 0x2e y la 0x33, y las 0x1a a 0x1c, en alcance y a la vista: 0x55faa0.
// El D3 con el rumbo de antes (0: 0x10) y el 19 con la clave del blanco.
TEST(HeroArrivalSkillTest, TheElfSkillsGoTo55faa0)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill :
             {std::uint8_t{0x1a}, std::uint8_t{0x1c}, std::uint8_t{0x2e}, std::uint8_t{0x33}})
            ExpectTheDirectCall(skill, map, SkillDraws + HeroCellDraws, ElfOnItsTarget);
    }
    // La 0x1b: la skill, el alcance, la celda del héroe, la del publicado y el registro propio de 0x55faa0, el 19 y
    // 0x4f5290 (0x4f3f80, el registro propio).
    const auto tested = Arriving(0x1b, 2);
    const CrtRandom expected =
        Advanced(tested->random, SkillDraws + HeroCellDraws + HeroCellDraws + OwnDraws + SendDraws + OwnDraws);
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
    EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x1b, 0x0a, 0x1b}));
    EXPECT_EQ(tested->Hero().animation.action, 0x92);
}

// 0x56609c..0x5660b1: sin el byte de 0x7758580, 0x7758584 es la skill; el registro propio se abre igual.
TEST(HeroArrivalSkillTest, WithoutTheActiveByteTheSlotIsTheSkill)
{
    const auto tested = Arriving(0x13, 0);
    tested->Movement().weaponSkill.active = 0x100;
    tested->Movement().weaponSkill.slot = 0x5;
    const CrtRandom expected = Advanced(tested->random, SkillDraws);
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    // Una skill de más de un byte no está en Skill.bmd.
    tested->Movement().weaponSkill.slot = 0x113;
    const auto cut = tested->Arrive();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x566108"), std::string::npos) << cut.Error();
}

// Las salidas: el blanco en −1 o muriéndose (+0x389) después del alcance; la 0x1a sin la línea de vista después de la
// celda del héroe; el blanco fuera del pool corta.
TEST(HeroArrivalSkillTest, TheEarlyExitsDoNothing)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = Arriving(0x1a, map);
        tested->Movement().weaponSkill.target = -1;
        CrtRandom expected = Advanced(tested->random, SkillDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());

        tested->Movement().weaponSkill.target = static_cast<std::int32_t>(TargetSlot);
        tested->Target().dying = 1;
        expected = Advanced(expected, SkillDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 0u);

        tested->Target().dying = 0;
        tested->world.Map().terrain.attributes->cells[(116u << 8) + 183u] = 4;
        expected = Advanced(expected, SkillDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 184u);
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    const auto tested = Arriving(0x1a, 0);
    tested->Movement().weaponSkill.target = static_cast<std::int32_t>(tested->world.Actors().size());
    const auto cut = tested->Arrive();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x56615b"), std::string::npos) << cut.Error();
}

// 0x56620d..0x566299: fuera de alcance, el camino de PathFinding2 hasta el alcance desde la celda del héroe (la Y y la
// X), · 1,2 en un float con 0x55b6f0; con él, +0x378 en 1 y +0x379 sigue en 5. El alcance 5 y el blanco diez celdas
// al este, en (192, 115): el 5 y el 6 terminan en celdas distintas.
TEST(HeroArrivalSkillTest, OutOfReachTheHeroWalksIntoReach)
{
    struct Probe
    {
        std::uint8_t skill;
        float distance;
    };
    for (const std::int32_t map : {0, 2})
    {
        for (const Probe probe : {Probe{0x1, 5.0f}, Probe{0x13, 6.0f}, Probe{0x1a, 5.0f}, Probe{0x33, 5.0f}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(probe.skill);
            const auto tested = Arriving(probe.skill, map, 5);
            tested->Target().position = {19250.0f, 11550.0f, 0.0f};
            const CrtRandom expected = Advanced(tested->random, SkillDraws + HeroCellDraws);
            ASSERT_TRUE(tested->Arrive().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
            const auto path = Client::GameLogic::PathFinder(tested->world.Map().terrain.attributes->cells)
                                  .Find({182, 115}, {192, 115}, false, probe.distance);
            ASSERT_TRUE(path.has_value());
            EXPECT_EQ(tested->Hero().movement, 1);
            EXPECT_EQ(tested->Hero().movementType, 5);
            ASSERT_EQ(tested->Hero().path.count, path->count);
            EXPECT_EQ(tested->Hero().path.cells[path->count - 1].x, path->cells[path->count - 1].x);
            EXPECT_EQ(tested->Hero().path.cells[path->count - 1].y, path->cells[path->count - 1].y);
            EXPECT_EQ(tested->Movement().targetX, 192u);
            EXPECT_EQ(tested->Movement().targetY, 115u);
        }
    }
    const auto walls = Arriving(0x1, 0);
    auto& cells = walls->world.Map().terrain.attributes->cells;
    const auto near = Client::GameLogic::PathFinder(cells).Find({182, 115}, {192, 115}, false, 5.0f);
    const auto far = Client::GameLogic::PathFinder(cells).Find({182, 115}, {192, 115}, false, 6.0f);
    ASSERT_TRUE(near && far);
    EXPECT_NE(near->count, far->count);
}

// P-C256: fuera de alcance, el blanco con la celda fuera de 0..255 (que PathFinding2, 0x566272, toma como índice
// lineal) no lleva camino: después de leer la Y y la X del héroe, el camino se descarta sin cortar. En alcance, la
// línea de vista que sale de TerrainWall (0x56639d → 0x55886d, la celda 256 de la fila 255) se toma como sin línea.
TEST(HeroArrivalSkillTest, ACellOffTheGridDiscardsTheWalkAndTheLine)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const glm::vec3 position : {glm::vec3{-1000.0f, 11550.0f, 0.0f}, glm::vec3{18450.0f, 30000.0f, 0.0f}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(position.x));
            const auto tested = Arriving(0x1, map);
            tested->Target().position = position;
            const CrtRandom expected = Advanced(tested->random, SkillDraws + HeroCellDraws);
            const auto result = tested->Arrive();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
            EXPECT_EQ(tested->Hero().movement, 0);
            EXPECT_EQ(tested->Hero().path.count, 0);
        }
        SCOPED_TRACE(map);
        const auto edge = Arriving(0x1, map);
        edge->Hero().cellX = 255;
        edge->Hero().cellY = 255;
        edge->Hero().position = {25550.0f, 25550.0f, 0.0f};
        edge->Target().position = {25650.0f, 25550.0f, 0.0f};
        const CrtRandom expected = Advanced(edge->random, SkillDraws + HeroCellDraws);
        const auto result = edge->Arrive();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(edge->random.State(), expected.State());
        EXPECT_TRUE(edge->sent.empty());
        EXPECT_EQ(edge->Movement().targetX, 256u);
    }
}

// 0x5661e8 y sus pares: en alcance pero en la zona segura, el camino en lugar de la skill, sin envíos; con el héroe ya
// en alcance, PathFinding2 no da un camino de dos celdas y no camina (Q-C1217).
TEST(HeroArrivalSkillTest, InTheSafeZoneTheHeroWalksInstead)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1}, std::uint8_t{0x13}, std::uint8_t{0x33}})
    {
        SCOPED_TRACE(skill);
        const auto tested = Arriving(skill, 0);
        tested->Hero().safeZone = 1;
        const CrtRandom expected = Advanced(tested->random, SkillDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        const float distance = skill == 0x13 ? 4.8f : 4.0f;
        const auto path = Client::GameLogic::PathFinder(tested->world.Map().terrain.attributes->cells)
                              .Find({182, 115}, {184, 117}, false, distance);
        EXPECT_FALSE(path.has_value());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// Las skills fuera de las tablas (0x5, 0x34, 0x43) y la 0: la skill y el alcance, y nada más.
TEST(HeroArrivalSkillTest, TheOtherSkillsDoNothing)
{
    for (const std::uint8_t skill : {std::uint8_t{0}, std::uint8_t{0x5}, std::uint8_t{0x34}, std::uint8_t{0x43}})
    {
        SCOPED_TRACE(skill);
        const auto tested = Arriving(skill, 2);
        const CrtRandom expected = Advanced(tested->random, SkillDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().targetX, 0u);
    }
}
} // namespace
