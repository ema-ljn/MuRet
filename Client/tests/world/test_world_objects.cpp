#include "support/CheckedWorldObjectProjection.h"

#include "WorldObjects/WorldObjects.h"

#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Models/ObjectState.h"
#include "Kernel/WorldObject.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::WorldObjects::CreateWorldObjects, StepWorldObject and DrawOf in the cases the 276 vectors of map 55 do not
// touch (06 § El mundo del login, the CL7.2 review), and StepWorldObjectChecked with the cases of maps 0 and 2 (06 §
// Los objetos del juego).
namespace
{
using Client::Kernel::WorldObject;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;
constexpr std::uint8_t Devias = 2;

std::vector<WorldObject> Created(std::int16_t type, float x, float y, std::uint8_t map = Login)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {x, y, 0.0f};
    record.scale = 1.0f;
    return Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), map,
                                                       Client::WorldObjects::ObjectScene::Login);
}

// 0x60666c: the position times 0.000625f in the x87, without rounding the product, and ftol. At multiples of 1600 the
// float goes up to the next integer; the x87 does not.
TEST(WorldObjectsTest, TheSectorIsTheProductWithoutRounding)
{
    EXPECT_EQ(Client::WorldObjects::SectorX(Created(1, 1600.0f, 0.0f).front()), 0);
    EXPECT_EQ(Client::WorldObjects::SectorX(Created(1, 14400.0f, 0.0f).front()), 8);
    EXPECT_EQ(Client::WorldObjects::SectorX(Created(1, 8000.0f, 0.0f).front()), 4);
    const auto edge = Created(1, 25600.0f, 25600.0f);
    ASSERT_EQ(edge.size(), 1u) << "25.600 es el sector 15: se crea";
    EXPECT_EQ(Client::WorldObjects::SectorX(edge.front()), 15);
    EXPECT_EQ(Client::WorldObjects::SectorY(edge.front()), 15);
    EXPECT_EQ(Client::WorldObjects::SectorX(Created(1, -1000.0f, 0.0f).front()), 0) << "ftol trunca hacia el 0";
    EXPECT_EQ(Client::WorldObjects::SectorX(Created(1, -1600.0f, 0.0f).front()), 0) << "−0,99999…: todavía el 0";
    EXPECT_TRUE(Created(1, -1601.0f, 0.0f).empty());
    EXPECT_TRUE(Created(1, 25700.0f, 0.0f).empty());
}

// Q-C186: records 492 and 493 of World3\EncTerrain3.obj (type 86, y at 0xFFFFFFFF) are in the original's list 32.
// With the invalid operation masked, ftol gives 0x8000000000000000 with a NaN, and 0x606660 compares the low dword
// signed (0x60668d..0x6066ab): sector 0, and the object is created.
TEST(WorldObjectsTest, ANotANumberPositionGoesToSectorZeroAndIsCreated)
{
    const auto objects = Created(86, 4000.0f, std::bit_cast<float>(0xFFFFFFFFu), Devias);
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_EQ(Client::WorldObjects::SectorX(objects.front()), 2);
    EXPECT_EQ(Client::WorldObjects::SectorY(objects.front()), 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(objects.front().transform.position.y), 0xFFFFFFFFu);
}

// 0x60668d..0x6066ab looks at EAX, ftol's low dword, not the product: 2^65 times 0.000625f gives 0x51eb85 · 2^32,
// exact and with low dword 0, sector 0. Infinity and what does not fit in int64 stay unobserved (Q-C186): they are the
// port's boundary, which does not create them, not a result of the original.
TEST(WorldObjectsTest, TheSectorIsTheSignedLowDwordOfFtol)
{
    for (const float x : {0x1p65f, -0x1p65f})
    {
        const auto objects = Created(1, x, 0.0f);
        ASSERT_EQ(objects.size(), 1u) << x;
        EXPECT_EQ(Client::WorldObjects::SectorX(objects.front()), 0) << x;
    }
    for (const float x :
         {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0x1p74f, -0x1p74f})
    {
        EXPECT_TRUE(Created(1, x, 0.0f).empty()) << x;
    }
}

// 0x606660 takes the sector from the file's position, before its switch: the 55's whirlpools, which the case moves to
// (8600; 25000; 5000), stay in the sector they were in.
TEST(WorldObjectsTest, TheWhirlpoolsKeepTheSectorOfTheFile)
{
    const auto whirlpool = Created(0x56, 100.0f, 100.0f);
    ASSERT_EQ(whirlpool.size(), 1u);
    EXPECT_EQ(whirlpool.front().transform.position.x, 8600.0f);
    EXPECT_EQ(Client::WorldObjects::SectorX(whirlpool.front()), 0);
    EXPECT_EQ(Client::WorldObjects::SectorY(whirlpool.front()), 0);
}

// Pins an inference: 06 gives +0xdc = -50 for Bridge01 as *inferred*; its static reading is missing (P-C35 of 08).
// 0x6067ae initializes +0xdc to 0xc1f00000; only 0x6069bd (map 0, Bridge01) sets 0xc2480000. The map branch runs
// after the scene cases, also outside the game.
TEST(WorldObjectsTest, TheBridgeCullingMarginDependsOnItsMapNotItsScene)
{
    const std::array<Client::Data::Terrain::TerrainObject, 2> records{
        Client::Data::Terrain::TerrainObject{
            .type = 0x50, .position = {1400.0f, 500.0f, 25.0f}, .angle = {10.0f, 20.0f, 30.0f}, .scale = 2.0f},
        Client::Data::Terrain::TerrainObject{
            .type = 0x51, .position = {1400.0f, 500.0f, 25.0f}, .angle = {10.0f, 20.0f, 30.0f}, .scale = 2.0f}};
    struct Case
    {
        std::uint8_t map;
        std::uint32_t bridgeMargin;
    };
    constexpr std::array cases{Case{0, 0xC2480000u}, Case{55, 0xC1F00000u}, Case{0, 0xC2480000u}, Case{54, 0xC1F00000u},
                               Case{10, 0xC1F00000u}};
    std::vector<WorldObject> objects;
    for (const auto scene : {Client::WorldObjects::ObjectScene::Other, Client::WorldObjects::ObjectScene::Login,
                             Client::WorldObjects::ObjectScene::CharacterSelect})
    {
        SCOPED_TRACE(static_cast<int>(scene));
        for (const Case& test : cases)
        {
            SCOPED_TRACE(static_cast<int>(test.map));
            objects = Client::Tests::CheckedWorldObjectProjection(records, test.map, scene);
            ASSERT_EQ(objects.size(), 2u);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].cullingMargin), test.bridgeMargin);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[1].cullingMargin), 0xC1F00000u);
            for (const WorldObject& object : objects)
            {
                EXPECT_EQ(object.transform.position, glm::vec3(1400.0f, 500.0f, 25.0f));
                EXPECT_EQ(object.transform.angles, glm::vec3(10.0f, 20.0f, 30.0f));
                EXPECT_EQ(object.transform.scale, 2.0f);
                EXPECT_EQ(object.sectorX, 0);
                EXPECT_EQ(object.sectorY, 0);
            }
        }
    }
}

TEST(WorldObjectsTest, OtherLorenciaTypesKeepTheDefaultCullingMargin)
{
    for (const std::int16_t type : std::array<std::int16_t, 6>{0, 1, 0x4f, 0x51, 0x96, 0x97})
    {
        SCOPED_TRACE(type);
        const auto objects = Created(type, 1400.0f, 500.0f, Lorencia);
        ASSERT_EQ(objects.size(), 1u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].cullingMargin), 0xC1F00000u);
    }
}

// Catches omitted cases of switch 0, a blend inherited by mistake or the false fallthrough to map 1.
TEST(WorldObjectsTest, LorenciaCreationCoversEveryDirectType)
{
    struct Changed
    {
        std::int16_t type;
        std::int16_t blend;
        std::uint32_t speed;
    };
    constexpr std::array changes{
        Changed{0, -1, 0x3E4CCCCDu},  Changed{1, -1, 0x3E4CCCCDu},  Changed{52, 1, 0x3E23D70Au},
        Changed{59, -1, 0u},          Changed{90, 1, 0x3E99999Au},  Changed{150, 1, 0x3E99999Au},
        Changed{96, -1, 0x3E99999Au}, Changed{97, -1, 0x3E99999Au}, Changed{98, 2, 0x3E23D70Au},
        Changed{119, 2, 0x3E23D70Au}, Changed{105, 3, 0x3E23D70Au}, Changed{117, 4, 0x3E23D70Au},
        Changed{122, 4, 0x3E23D70Au}, Changed{118, 8, 0x3E23D70Au}};
    for (int type = -1; type <= 151; ++type)
    {
        SCOPED_TRACE(type);
        Client::Data::Terrain::TerrainObject record{.type = static_cast<std::int16_t>(type),
                                                    .position = {100.0f, 100.0f, 25.0f},
                                                    .angle = {10.0f, 20.0f, 30.0f},
                                                    .scale = 2.0f};
        Client::Kernel::WorldObjectRegistrations registry;
        const auto objects = Client::Tests::CheckedWorldObjectProjection(
            std::span(&record, 1), Lorencia, Client::WorldObjects::ObjectScene::Other, &registry);
        ASSERT_EQ(objects.size(), 1u);
        const WorldObject& object = objects.front();
        std::int16_t blend = -1;
        std::uint32_t speed = 0x3E23D70Au;
        for (const Changed& change : changes)
        {
            if (change.type == type)
            {
                blend = change.blend;
                speed = change.speed;
            }
        }
        EXPECT_EQ(object.state.blendMesh, blend);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(object.speed), speed);
        EXPECT_EQ(object.state.hiddenMesh, type == 133 ? -2 : -1);
        EXPECT_EQ(object.transform.scale, 2.0f);
        EXPECT_EQ(object.transform.position, glm::vec3(100.0f, 100.0f, 25.0f));
        EXPECT_EQ(object.transform.angles, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(object.light, glm::vec3(0.0f));
        EXPECT_TRUE(object.vertexLight);
        EXPECT_EQ(object.state.light, 1.0f);
        EXPECT_EQ(object.alpha, 1.0f);
        EXPECT_EQ(object.targetAlpha, 1.0f);
        EXPECT_FALSE(object.seen);
        const bool registered = type == 6 || type == 133 || type == 145 || type == 146;
        EXPECT_EQ(registry.entries[0].active, registered ? 1 : 0);
        EXPECT_EQ(registry.entries[0].object, registered ? object.identity : 0);
        EXPECT_EQ(registry.entries[1].active, 0);
    }
}

// Catches the scene case being skipped on common maps or running outside scenes 2 and 4.
TEST(WorldObjectsTest, CreationAppliesEverySupportedSceneOverrideBeforeMapCases)
{
    struct Case
    {
        std::int16_t type;
        std::uint32_t scale;
        float light;
        std::int16_t blend;
        bool vertexLight;
    };
    constexpr std::array cases{Case{60, 0x3F4CCCCDu, 0.2f, -1, true}, Case{160, 0x3D343958u, 1.0f, 1, false},
                               Case{161, 0x3F4CCCCDu, 1.0f, 0, true}, Case{162, 0x400CCCCDu, 0.0f, 1, false},
                               Case{163, 0x40400000u, 0.5f, 0, true}, Case{164, 0x40000000u, 1.0f, 10, true},
                               Case{180, 0x40000000u, 1.0f, -1, true}};
    for (const auto scene :
         {Client::WorldObjects::ObjectScene::Login, Client::WorldObjects::ObjectScene::CharacterSelect,
          Client::WorldObjects::ObjectScene::Other})
    {
        SCOPED_TRACE(static_cast<int>(scene));
        for (const std::uint8_t map : std::array<std::uint8_t, 3>{0, 55, 10})
        {
            SCOPED_TRACE(map);
            for (const Case& test : cases)
            {
                SCOPED_TRACE(test.type);
                Client::Data::Terrain::TerrainObject record{
                    .type = test.type, .position = {1600.0f, 14400.0f, 25.0f}, .scale = 2.0f};
                Client::Kernel::WorldObjectRegistrations registry;
                const auto objects =
                    Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), map, scene, &registry);
                ASSERT_EQ(objects.size(), 1u);
                const WorldObject& object = objects.front();
                const bool special = scene != Client::WorldObjects::ObjectScene::Other;
                EXPECT_EQ(std::bit_cast<std::uint32_t>(object.transform.scale), special ? test.scale : 0x40000000u);
                EXPECT_EQ(object.light, glm::vec3(special ? test.light : 0.0f));
                EXPECT_EQ(object.state.blendMesh, special ? test.blend : -1);
                EXPECT_EQ(object.vertexLight, special ? test.vertexLight : true);
                EXPECT_EQ(object.state.hiddenMesh, special && test.type == 180 ? -2 : -1);
                EXPECT_EQ(object.sectorX, 0);
                EXPECT_EQ(object.sectorY, 8);
                EXPECT_EQ(registry.entries[0].active, 0) << "60/map0 no cae al registro del mapa1";
            }
        }
    }
}

// Catches a table discarded when creating another batch, IDs tied to addresses and registering the wrong type.
TEST(WorldObjectsTest, LorenciaRegistersOnlyItsFourTypesInTheCallersExistingTable)
{
    Client::Kernel::WorldObjectRegistrations registry;
    registry.nextIdentity = 1000;
    registry.entries[0] = {.active = 2, .metadata = {7, 6, 5, 4, 3, 2, 1}, .object = 987};
    const std::array<Client::Data::Terrain::TerrainObject, 7> records{
        Client::Data::Terrain::TerrainObject{.type = 6, .position = {100.0f, 100.0f, 10.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 133, .position = {100.0f, 100.0f, 20.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 145, .position = {100.0f, 100.0f, 30.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 146, .position = {100.0f, 100.0f, 40.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 52, .position = {100.0f, 100.0f, 50.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 6, .position = {25700.0f, 100.0f, 60.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 6, .position = {100.0f, 100.0f, 70.0f}, .scale = 1.0f}};
    auto first = Client::Tests::CheckedWorldObjectProjection(records, Lorencia,
                                                             Client::WorldObjects::ObjectScene::Other, &registry);
    ASSERT_EQ(first.size(), 6u);
    EXPECT_EQ(registry.entries[0].active, 2);
    EXPECT_EQ(registry.entries[0].object, 987u);
    EXPECT_EQ(registry.entries[0].metadata, (std::array<std::uint8_t, 7>{7, 6, 5, 4, 3, 2, 1}));
    constexpr std::array<Client::Kernel::WorldObjectIdentity, 5> registered{1000, 1001, 1002, 1003, 1005};
    for (std::size_t index = 0; index < registered.size(); ++index)
    {
        EXPECT_EQ(registry.entries[index + 1].active, 1);
        EXPECT_EQ(registry.entries[index + 1].object, registered[index]);
    }
    EXPECT_EQ(registry.entries[6].active, 0);
    EXPECT_EQ(registry.nextIdentity, 1006u);
    EXPECT_EQ(first[1].state.hiddenMesh, -2);
    EXPECT_EQ(first[1].boundsMinimum, glm::vec3(-40.0f, -40.0f, 0.0f));
    EXPECT_EQ(first[1].boundsMaximum, glm::vec3(40.0f, 40.0f, 160.0f));
    auto moved = std::move(first);
    moved.reserve(4096);
    for (std::size_t index = 0; index < moved.size(); ++index)
    {
        EXPECT_EQ(moved[index].identity, 1000u + index);
    }
    const auto second = Client::Tests::CheckedWorldObjectProjection(
        std::span(records).first(1), Lorencia, Client::WorldObjects::ObjectScene::Other, &registry);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].identity, 1006u);
    EXPECT_EQ(registry.entries[6].object, 1006u);
    EXPECT_EQ(registry.nextIdentity, 1007u);
    EXPECT_EQ(moved[0].identity, registry.entries[1].object);
}

// Catches the auxiliary limit being mistaken for an object pool or reinitializing metadata/objects.
TEST(WorldObjectsTest, FullAuxiliaryTableStillCreatesEveryNewObject)
{
    Client::Kernel::WorldObjectRegistrations registry;
    for (auto& entry : registry.entries)
    {
        entry = {.active = 2, .metadata = {1, 2, 3, 4, 5, 6, 7}, .object = 99};
    }
    registry.nextIdentity = 100;
    const auto previous = registry.entries;
    std::vector<Client::Data::Terrain::TerrainObject> records(401);
    for (auto& record : records)
    {
        record = {.type = 133, .position = {100.0f, 100.0f, 0.0f}, .scale = 1.0f};
    }
    const auto objects = Client::Tests::CheckedWorldObjectProjection(
        records, Lorencia, Client::WorldObjects::ObjectScene::Other, &registry);
    ASSERT_EQ(objects.size(), 401u);
    EXPECT_EQ(registry.entries, previous);
    EXPECT_EQ(registry.nextIdentity, 501u);
    EXPECT_EQ(objects.front().identity, 100u);
    EXPECT_EQ(objects.back().identity, 500u);
    for (const WorldObject& object : objects)
    {
        EXPECT_TRUE(object.alive);
        EXPECT_FALSE(object.seen);
        EXPECT_EQ(object.alpha, 1.0f);
        EXPECT_EQ(object.boundsMaximum, glm::vec3(40.0f, 40.0f, 160.0f));
        EXPECT_EQ(object.state.hiddenMesh, -2);
    }
}

TEST(WorldObjectsTest, RegistrationIsAMapCaseInEverySceneAndNotAnotherMap)
{
    const std::array<Client::Data::Terrain::TerrainObject, 4> records{
        Client::Data::Terrain::TerrainObject{.type = 6, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 133, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 145, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 146, .scale = 1.0f}};
    for (const auto scene :
         {Client::WorldObjects::ObjectScene::Login, Client::WorldObjects::ObjectScene::CharacterSelect,
          Client::WorldObjects::ObjectScene::Other})
    {
        for (const std::uint8_t map : std::array<std::uint8_t, 3>{0, 1, 55})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(scene));
            Client::Kernel::WorldObjectRegistrations registry;
            const auto objects = Client::Tests::CheckedWorldObjectProjection(records, map, scene, &registry);
            ASSERT_EQ(objects.size(), 4u);
            EXPECT_EQ(registry.entries[0].active, map == 0 ? 1 : 0);
            EXPECT_EQ(registry.entries[3].active, map == 0 ? 1 : 0);
            EXPECT_EQ(registry.entries[4].active, 0);
            EXPECT_EQ(registry.nextIdentity, 5u);
        }
    }
}

// Catches premature binary32 rounding of the reciprocal (7 gives ...a2 if it spills).
TEST(WorldObjectsTest, TreeSpeedRetainsTheWideReciprocalUntilItsOnlyStore)
{
    struct Case
    {
        std::uint32_t scale;
        std::uint32_t speed;
    };
    constexpr std::array cases{Case{0x40E00000u, 0x3D6A0EA1u}, Case{0x41300000u, 0x3D14F209u},
                               Case{0x3E99999Au, 0x3FAAAAAAu}, Case{0xC0E00000u, 0xBD6A0EA1u},
                               Case{0u, 0x7F800000u},          Case{0x80000000u, 0xFF800000u},
                               Case{0x7F800000u, 0u}};
    for (const Case& test : cases)
    {
        for (const std::int16_t type : std::array<std::int16_t, 2>{0, 1})
        {
            SCOPED_TRACE(test.scale);
            SCOPED_TRACE(type);
            Client::Data::Terrain::TerrainObject record{.type = type, .scale = std::bit_cast<float>(test.scale)};
            const auto objects = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), Lorencia,
                                                                             Client::WorldObjects::ObjectScene::Other);
            ASSERT_EQ(objects.size(), 1u);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].speed), test.speed);
            EXPECT_EQ(objects[0].boundsMinimum, glm::vec3(-150.0f, -150.0f, 0.0f));
            EXPECT_EQ(objects[0].boundsMaximum, glm::vec3(150.0f, 150.0f, 500.0f));
        }
    }
}

// Catches defaults borrowed from actors and data inherited from the previous batch.
TEST(WorldObjectsTest, NewObjectsHaveFreshConstructorStateAndTheSecondPositionCopy)
{
    Client::Kernel::WorldObjectRegistrations registry;
    Client::Data::Terrain::TerrainObject record{
        .type = 6, .position = {1600.0f, 14400.0f, 25.0f}, .angle = {10.0f, 20.0f, 30.0f}, .scale = 2.0f};
    auto previous = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), Lorencia,
                                                                Client::WorldObjects::ObjectScene::Other, &registry);
    ASSERT_EQ(previous.size(), 1u);
    previous[0].alive = false;
    previous[0].alpha = 0.0f;
    previous[0].seen = true;
    previous[0].boundsMaximum = glm::vec3(900.0f);
    const auto created = Client::Tests::CheckedWorldObjectProjection(
        std::span(&record, 1), Lorencia, Client::WorldObjects::ObjectScene::Other, &registry);
    ASSERT_EQ(created.size(), 1u);
    const WorldObject& object = created[0];
    EXPECT_NE(object.identity, previous[0].identity);
    EXPECT_EQ(registry.entries[0].object, previous[0].identity);
    EXPECT_EQ(registry.entries[1].object, object.identity);
    EXPECT_TRUE(object.alive);
    EXPECT_FALSE(object.seen);
    EXPECT_EQ(object.basePosition, glm::vec3(1600.0f, 14400.0f, 25.0f));
    EXPECT_EQ(object.boundsMinimum, glm::vec3(-40.0f, -40.0f, 0.0f));
    EXPECT_EQ(object.boundsMaximum, glm::vec3(40.0f, 40.0f, 80.0f));
    EXPECT_EQ(object.fieldE0, 50.0f);
    EXPECT_EQ(object.alpha, 1.0f);
    EXPECT_EQ(object.targetAlpha, 1.0f);
    EXPECT_EQ(object.state.uv.x, 0.0f);
    EXPECT_EQ(object.state.uv.y, 0.0f);
    EXPECT_EQ(object.animation.action, 0);
    EXPECT_EQ(object.animation.frame, 0.0f);
    EXPECT_EQ(object.animation.previousAction, 0);
    EXPECT_EQ(object.animation.previousKey, 0);
    EXPECT_EQ(object.transform.lockedRootHeight, 0.0f);
    EXPECT_FALSE(previous[0].alive);
    EXPECT_EQ(previous[0].boundsMaximum, glm::vec3(900.0f));
}

// Catches truncating the type before validating the selector or falling into another map's branch.
TEST(WorldObjectsTest, SignedFileTypesOutsideTheMapZeroSelectorKeepDefaults)
{
    for (const std::int16_t type : std::array<std::int16_t, 6>{-32768, -515, -1, 151, 153, 32767})
    {
        Client::Data::Terrain::TerrainObject record{.type = type, .scale = 7.0f};
        Client::Kernel::WorldObjectRegistrations registry;
        const auto objects = Client::Tests::CheckedWorldObjectProjection(
            std::span(&record, 1), Lorencia, Client::WorldObjects::ObjectScene::Other, &registry);
        ASSERT_EQ(objects.size(), 1u);
        EXPECT_EQ(objects[0].type, type);
        EXPECT_EQ(objects[0].state.blendMesh, -1);
        EXPECT_EQ(objects[0].state.hiddenMesh, -1);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].speed), 0x3E23D70Au);
        EXPECT_EQ(objects[0].boundsMaximum, glm::vec3(40.0f, 40.0f, 80.0f));
        EXPECT_EQ(registry.entries[0].active, 0);
    }
}

// Catches ordering by type/ID instead of by sectors and creation queue.
TEST(WorldObjectsTest, RegistrationDoesNotChangeSectorAndInsertionDrawOrder)
{
    const std::array<Client::Data::Terrain::TerrainObject, 5> records{
        Client::Data::Terrain::TerrainObject{.type = 133, .position = {2000.0f, 2000.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 6, .position = {100.0f, 2000.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 145, .position = {100.0f, 100.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 146, .position = {100.0f, 2000.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 52, .position = {2000.0f, 100.0f, 0.0f}, .scale = 1.0f}};
    Client::Kernel::WorldObjectRegistrations registry;
    const auto objects = Client::Tests::CheckedWorldObjectProjection(
        records, Lorencia, Client::WorldObjects::ObjectScene::Other, &registry);
    ASSERT_EQ(objects.size(), 5u);
    EXPECT_EQ(Client::WorldObjects::DrawOrder(objects), (std::vector<std::size_t>{2, 1, 3, 4, 0}));
    for (const WorldObject& object : objects)
    {
        EXPECT_FALSE(object.seen);
    }
}

// 0x604ab0 exits after the alpha if it fell below 0.01: it neither advances nor runs the map cases.
TEST(WorldObjectsTest, AnObjectFadedOutDoesNotStep)
{
    WorldObject whirlpool = Created(0x56, 100.0f, 100.0f).front();
    whirlpool.alpha = 0.005f;
    whirlpool.targetAlpha = 0.0f;
    const float yaw = whirlpool.transform.angles.z;
    Client::WorldObjects::StepWorldObject(whirlpool, Login, nullptr);
    EXPECT_LT(whirlpool.alpha, 0.01f);
    EXPECT_EQ(whirlpool.transform.angles.z, yaw) << "el giro del caso 0x56 no corre";
    EXPECT_EQ(whirlpool.state.blendMesh, Client::Models::NoBlendMesh);
}

// The CRT rand() with seed 7 after `draws` calls.
Client::Crt::CrtRandom SevenAfter(int draws)
{
    Client::Crt::CrtRandom random(7);
    for (int k = 0; k < draws; ++k)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// 0x604bc4 and 0x604c06: before the alpha, the tavern roofs of the 0 (0x7d and 0x7e) go to target 0 with the hero's
// layer at 4, and those of the 2 (0x51, 0x52, 0x60, 0x62 and 0x63) with the layer at 3 or from 10 up; to 1 otherwise.
// The same types on the other map are not touched, and nobody takes a rand().
TEST(WorldObjectsTest, TheRoofsFollowTheLayerOfTheHero)
{
    struct Case
    {
        std::uint8_t map;
        std::int16_t type;
        std::int32_t layer;
        float target;
    };
    for (const Case& probe : {Case{Lorencia, 0x7d, 4, 0.0f}, Case{Lorencia, 0x7e, 4, 0.0f},
                              Case{Lorencia, 0x7d, 3, 1.0f}, Case{Lorencia, 0x7e, 0, 1.0f}, Case{Devias, 0x51, 3, 0.0f},
                              Case{Devias, 0x52, 10, 0.0f}, Case{Devias, 0x60, 12, 0.0f}, Case{Devias, 0x62, 9, 1.0f},
                              Case{Devias, 0x63, 4, 1.0f}, Case{Lorencia, 0x51, 3, 0.5f}, Case{Devias, 0x7d, 4, 0.5f}})
    {
        SCOPED_TRACE(std::to_string(probe.map) + " " + std::to_string(probe.type) + " " + std::to_string(probe.layer));
        WorldObject roof = Created(probe.type, 100.0f, 100.0f, probe.map).front();
        roof.targetAlpha = 0.5f;
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::WorldObjects::StepWorldObjectChecked(
            roof, probe.map, nullptr, random, Client::Kernel::ObjectStepGlobals{.heroCellLayer = probe.layer});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(roof.targetAlpha, probe.target);
        EXPECT_EQ(random.State(), 7u);
    }
}

// 0x604db9 and 0x60508f: the windows of the 0 (0x75 and 0x7a) and the 2's 0x4e take the blendMesh light with a
// rand(), (rand() & 3 + 4) · 0.1f stored in float. The 2's 0x75 has no case.
TEST(WorldObjectsTest, TheWindowsFlickerWithARand)
{
    struct Case
    {
        std::uint8_t map;
        std::int16_t type;
        int draws;
    };
    for (const Case& probe :
         {Case{Lorencia, 0x75, 1}, Case{Lorencia, 0x7a, 1}, Case{Devias, 0x4e, 1}, Case{Devias, 0x75, 0}})
    {
        SCOPED_TRACE(std::to_string(probe.map) + " " + std::to_string(probe.type));
        WorldObject window = Created(probe.type, 100.0f, 100.0f, probe.map).front();
        const float before = window.state.light;
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::WorldObjects::StepWorldObjectChecked(window, probe.map, nullptr, random,
                                                                          Client::Kernel::ObjectStepGlobals{});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), SevenAfter(probe.draws).State());
        Client::Crt::CrtRandom expected(7);
        const float light =
            probe.draws == 0
                ? before
                : static_cast<float>(static_cast<double>((expected.Next() & 3) + 4) * static_cast<double>(0.1f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(window.state.light), std::bit_cast<std::uint32_t>(light));
    }
}

// 0x604d8e: the 0's 0x76 and 0x77 scroll the blendMesh V with the world clock: (−ftol(T) % 1000) · 0.001f, with
// idiv's signed remainder.
TEST(WorldObjectsTest, TheBlendMeshOfTheHousesScrollsWithTheClock)
{
    struct Case
    {
        std::int16_t type;
        float clock;
        int scroll;
    };
    for (const Case& probe : {Case{0x76, 1234.0f, -234}, Case{0x77, 999.9f, -999}, Case{0x76, 2000.5f, 0}})
    {
        SCOPED_TRACE(probe.clock);
        WorldObject house = Created(probe.type, 100.0f, 100.0f, Lorencia).front();
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::WorldObjects::StepWorldObjectChecked(
            house, Lorencia, nullptr, random, Client::Kernel::ObjectStepGlobals{.worldClock = probe.clock});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(house.state.uv.y,
                  static_cast<float>(static_cast<double>(probe.scroll) * static_cast<double>(0.001f)));
        EXPECT_EQ(house.state.uv.x, 0.0f);
        EXPECT_EQ(random.State(), 7u);
    }
}

// Without borrowed destinations, the case cuts at the service that needs them, with the caller's address and its RNG
// prefix. The alpha and the animation already ran.
TEST(WorldObjectsTest, MissingStepDestinationsKeepTheirAddressAndRandomPrefix)
{
    struct Case
    {
        std::uint8_t map;
        std::int16_t type;
        std::string_view address;
        int draws;
    };
    for (const Case& probe :
         {Case{Lorencia, 0x32, "0x604eb0", 5}, Case{Lorencia, 0x33, "0x604ecb", 5}, Case{Lorencia, 0x34, "0x604f22", 5},
          Case{Lorencia, 0x37, "0x604e78", 5}, Case{Lorencia, 0x50, "0x604e40", 5}, Case{Lorencia, 0x82, "0x604de3", 5},
          Case{Lorencia, 0x83, "0x604e02", 4}, Case{Lorencia, 0x84, "0x604e21", 4}, Case{Lorencia, 0x5a, "0x605718", 1},
          Case{Lorencia, 0x96, "0x605718", 1}, Case{Devias, 0x1e, "0x6050b9", 5}, Case{Devias, 0x42, "0x6050b9", 5},
          Case{Devias, 0x14, "0x6050d4", 0}, Case{Devias, 0x41, "0x6050d4", 0}, Case{Devias, 0x56, "0x6050d4", 0},
          Case{Devias, 0x58, "0x6050d4", 0}})
    {
        SCOPED_TRACE(std::to_string(probe.map) + " " + std::to_string(probe.type));
        WorldObject object = Created(probe.type, 100.0f, 100.0f, probe.map).front();
        object.alpha = 0.5f;
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::WorldObjects::StepWorldObjectChecked(object, probe.map, nullptr, random,
                                                                          Client::Kernel::ObjectStepGlobals{});
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find(probe.address), std::string::npos) << stepped.Error();
        EXPECT_GT(object.alpha, 0.5f) << "el alpha ya fue hacia su meta";
        EXPECT_EQ(random.State(), SevenAfter(probe.draws).State());
    }
}

// With the alpha below 0.01, 0x604ab0 exits before the case: the window takes no rand(). The roof, which goes before
// the alpha, does change its target.
TEST(WorldObjectsTest, AFadedOutObjectSkipsItsCaseButNotItsRoof)
{
    WorldObject window = Created(0x7a, 100.0f, 100.0f, Lorencia).front();
    window.alpha = 0.005f;
    window.targetAlpha = 0.0f;
    Client::Crt::CrtRandom random(7);
    ASSERT_TRUE(Client::WorldObjects::StepWorldObjectChecked(window, Lorencia, nullptr, random,
                                                             Client::Kernel::ObjectStepGlobals{})
                    .HasValue());
    EXPECT_EQ(random.State(), 7u);

    WorldObject roof = Created(0x7d, 100.0f, 100.0f, Lorencia).front();
    roof.alpha = 0.005f;
    roof.targetAlpha = 0.0f;
    ASSERT_TRUE(Client::WorldObjects::StepWorldObjectChecked(roof, Lorencia, nullptr, random,
                                                             Client::Kernel::ObjectStepGlobals{})
                    .HasValue());
    EXPECT_EQ(roof.targetAlpha, 1.0f);
}

// The game step knows the ported maps and the 55 (with the same as StepWorldObject); on another one, like the 35
// (World36, with no .att in the Data), it cuts before touching the object.
TEST(WorldObjectsTest, TheCheckedStepKnowsTheReferenceMapsAndTheLogin)
{
    WorldObject other = Created(1, 100.0f, 100.0f, 3).front();
    other.alpha = 0.5f;
    Client::Crt::CrtRandom random(7);
    const auto stepped =
        Client::WorldObjects::StepWorldObjectChecked(other, 35, nullptr, random, Client::Kernel::ObjectStepGlobals{});
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x604ab0"), std::string::npos) << stepped.Error();
    EXPECT_EQ(other.alpha, 0.5f);

    WorldObject whirlpool = Created(0x56, 100.0f, 100.0f).front();
    WorldObject login = whirlpool;
    ASSERT_TRUE(Client::WorldObjects::StepWorldObjectChecked(whirlpool, Login, nullptr, random,
                                                             Client::Kernel::ObjectStepGlobals{})
                    .HasValue());
    Client::WorldObjects::StepWorldObject(login, Login, nullptr);
    EXPECT_EQ(whirlpool.transform.angles.z, login.transform.angles.z);
    EXPECT_EQ(whirlpool.alpha, login.alpha);
    EXPECT_EQ(whirlpool.state.blendMesh, login.state.blendMesh);
    EXPECT_EQ(random.State(), 7u);
}

// 0x5ffce0 without per-vertex light: the terrain light times 0.1 of G and B is stored in float before adding the
// object's; R's is added from the x87. With this R the two computations differ in the last bit.
TEST(WorldObjectsTest, TheRedOfAnUnlitObjectIsNotRoundedFirst)
{
    WorldObject logo = Created(1, 100.0f, 100.0f, Lorencia).front();
    logo.vertexLight = false;
    logo.light = glm::vec3(std::bit_cast<float>(0x3F142C32u));
    const glm::vec3 terrain(std::bit_cast<float>(0x3F01DAADu));
    const auto draw = Client::WorldObjects::DrawOf(logo, terrain, Lorencia);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.x), 0x3F212877u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.y), 0x3F212876u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.z), 0x3F212876u);
}

// 0x600a4a: the 0's 0x69 (Waterspout01) goes with its four 0x4ecc30, with the blendMesh 3 from its creation and the
// color of 0x5ffce0. Hidden (+0x58 at −2) or with the alpha below 0.01, nothing; on the 2 it is not a case, and goes
// through the common RenderBody.
TEST(WorldObjectsTest, TheLorenciaWaterspoutDrawsItsOwnMeshes)
{
    WorldObject fountain = Created(Client::WorldObjects::Waterspout, 100.0f, 100.0f, Lorencia).front();
    fountain.light = glm::vec3(0.25f);
    const glm::vec3 terrain(0.5f);
    const auto draw = Client::WorldObjects::DrawOf(fountain, terrain, Lorencia);
    EXPECT_EQ(draw.kind, Client::Kernel::ObjectDrawKind::Waterspout);
    EXPECT_TRUE(draw.posed);
    EXPECT_EQ(draw.state.blendMesh, 3);
    EXPECT_EQ(draw.color, glm::vec3(0.75f));
    EXPECT_TRUE(draw.vertexLight);

    WorldObject hidden = fountain;
    hidden.state.hiddenMesh = Client::Kernel::HiddenObject;
    EXPECT_EQ(Client::WorldObjects::DrawOf(hidden, terrain, Lorencia).kind, Client::Kernel::ObjectDrawKind::Hidden);
    WorldObject faded = fountain;
    faded.alpha = 0.005f;
    EXPECT_EQ(Client::WorldObjects::DrawOf(faded, terrain, Lorencia).kind, Client::Kernel::ObjectDrawKind::Hidden);

    const WorldObject devias = Created(Client::WorldObjects::Waterspout, 100.0f, 100.0f, Devias).front();
    EXPECT_EQ(Client::WorldObjects::DrawOf(devias, terrain, Devias).kind, Client::Kernel::ObjectDrawKind::Body);
}

// The first seed from 1 whose first rand() is odd (`odd`) or even.
std::uint32_t SeedWithFirstRand(bool odd)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Crt::CrtRandom random(seed);
        if (((random.Next() % 2) != 0) == odd)
        {
            return seed;
        }
    }
}

// 0x603011, the 0x69 case in 0x602ed0: the blendMesh light at 1, the V with the clock like the 0x76's
// (−ftol(T) % 1000) · 0.001f, and rand() % 2. Odd, nothing more; even, the two rand() of the first drop (x and z) and
// the cut at 0x53fe70 (0x6030c7), which leads to the game's particles.
TEST(WorldObjectsTest, TheWaterspoutFrameScrollsItsVAndDropsHalfTheTime)
{
    for (const bool odd : {true, false})
    {
        SCOPED_TRACE(odd);
        WorldObject fountain = Created(Client::WorldObjects::Waterspout, 100.0f, 100.0f, Lorencia).front();
        fountain.state.light = 0.25f;
        fountain.state.uv.x = 0.5f;
        const std::uint32_t seed = SeedWithFirstRand(odd);
        Client::Crt::CrtRandom random(seed);
        const auto stepped = Client::WorldObjects::StepTheWaterspoutFrame(fountain, random, 1234.0f);
        EXPECT_EQ(fountain.state.light, 1.0f);
        EXPECT_EQ(fountain.state.uv.y, static_cast<float>(-234.0 * static_cast<double>(0.001f)));
        EXPECT_EQ(fountain.state.uv.x, 0.5f);
        Client::Crt::CrtRandom expected(seed);
        static_cast<void>(expected.Next());
        if (odd)
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(random.State(), expected.State());
            continue;
        }
        const int x = (expected.Next() % 32) - 16;
        const int z = (expected.Next() % 32) - 16;
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("0x6030c7"), std::string::npos) << stepped.Error();
        EXPECT_NE(stepped.Error().find("(" + std::to_string(x) + ", −20, " + std::to_string(z) + ")"),
                  std::string::npos)
            << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
    }
}

// Without a credited ftol of the clock, 0x603011 cuts at 0x60301e with the light already at 1, the V as it was and no
// rand().
TEST(WorldObjectsTest, TheWaterspoutFrameNeedsACreditedClock)
{
    WorldObject fountain = Created(Client::WorldObjects::Waterspout, 100.0f, 100.0f, Lorencia).front();
    fountain.state.light = 0.25f;
    fountain.state.uv.y = 0.5f;
    Client::Crt::CrtRandom random(7);
    const auto stepped =
        Client::WorldObjects::StepTheWaterspoutFrame(fountain, random, std::numeric_limits<float>::infinity());
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x60301e"), std::string::npos) << stepped.Error();
    EXPECT_EQ(fountain.state.light, 1.0f);
    EXPECT_EQ(fountain.state.uv.y, 0.5f);
    EXPECT_EQ(random.State(), 7u);
}

// 0x602ed0: the light of the lantern's two sprites, L = (rand() % 30 + 70) · 0.01f stored in float, and
// (L; L · 0.5; L · 0.2f), each product stored in float.
TEST(WorldObjectsTest, TheLanternGlowIsOrange)
{
    const auto bits = [](const glm::vec3& light)
    {
        return std::array<std::uint32_t, 3>{std::bit_cast<std::uint32_t>(light.x),
                                            std::bit_cast<std::uint32_t>(light.y),
                                            std::bit_cast<std::uint32_t>(light.z)};
    };
    EXPECT_EQ(bits(Client::WorldObjects::StreetLightGlowLight(0)),
              (std::array<std::uint32_t, 3>{0x3F333333u, 0x3EB33333u, 0x3E0F5C29u}));
    EXPECT_EQ(bits(Client::WorldObjects::StreetLightGlowLight(59)),
              (std::array<std::uint32_t, 3>{0x3F7D70A3u, 0x3EFD70A3u, 0x3E4AC083u}));
    EXPECT_EQ(bits(Client::WorldObjects::StreetLightGlowLight(13)),
              (std::array<std::uint32_t, 3>{0x3F547AE1u, 0x3ED47AE1u, 0x3E29FBE7u}));
}
} // namespace
