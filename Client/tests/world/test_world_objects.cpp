#include "support/CheckedWorldObjectProjection.h"

#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/ObjectState.h"
#include "World/WorldObjectRegistrations.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

// Client::World::CreateWorldObjects, StepWorldObject y DrawOf en los casos que los 276 vectores del
// mapa 55 no tocan (06 § El mundo del login, la revisión de la CL7.2).
namespace
{
using Client::World::WorldObject;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;

std::vector<WorldObject> Created(std::int16_t type, float x, float y, std::uint8_t map = Login)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {x, y, 0.0f};
    record.scale = 1.0f;
    return Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), map, Client::World::ObjectScene::Login);
}

// 0x60666c: la posición por 0,000625f en la x87, sin redondear el producto, y ftol. En los múltiplos de
// 1.600 el float sube al entero siguiente; la x87 no.
TEST(WorldObjectsTest, TheSectorIsTheProductWithoutRounding)
{
    EXPECT_EQ(Client::World::SectorX(Created(1, 1600.0f, 0.0f).front()), 0);
    EXPECT_EQ(Client::World::SectorX(Created(1, 14400.0f, 0.0f).front()), 8);
    EXPECT_EQ(Client::World::SectorX(Created(1, 8000.0f, 0.0f).front()), 4);
    const auto edge = Created(1, 25600.0f, 25600.0f);
    ASSERT_EQ(edge.size(), 1u) << "25.600 es el sector 15: se crea";
    EXPECT_EQ(Client::World::SectorX(edge.front()), 15);
    EXPECT_EQ(Client::World::SectorY(edge.front()), 15);
    EXPECT_EQ(Client::World::SectorX(Created(1, -1000.0f, 0.0f).front()), 0) << "ftol trunca hacia el 0";
    EXPECT_EQ(Client::World::SectorX(Created(1, -1600.0f, 0.0f).front()), 0) << "−0,99999…: todavía el 0";
    EXPECT_TRUE(Created(1, -1601.0f, 0.0f).empty());
    EXPECT_TRUE(Created(1, 25700.0f, 0.0f).empty());
}

// Un .obj con un NaN (World3, dos registros del tipo 86): ftol da 0x80000000 y 0x606660 no lo crea.
TEST(WorldObjectsTest, ANotANumberPositionIsNotCreated)
{
    EXPECT_TRUE(Created(0x56, 100.0f, std::numeric_limits<float>::quiet_NaN(), 3).empty());
}

// 0x606660 saca el sector de la posición del archivo, antes de su switch: los remolinos del 55, que el
// caso lleva a (8.600; 25.000; 5.000), quedan en el sector de donde estaban.
TEST(WorldObjectsTest, TheWhirlpoolsKeepTheSectorOfTheFile)
{
    const auto whirlpool = Created(0x56, 100.0f, 100.0f);
    ASSERT_EQ(whirlpool.size(), 1u);
    EXPECT_EQ(whirlpool.front().transform.position.x, 8600.0f);
    EXPECT_EQ(Client::World::SectorX(whirlpool.front()), 0);
    EXPECT_EQ(Client::World::SectorY(whirlpool.front()), 0);
}

// Fija una inferencia: 06 da +0xdc = -50 de Bridge01 como *inferido*; falta su lectura estática (P-C35 del 08).
// 0x6067ae inicializa +0xdc a c1f00000; solo 0x6069bd (mapa 0, Bridge01) pone c2480000.
// La rama del mapa corre después de los casos de escena, también fuera del juego.
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
    for (const auto scene : {Client::World::ObjectScene::Other, Client::World::ObjectScene::Login,
                             Client::World::ObjectScene::CharacterSelect})
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

// Detecta casos omitidos del switch 0, un blend heredado por error o el falso fallthrough a mapa 1.
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
        Client::World::WorldObjectRegistrations registry;
        const auto objects = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), Lorencia,
                                                                         Client::World::ObjectScene::Other, &registry);
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

// Detecta si el caso de escena se saltea en mapas comunes o corre fuera de las escenas 2 y 4.
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
    for (const auto scene : {Client::World::ObjectScene::Login, Client::World::ObjectScene::CharacterSelect,
                             Client::World::ObjectScene::Other})
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
                Client::World::WorldObjectRegistrations registry;
                const auto objects =
                    Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), map, scene, &registry);
                ASSERT_EQ(objects.size(), 1u);
                const WorldObject& object = objects.front();
                const bool special = scene != Client::World::ObjectScene::Other;
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

// Detecta tabla descartada al crear otro batch, IDs ligados a direcciones y registro del tipo equivocado.
TEST(WorldObjectsTest, LorenciaRegistersOnlyItsFourTypesInTheCallersExistingTable)
{
    Client::World::WorldObjectRegistrations registry;
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
    auto first =
        Client::Tests::CheckedWorldObjectProjection(records, Lorencia, Client::World::ObjectScene::Other, &registry);
    ASSERT_EQ(first.size(), 6u);
    EXPECT_EQ(registry.entries[0].active, 2);
    EXPECT_EQ(registry.entries[0].object, 987u);
    EXPECT_EQ(registry.entries[0].metadata, (std::array<std::uint8_t, 7>{7, 6, 5, 4, 3, 2, 1}));
    constexpr std::array<Client::World::WorldObjectIdentity, 5> registered{1000, 1001, 1002, 1003, 1005};
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
    const auto second = Client::Tests::CheckedWorldObjectProjection(std::span(records).first(1), Lorencia,
                                                                    Client::World::ObjectScene::Other, &registry);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].identity, 1006u);
    EXPECT_EQ(registry.entries[6].object, 1006u);
    EXPECT_EQ(registry.nextIdentity, 1007u);
    EXPECT_EQ(moved[0].identity, registry.entries[1].object);
}

// Detecta que el límite auxiliar se confunda con un pool de objetos o reinicialice metadata/objetos.
TEST(WorldObjectsTest, FullAuxiliaryTableStillCreatesEveryNewObject)
{
    Client::World::WorldObjectRegistrations registry;
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
    const auto objects =
        Client::Tests::CheckedWorldObjectProjection(records, Lorencia, Client::World::ObjectScene::Other, &registry);
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
    for (const auto scene : {Client::World::ObjectScene::Login, Client::World::ObjectScene::CharacterSelect,
                             Client::World::ObjectScene::Other})
    {
        for (const std::uint8_t map : std::array<std::uint8_t, 3>{0, 1, 55})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(scene));
            Client::World::WorldObjectRegistrations registry;
            const auto objects = Client::Tests::CheckedWorldObjectProjection(records, map, scene, &registry);
            ASSERT_EQ(objects.size(), 4u);
            EXPECT_EQ(registry.entries[0].active, map == 0 ? 1 : 0);
            EXPECT_EQ(registry.entries[3].active, map == 0 ? 1 : 0);
            EXPECT_EQ(registry.entries[4].active, 0);
            EXPECT_EQ(registry.nextIdentity, 5u);
        }
    }
}

// Detecta el redondeo binary32 prematuro del recíproco (7 da ...a2 si se derrama).
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
                                                                             Client::World::ObjectScene::Other);
            ASSERT_EQ(objects.size(), 1u);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].speed), test.speed);
            EXPECT_EQ(objects[0].boundsMinimum, glm::vec3(-150.0f, -150.0f, 0.0f));
            EXPECT_EQ(objects[0].boundsMaximum, glm::vec3(150.0f, 150.0f, 500.0f));
        }
    }
}

// Detecta defaults prestados de actores y datos heredados del batch anterior.
TEST(WorldObjectsTest, NewObjectsHaveFreshConstructorStateAndTheSecondPositionCopy)
{
    Client::World::WorldObjectRegistrations registry;
    Client::Data::Terrain::TerrainObject record{
        .type = 6, .position = {1600.0f, 14400.0f, 25.0f}, .angle = {10.0f, 20.0f, 30.0f}, .scale = 2.0f};
    auto previous = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), Lorencia,
                                                                Client::World::ObjectScene::Other, &registry);
    ASSERT_EQ(previous.size(), 1u);
    previous[0].alive = false;
    previous[0].alpha = 0.0f;
    previous[0].seen = true;
    previous[0].boundsMaximum = glm::vec3(900.0f);
    const auto created = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), Lorencia,
                                                                     Client::World::ObjectScene::Other, &registry);
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

// Detecta truncar el tipo antes de validar el selector o caer a una rama de otro mapa.
TEST(WorldObjectsTest, SignedFileTypesOutsideTheMapZeroSelectorKeepDefaults)
{
    for (const std::int16_t type : std::array<std::int16_t, 6>{-32768, -515, -1, 151, 153, 32767})
    {
        Client::Data::Terrain::TerrainObject record{.type = type, .scale = 7.0f};
        Client::World::WorldObjectRegistrations registry;
        const auto objects = Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), Lorencia,
                                                                         Client::World::ObjectScene::Other, &registry);
        ASSERT_EQ(objects.size(), 1u);
        EXPECT_EQ(objects[0].type, type);
        EXPECT_EQ(objects[0].state.blendMesh, -1);
        EXPECT_EQ(objects[0].state.hiddenMesh, -1);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(objects[0].speed), 0x3E23D70Au);
        EXPECT_EQ(objects[0].boundsMaximum, glm::vec3(40.0f, 40.0f, 80.0f));
        EXPECT_EQ(registry.entries[0].active, 0);
    }
}

// Detecta orden por tipo/ID en vez de sectores y cola de creación.
TEST(WorldObjectsTest, RegistrationDoesNotChangeSectorAndInsertionDrawOrder)
{
    const std::array<Client::Data::Terrain::TerrainObject, 5> records{
        Client::Data::Terrain::TerrainObject{.type = 133, .position = {2000.0f, 2000.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 6, .position = {100.0f, 2000.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 145, .position = {100.0f, 100.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 146, .position = {100.0f, 2000.0f, 0.0f}, .scale = 1.0f},
        Client::Data::Terrain::TerrainObject{.type = 52, .position = {2000.0f, 100.0f, 0.0f}, .scale = 1.0f}};
    Client::World::WorldObjectRegistrations registry;
    const auto objects =
        Client::Tests::CheckedWorldObjectProjection(records, Lorencia, Client::World::ObjectScene::Other, &registry);
    ASSERT_EQ(objects.size(), 5u);
    EXPECT_EQ(Client::World::DrawOrder(objects), (std::vector<std::size_t>{2, 1, 3, 4, 0}));
    for (const WorldObject& object : objects)
    {
        EXPECT_FALSE(object.seen);
    }
}

// 0x604ab0 sale después del alpha si quedó por debajo de 0,01: no avanza ni corre los casos del mapa.
TEST(WorldObjectsTest, AnObjectFadedOutDoesNotStep)
{
    WorldObject whirlpool = Created(0x56, 100.0f, 100.0f).front();
    whirlpool.alpha = 0.005f;
    whirlpool.targetAlpha = 0.0f;
    const float yaw = whirlpool.transform.angles.z;
    Client::World::StepWorldObject(whirlpool, Login, nullptr);
    EXPECT_LT(whirlpool.alpha, 0.01f);
    EXPECT_EQ(whirlpool.transform.angles.z, yaw) << "el giro del caso 0x56 no corre";
    EXPECT_EQ(whirlpool.state.blendMesh, Client::Render::Models::NoBlendMesh);
}

// 0x5ffce0 sin luz por vértice: la luz del terreno por 0,1 de G y B se guarda en float antes de sumarle
// la del objeto; la de R se suma desde la x87. Con este R las dos cuentas difieren en el último bit.
TEST(WorldObjectsTest, TheRedOfAnUnlitObjectIsNotRoundedFirst)
{
    WorldObject logo = Created(1, 100.0f, 100.0f, Lorencia).front();
    logo.vertexLight = false;
    logo.light = glm::vec3(std::bit_cast<float>(0x3F142C32u));
    const glm::vec3 terrain(std::bit_cast<float>(0x3F01DAADu));
    const auto draw = Client::World::DrawOf(logo, terrain, Lorencia);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.x), 0x3F212877u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.y), 0x3F212876u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.z), 0x3F212876u);
}

// 0x602ed0: la luz de los dos sprites del farol, L = (rand() % 30 + 70) · 0,01f guardado en float, y
// (L; L · 0,5; L · 0,2f), cada producto guardado en float.
TEST(WorldObjectsTest, TheLanternGlowIsOrange)
{
    const auto bits = [](const glm::vec3& light)
    {
        return std::array<std::uint32_t, 3>{std::bit_cast<std::uint32_t>(light.x),
                                            std::bit_cast<std::uint32_t>(light.y),
                                            std::bit_cast<std::uint32_t>(light.z)};
    };
    EXPECT_EQ(bits(Client::World::StreetLightGlowLight(0)),
              (std::array<std::uint32_t, 3>{0x3F333333u, 0x3EB33333u, 0x3E0F5C29u}));
    EXPECT_EQ(bits(Client::World::StreetLightGlowLight(59)),
              (std::array<std::uint32_t, 3>{0x3F7D70A3u, 0x3EFD70A3u, 0x3E4AC083u}));
    EXPECT_EQ(bits(Client::World::StreetLightGlowLight(13)),
              (std::array<std::uint32_t, 3>{0x3F547AE1u, 0x3ED47AE1u, 0x3E29FBE7u}));
}
} // namespace
